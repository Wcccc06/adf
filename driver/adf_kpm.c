/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * adf_kpm.c - 内核模块（KernelPatch KPM 形式）
 *
 * 只使用 KernelPatch 实际导出的 API（已核对头文件）：
 *   compat_strncpy_from_user(char*, const char __user*, long)   —— 读用户态字符串
 *   compat_copy_to_user(void __user*, const void*, int)          —— 写用户态（反向用于读 8 字节字段）
 *   hook_syscalln / unhook_syscalln / syscall_argn / skip_origin —— syscall 挂钩
 *
 * 功能：隐藏进程信息 + 隐藏 ADB 路径
 *   getdents64 后置回调：把命中名单的目录项从返回缓冲区里挤掉
 *   openat 前置回调：命中隐藏路径直接返回 -ENOENT
 */
#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <hook.h>
#include <syscall.h>
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
#define ADF_ENT_MAX   256

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

/* ---------------- 字符串工具（内核里没有 libc） ---------------- */

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

/* 反向读用户态（compat_copy_to_user 当 from_user 用，需绕开 __user 检查） */
static int u_read(void *dst, const char *src, int n)
{
    return compat_copy_to_user((void __user *)dst, (const void *)src, n);
}

/* 读用户态 d_reclen（目录项头两个字段，前 8 字节里的低 2 字节） */
static int u_reclen(const char *p)
{
    unsigned char hdr[8];
    if (u_read(hdr, p, 8) != 0) return -1;
    return (int)(hdr[4] | (hdr[5] << 8));
}

/* 挤压式过滤：把保留的目录项往前挪，尾部留残渣 */
static void compact_block(char __user *base, long from, long to, long n)
{
    char tmp[ADF_ENT_MAX];
    long done = 0;
    while (done < n) {
        int chunk = (int)(n - done);
        if (chunk > (int)sizeof(tmp)) chunk = (int)sizeof(tmp);
        if (u_read(tmp, (const char *)(base + from + done), chunk) != 0) return;
        if (compat_copy_to_user((void __user *)(base + to + done), tmp, chunk) != 0) return;
        done += chunk;
    }
}

/* ---------------- getdents64 后置：过滤目录项 ---------------- */

static void getdents64_after(hook_fargs3_t *args, void *udata)
{
    char __user *base;
    long ret, pos = 0, out = 0;

    if (!adf_enabled) return;

    ret = (long)args->ret;
    if (ret <= 0) return;

    base = (char __user *)syscall_argn(args, 1);
    if (!base) return;

    while (pos + 19 <= ret) {
        int reclen = u_reclen((const char *)(base + pos));
        char name[ADF_ENT_MAX];
        int drop = 0;

        if (reclen < 19 || pos + reclen > ret) break;

        if (compat_strncpy_from_user(name, (const char __user *)(base + pos + 19),
                                     sizeof(name) - 1) >= 0) {
            name[sizeof(name) - 1] = 0;
            if (name_hidden(name)) drop = 1;
        }

        if (!drop) {
            if (out != pos) compact_block(base, pos, out, reclen);
            out += reclen;
        }
        pos += reclen;
    }

    if (out != ret) args->ret = out;
}

/* ---------------- openat 前置：拦路径 ---------------- */

static void openat_before(hook_fargs4_t *args, void *udata)
{
    char path[ADF_PATH_LEN];
    const char __user *up;

    if (!adf_enabled) return;
    up = (const char __user *)syscall_argn(args, 1);
    if (!up) return;

    memset(path, 0, sizeof(path));
    if (compat_strncpy_from_user(path, up, sizeof(path) - 1) < 0) return;
    path[sizeof(path) - 1] = 0;

    if (path_hidden(path)) {
        args->ret = -2;          /* -ENOENT */
        args->skip_origin = 1;   /* 不执行原 syscall */
    }
}

/* ---------------- 配置下发（ksud kpm control adf "..."） ---------------- */

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

    pr_info("adf: loaded names=%d paths=%d\n", name_count, path_count);
    return 0;
}

static long adf_control0(const char *args, char *__user out_msg, int outlen)
{
    char out[64];
    int n = 0;

    parse_args(args);

    s_copy(out, "adf enabled=", sizeof(out));
    n = 12;
    out[n++] = (char)('0' + (adf_enabled ? 1 : 0));
    out[n++] = '\n';
    out[n] = 0;

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
