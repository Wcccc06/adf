/* SPDX-License-Identifier: GPL-2.0 */
/*
 * adf_driver.c - 驱动层（LKM 内核模块, arm64）
 * 对标: LinYuDriverLoader / LinYuKernel 那一层
 *
 * 设备实测内核: 6.1.118-android14-11-o-g64180ab070e5
 *   MODULES=y / MODULE_SIG_FORCE 未开 / MODVERSIONS=y / CFI_CLANG=y
 *   RANDSTRUCT_NONE=y / STRICT_KERNEL_RWX=y / KALLSYMS_ALL=y
 * 加载: ksud insmod adf_driver.ko
 *
 * 挂钩（arm64 干净做法，不动 sys_call_table、不碰 CR0/x86 指令）：
 *   1) kprobe(__arm64_sys_getdents64).post   过滤目录项（ls / ps / readdir）
 *   2) kprobe(do_filp_open).pre             命中隐藏路径 -> 记录，post 阶段把 fd 关掉
 *   3) kprobe(do_filp_open).post            命中则返回 -ENOENT（调用方看到"文件不存在"）
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("adf");
MODULE_DESCRIPTION("adf driver: hide process info and ADB paths at syscall layer");
MODULE_VERSION("1.0.0");

#define ADF_PATH_LEN 128
#define ADF_BUF      512

static int hide_proc = 1;
static int hide_adb = 1;
static int filter_tgid = 0;
module_param(hide_proc, int, 0644);
module_param(hide_adb, int, 0644);
module_param(filter_tgid, int, 0644);

static const char *const default_names[] = {
    "adbd", "dfmenu", "linyu", "ksud", "magiskd", "su",
};
static const char *const default_paths[] = {
    "/system/bin/adb", "/system/bin/adbd", "/data/adb",
    "/data/local/tmp/adb", "/data/misc/adb", "/dev/usb-ffs/adb",
};

static bool adf_skip(void)
{
    if (!hide_proc && !hide_adb) return true;
    if (filter_tgid && current->tgid != filter_tgid) return true;
    return false;
}

static bool name_hidden(const char *n)
{
    int i;
    if (!n || !*n) return false;
    if (hide_proc)
        for (i = 0; i < ARRAY_SIZE(default_names); i++)
            if (!strcmp(n, default_names[i])) return true;
    if (hide_adb && strstr(n, "adb")) return true;
    return false;
}

static bool pid_hidden_str(const char *s, int len)
{
    char tmp[16];
    struct pid *pp;
    struct task_struct *t;
    bool hit;

    if (!hide_proc || len <= 0 || len > 7) return false;
    memcpy(tmp, s, len);
    tmp[len] = 0;
    pp = find_get_pid(simple_strtol(tmp, NULL, 10));
    if (!pp) return false;
    t = get_pid_task(pp, PIDTYPE_PID);
    put_pid(pp);
    if (!t) return false;
    hit = name_hidden(t->comm);
    put_task_struct(t);
    return hit;
}

static bool path_hidden(const char *p)
{
    int i;

    if (!p || !*p) return false;

    if (hide_adb)
        for (i = 0; i < ARRAY_SIZE(default_paths); i++)
            if (strstr(p, default_paths[i])) return true;

    if (hide_proc && !strncmp(p, "/proc/", 6)) {
        const char *q = p + 6;
        int k = 0;
        while (q[k] && q[k] != '/' && k < 15) k++;
        if (q[k] == '/' && pid_hidden_str(q, k)) return true;
    }
    return false;
}

/* ---------------- 1. getdents64 过滤 ---------------- */

static int getdents64_post(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{
    long ret;
    void __user *ubuf;
    char *kbuf;
    long out = 0, pos = 0;

    ret = (long)regs->regs[0];
    if (ret <= 0 || adf_skip() || ret > ADF_BUF) return 0;

    ubuf = (void __user *)regs->regs[1];
    if (!ubuf) return 0;

    kbuf = kmalloc(ret, GFP_ATOMIC);
    if (!kbuf) return 0;
    if (copy_from_user(kbuf, ubuf, ret)) { kfree(kbuf); return 0; }

    while (pos + (long)sizeof(struct linux_dirent64) <= ret) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(kbuf + pos);
        const char *n = d->d_name;
        bool drop = false;

        if (d->d_reclen < sizeof(struct linux_dirent64)) break;
        if (pos + d->d_reclen > ret) break;

        if (name_hidden(n)) {
            drop = true;
        } else if (hide_proc) {
            int i = 0, allnum = 1;
            while (n[i] && i < 15) { if (n[i] < '0' || n[i] > '9') { allnum = 0; break; } i++; }
            if (allnum && i > 0) drop = pid_hidden_str(n, i);
        }

        if (!drop) {
            if (out != pos) memmove(kbuf + out, kbuf + pos, d->d_reclen);
            out += d->d_reclen;
        }
        pos += d->d_reclen;
    }

    if (out != ret) {
        if (out == 0) regs->regs[0] = 0;
        else if (!copy_to_user(ubuf, kbuf, out)) regs->regs[0] = out;
    }
    kfree(kbuf);
    return 0;
}

/* ---------------- 2 + 3. 打开/stat 拦截 ---------------- */
/*
 * 内核原型: struct file *do_filp_open(int dfd, struct filename *pathname,
 *                                     const struct open_flags *op)
 * pre 阶段：从 struct filename 里取名字，命中就打标记（per-cpu 变量）
 * post 阶段：命中的话把刚拿到的 file 关掉并返回 ERR_PTR(-ENOENT)
 *
 * 说明：kprobe 的 pre_handler 无法"跳过原函数"（arm64 没有 kprobe override，
 * 该功能属于 BPF 且需 CONFIG_BPF_KPROBE_OVERRIDE）。所以用 pre 判定 + post 生效。
 */

struct adf_filename {
    const char *name;
    const char __user *uptr;
    int refcnt;
};

/* pre 与 post 在同一个 task/CPU 上执行，用 per-cpu 标记传递命中状态 */
static DEFINE_PER_CPU(int, adf_hit);

static int filp_open_pre(struct kprobe *p, struct pt_regs *regs)
{
    struct adf_filename *fn = (struct adf_filename *)regs->regs[1];

    this_cpu_write(adf_hit, 0);
    if (adf_skip() || !fn || !fn->name) return 0;
    if (path_hidden(fn->name)) this_cpu_write(adf_hit, 1);
    return 0;
}

static int filp_open_post(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{
    struct file *fp;

    if (!this_cpu_read(adf_hit)) return 0;
    this_cpu_write(adf_hit, 0);

    fp = (struct file *)regs->regs[0];
    if (fp && !IS_ERR(fp)) filp_close(fp, NULL);
    regs->regs[0] = (unsigned long)ERR_PTR(-ENOENT);
    return 0;
}

/* ---------------- 安装 / 卸载 ---------------- */

static struct kprobe kp_getdents64 = {
    .symbol_name = "__arm64_sys_getdents64",
    .post_handler = getdents64_post,
};
static struct kprobe kp_filp_open = {
    .symbol_name = "do_filp_open",
    .pre_handler = filp_open_pre,
    .post_handler = filp_open_post,
};

static int __init adf_init(void)
{
    int rc1, rc2;

    rc1 = register_kprobe(&kp_getdents64);
    rc2 = register_kprobe(&kp_filp_open);

    pr_info("adf: loaded getdents64=%d filp_open=%d hide_proc=%d hide_adb=%d\n",
            rc1, rc2, hide_proc, hide_adb);
    return 0;
}

static void __exit adf_exit(void)
{
    unregister_kprobe(&kp_filp_open);
    unregister_kprobe(&kp_getdents64);
    pr_info("adf: unloaded\n");
}

module_init(adf_init);
module_exit(adf_exit);
