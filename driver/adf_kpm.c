/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * adf_kpm.c - 内核模块（KernelPatch KPM 形式）
 * 只用 KernelPatch 提供的精简头，不用完整内核头 —— 这样才编得出来。
 *
 * 挂钩系统调用，在返回用户态之前把"进程信息"和"ADB 路径"抹掉：
 *   getdents64  -> 过滤目录项（ls / ps / readdir 全走这里）
 *   openat      -> 命中隐藏路径，返回 ENOENT
 *
 * 来源：KernelPatch (GPL-2.0) 的 hook API + syscall 文档；
 *       idandev/hidefile-kernel-module (Apache-2.0) 的 getdents64 过滤思路。
 */
#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <hook.h>
#include <syscall.h>
#include <kallsyms.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <uapi/asm-generic/unistd.h>

KPM_NAME("adf");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("adf");
KPM_DESCRIPTION("hide process info and ADB paths at the syscall layer");

#define ADF_MAX_NAMES 16
#define ADF_MAX_PATHS 24
#define ADF_NAME_LEN  32
#define ADF_PATH_LEN  96
#define ADF_BUF       512

static int adf_enabled = 1;
static int hide_proc = 1;
static int hide_adb = 1;

static int name_count = 0;
static char names[ADF_MAX_NAMES][ADF_NAME_LEN];
static int path_count = 0;
static char paths[ADF_MAX_PATHS][ADF_PATH_LEN];

static const char *const k_names[] = {
    "adbd", "dfmenu", "linyu", "ksud", "magiskd", "su",
};
static const char *const k_paths[] = {
    "/system/bin/adb", "/system/bin/adbd", "/data/adb",
    "/data/local/tmp/adb", "/data/misc/adb", "/dev/usb-ffs/adb",
};

/* ---------------- 字符串（内核里不用 libc） ---------------- */

static int s_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int s_has(const char *hay, const char *needle)
{
    if (!needle || !*needle) return 0;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

static void s_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!src) { dst[0] = 0; return; }
    for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int name_hidden(const char *n)
{
    int i;
    if (!n || !*n) return 0;
    if (hide_proc)
        for (i = 0; i < name_count; i++)
            if (s_eq(n, names[i])) return 1;
    if (hide_adb && s_has(n, "adb")) return 1;
    return 0;
}

static int path_hidden(const char *p)
{
    int i;
    if (!p || !*p) return 0;
    if (hide_adb)
        for (i = 0; i < path_count; i++)
            if (s_has(p, paths[i])) return 1;
    return 0;
}

/* ---------------- getdents64：过滤目录项 ---------------- */

/* linux_dirent64 布局（用户态可见的部分，不用内核头也能定义） */
struct adf_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char d_name[];
};

static void getdents64_after(hook_fargs3_t *args, void *udata)
{
    long ret;
    void __user *ubuf;
    char kbuf[ADF_BUF];
    long out = 0, pos = 0;

    if (!adf_enabled) return;

    ret = (long)args->ret;
    if (ret <= 0 || ret > ADF_BUF) return;

    ubuf = (void __user *)syscall_argn(args, 1);
    if (!ubuf) return;
    if (compat_copy_from_user(kbuf, ubuf, ret) != 0) return;

    while (pos + (long)sizeof(struct adf_dirent64) <= ret) {
        struct adf_dirent64 *d = (struct adf_dirent64 *)(kbuf + pos);
        int drop = 0;

        if (d->d_reclen < sizeof(struct adf_dirent64)) break;
        if (pos + d->d_reclen > ret) break;

        if (name_hidden(d->d_name)) drop = 1;

        if (!drop) {
            if (out != pos) memmove(kbuf + out, kbuf + pos, d->d_reclen);
            out += d->d_reclen;
        }
        pos += d->d_reclen;
    }

    if (out != ret) {
        if (out == 0) {
            args->ret = 0;
        } else if (compat_copy_to_user(ubuf, kbuf, out) == 0) {
            args->ret = out;
        }
    }
}

/* ---------------- openat：拦路径 ---------------- */

static void openat_before(hook_fargs4_t *args, void *udata)
{
    const char __user *up;
    char path[ADF_PATH_LEN];

    if (!adf_enabled) return;
    up = (const char __user *)syscall_argn(args, 1);
    if (!up) return;

    memset(path, 0, sizeof(path));
    if (compat_strncpy_from_user(path, up, sizeof(path) - 1) < 0) return;

    if (path_hidden(path)) {
        args->ret = -2;          /* -ENOENT */
        args->skip_origin = 1;   /* 跳过原 syscall 实现 */
    }
}

/* ---------------- 配置（用户态用 ksud kpm control 下发） ---------------- */

static void add_name(const char *v)
{
    if (name_count < ADF_MAX_NAMES) s_copy(names[name_count++], v, ADF_NAME_LEN);
}

static void add_path(const char *v)
{
    if (path_count < ADF_MAX_PATHS) s_copy(paths[path_count++], v, ADF_PATH_LEN);
}

static void parse_args(const char *args)
{
    char buf[KPM_ARGS_LEN];
    char *p;

    if (!args || !*args) return;
    s_copy(buf, args, sizeof(buf));

    p = buf;
    while (*p) {
        char *seg = p;
        while (*p && *p != ';' && *p != '\n') p++;
        if (*p) *p++ = 0;
        if (!*seg) continue;

        if (s_eq(seg, "off")) { adf_enabled = 0; continue; }
        if (s_eq(seg, "on")) { adf_enabled = 1; continue; }
        if (s_eq(seg, "hide_proc=0")) { hide_proc = 0; continue; }
        if (s_eq(seg, "hide_proc=1")) { hide_proc = 1; continue; }
        if (s_eq(seg, "hide_adb=0")) { hide_adb = 0; continue; }
        if (s_eq(seg, "hide_adb=1")) { hide_adb = 1; continue; }
        if (!strncmp(seg, "name=", 5)) { add_name(seg + 5); continue; }
        if (!strncmp(seg, "path=", 5)) { add_path(seg + 5); continue; }
    }
}

/* ---------------- 生命周期 ---------------- */

static long adf_init(const char *args, const char *event, void *__user reserved)
{
    int i, rc;

    for (i = 0; i < (int)(sizeof(k_names) / sizeof(k_names[0])); i++) add_name(k_names[i]);
    for (i = 0; i < (int)(sizeof(k_paths) / sizeof(k_paths[0])); i++) add_path(k_paths[i]);

    parse_args(args);

    rc = hook_syscalln(__NR_getdents64, 3, NULL, getdents64_after, NULL);
    pr_info("adf: hook getdents64 rc=%d\n", rc);

    rc = hook_syscalln(__NR_openat, 4, openat_before, NULL, NULL);
    pr_info("adf: hook openat rc=%d\n", rc);

    pr_info("adf: loaded names=%d paths=%d hide_proc=%d hide_adb=%d\n",
            name_count, path_count, hide_proc, hide_adb);
    return 0;
}

static long adf_control0(const char *args, char *__user out_msg, int outlen)
{
    char out[128];
    int n;

    parse_args(args);

    n = snprintf(out, sizeof(out), "enabled=%d names=%d paths=%d hide_proc=%d hide_adb=%d\n",
                 adf_enabled, name_count, path_count, hide_proc, hide_adb);
    if (out_msg && outlen > 0) compat_copy_to_user(out_msg, out, n < outlen ? n : outlen);
    return 0;
}

static long adf_exit(void *__user reserved)
{
    unhook_syscalln(__NR_openat, openat_before, NULL);
    unhook_syscalln(__NR_getdents64, NULL, getdents64_after);
    pr_info("adf: unloaded\n");
    return 0;
}

KPM_INIT(adf_init);
KPM_CTL0(adf_control0);
KPM_EXIT(adf_exit);
