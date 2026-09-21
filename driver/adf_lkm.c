// SPDX-License-Identifier: GPL-2.0
/*
 * adf_lkm.c - 标准内核模块（LKM）版本
 * 适合内核允许加载未签名模块、但不支持 KPM 的设备（本机实测就是这种）
 *
 * 设备内核: 6.1.118-android14-11-o-g64180ab070e5
 *   CONFIG_MODULES=y / MODULE_SIG_FORCE 未开 / MODVERSIONS=y / KALLSYMS_ALL=y
 * 加载: ksud insmod adf_lkm.ko  或  insmod adf_lkm.ko
 *
 * 挂钩: kprobe(__arm64_sys_getdents64) 后置过滤 + kprobe(do_filp_open) 前置判定/后置返回 -ENOENT
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/uaccess.h>
#include <linux/dirent.h>
#include <linux/string.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("adf");
MODULE_DESCRIPTION("adf: hide process info and ADB paths at syscall layer (LKM)");
MODULE_VERSION("1.0.0");

#define ADF_PATH_LEN 128
#define ADF_ENT_MAX  256

static int hide_proc = 1;
static int hide_adb = 1;
static int filter_tgid = 0;
module_param(hide_proc, int, 0644);
module_param(hide_adb, int, 0644);
module_param(filter_tgid, int, 0644);

static const char *const k_names[] = {
    "adbd", "dfmenu", "linyu", "ksud", "magiskd", "su",
};
static const char *const k_paths[] = {
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
        for (i = 0; i < ARRAY_SIZE(k_names); i++)
            if (!strcmp(n, k_names[i])) return true;
    if (hide_adb && strstr(n, "adb")) return true;
    return false;
}

static bool path_hidden(const char *p)
{
    int i;
    if (!p || !*p) return false;
    if (hide_adb)
        for (i = 0; i < ARRAY_SIZE(k_paths); i++)
            if (strstr(p, k_paths[i])) return true;
    return false;
}

/* ---------------- getdents64 过滤 ---------------- */

static int getdents64_post(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{
    long ret;
    void __user *ubuf;
    long pos = 0, out = 0;

    if (adf_skip()) return 0;
    ret = (long)regs->regs[0];
    if (ret <= 0) return 0;
    ubuf = (void __user *)regs->regs[1];
    if (!ubuf) return 0;

    while (pos + (long)sizeof(struct linux_dirent64) <= ret) {
        struct linux_dirent64 hdr;
        unsigned short reclen;
        char name[ADF_ENT_MAX];
        bool drop = false;

        if (copy_from_user(&hdr, (char __user *)ubuf + pos, sizeof(hdr))) break;
        reclen = hdr.d_reclen;
        if (reclen < sizeof(struct linux_dirent64)) break;
        if (pos + reclen > ret) break;

        memset(name, 0, sizeof(name));
        if (strncpy_from_user(name, (char __user *)ubuf + pos + sizeof(hdr), sizeof(name) - 1) < 0) {
            pos += reclen;
            continue;
        }
        if (name_hidden(name)) drop = true;

        if (!drop) {
            if (out != pos) {
                char tmp[ADF_ENT_MAX];
                if (reclen <= sizeof(tmp) && !copy_from_user(tmp, (char __user *)ubuf + pos, reclen)) {
                    if (copy_to_user((char __user *)ubuf + out, tmp, reclen)) break;
                }
            }
            out += reclen;
        }
        pos += reclen;
    }

    if (out != ret) regs->regs[0] = out;
    return 0;
}

/* ---------------- do_filp_open 拦截 ---------------- */
/*
 * struct filename { const char *name; const char __user *uptr; int refcnt; };
 * 前置：判定命中 -> 置 per-cpu 标记
 * 后置：命中则关掉刚打开的 file 并返回 -ENOENT
 */
struct adf_filename {
    const char *name;
    const char __user *uptr;
    int refcnt;
};

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

    pr_info("adf_lkm: loaded getdents64=%d filp_open=%d hide_proc=%d hide_adb=%d\n",
            rc1, rc2, hide_proc, hide_adb);
    return 0;
}

static void __exit adf_exit(void)
{
    unregister_kprobe(&kp_filp_open);
    unregister_kprobe(&kp_getdents64);
    pr_info("adf_lkm: unloaded\n");
}

module_init(adf_init);
module_exit(adf_exit);
