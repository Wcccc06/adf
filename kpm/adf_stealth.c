// SPDX-License-Identifier: GPL-2.0
/*
 * adf-stealth.c - 隐藏 ADB 路径 + 隐藏进程信息（KernelPatch KPM）
 *
 * 合并自：
 *   kelexine/kpm-rootstealth (GPL-2.0)  —— UID 门控、路径规范化、openat/statx/readlinkat 钩子
 *   klcok/kpm-hide-env       (GPL-2.0)  —— /data/adb 系列路径列表
 *   idandev/hidefile         (Apache-2.0)—— getdents64 目录项过滤思路
 *
 * 为什么合并：rootstealth v1.0.2 为避开内核内存分配器删掉了 getdents64 钩子，
 * 导致 ls/readdir 仍能看到 /data/adb 的条目。本版本用逐条读取（不分配缓冲）把它补回来。
 */
#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <syscall.h>
#include <hook.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define __ARCH_WANT_NEW_STAT
#include <uapi/asm-generic/unistd.h>
#ifndef __NR_newfstatat
#define __NR_newfstatat 79
#endif
#ifndef __NR_statx
#define __NR_statx 291
#endif
#ifndef __NR_faccessat
#define __NR_faccessat 48
#endif

KPM_NAME("adf-stealth");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("adf");
KPM_DESCRIPTION("Hide ADB paths and process info from untrusted UIDs");

#define TAG            "[adf] "
#define MAX_BLOCKLIST  64
#define MAX_PATH_LEN   256
#define UID_TRUSTED_MAX 2000
#define ENOENT_CODE    2

typedef struct { char path[MAX_PATH_LEN]; int active; } bl_entry_t;
static bl_entry_t s_list[MAX_BLOCKLIST];
static int s_count = 0;

static const char *const DEFAULT_NAMES[] = {
    "adbd", "dfmenu", "linyu", "ksud", "magiskd", "zygiskd", "su",
    NULL,
};

static const char *const DEFAULT_BLOCKED[] = {
    "/data/adb",
    "/data/adb/magisk",
    "/data/adb/ksu",
    "/data/adb/ap",
    "/data/adb/modules",
    "/data/adb/kpmodules",
    "/data/adb/post-fs-data.d",
    "/data/adb/service.d",
    "/data/adb/adf",
    "/sbin/.magisk", "/sbin/magisk", "/sbin/su",
    "/system/xbin/su", "/system/bin/su", "/system/su",
    "/system/bin/.ext/.su", "/system/app/Superuser.apk",
    "/system/bin/adb", "/system/bin/adbd", "/system/xbin/adb",
    "/vendor/bin/adb", "/vendor/bin/adbd",
    "/data/local/tmp/adb", "/data/misc/adb", "/dev/usb-ffs/adb",
    "/sys/class/android_usb/android0/f_adb",
    "/dev/magisk", "/cache/magisk.log", "/cache/magisk_install.log",
    NULL,
};

static void blocklist_init(void)
{
    int i;
    memset(s_list, 0, sizeof(s_list));
    s_count = 0;
    for (i = 0; DEFAULT_BLOCKED[i] && s_count < MAX_BLOCKLIST; i++) {
        strncpy(s_list[s_count].path, DEFAULT_BLOCKED[i], MAX_PATH_LEN - 1);
        s_list[s_count].path[MAX_PATH_LEN - 1] = 0;
        s_list[s_count].active = 1;
        s_count++;
    }
}

static int name_blocked(const char *n)
{
    int i;
    if (!n || !*n) return 0;
    for (i = 0; DEFAULT_NAMES[i]; i++)
        if (!strcmp(n, DEFAULT_NAMES[i])) return 1;
    if (strstr(n, "adb")) return 1;
    return 0;
}

static int path_normalize(const char *in, char *out, int outlen)
{
    int o = 0, prev_slash = 0;
    if (!in) return -1;
    while (*in && o < outlen - 1) {
        if (*in == '/' && prev_slash) { in++; continue; }
        out[o++] = *in;
        prev_slash = (*in == '/');
        in++;
    }
    if (*in) return -1;
    if (o > 1 && out[o - 1] == '/') o--;
    if (o >= 2 && out[o - 1] == '.' && out[o - 2] == '/') o -= 2;
    if (o > 1 && out[o - 1] == '/') o--;
    out[o] = 0;
    return o;
}

static int is_blocked(const char *path)
{
    char norm[MAX_PATH_LEN];
    int i;
    if (!path || !path[0]) return 0;
    if (path_normalize(path, norm, sizeof(norm)) < 0) return 1;
    for (i = 0; i < s_count; i++) {
        const char *b;
        size_t blen;
        if (!s_list[i].active) continue;
        b = s_list[i].path;
        blen = strlen(b);
        if (!blen) continue;
        if (!strcmp(norm, b)) return 1;
        if (!strncmp(norm, b, blen) && (norm[blen] == '/' || norm[blen] == 0)) return 1;
    }
    return 0;
}

static int is_trusted_uid(void)
{
    uid_t u = current_uid();
    return (u <= UID_TRUSTED_MAX);
}

static int is_proc_maps_path(const char *p)
{
    const char *q;
    int k = 0;
    if (strncmp(p, "/proc/", 6)) return 0;
    q = p + 6;
    while (q[k] && q[k] != '/') k++;
    if (k == 0) return 0;
    if (!strcmp(q + k, "/maps") || !strcmp(q + k, "/smaps") ||
        !strncmp(q + k, "/map_files", 10) || !strcmp(q + k, "/cmdline") ||
        !strcmp(q + k, "/comm"))
        return 1;
    return 0;
}

static void check_path4(hook_fargs4_t *args)
{
    char kpath[MAX_PATH_LEN];
    const char __user *up;
    if (is_trusted_uid()) return;
    up = (const char __user *)(uintptr_t)args->arg1;
    if (!up) return;
    if (compat_strncpy_from_user(kpath, up, sizeof(kpath) - 1) < 0) return;
    kpath[sizeof(kpath) - 1] = 0;
    if (is_blocked(kpath) || is_proc_maps_path(kpath)) {
        args->ret = -ENOENT_CODE;
        args->skip_origin = 1;
    }
}

static void before_openat(hook_fargs4_t *args, void *udata)    { (void)udata; check_path4(args); }
static void before_faccessat(hook_fargs4_t *args, void *udata) { (void)udata; check_path4(args); }
static void before_newfstatat(hook_fargs4_t *args, void *udata){ (void)udata; check_path4(args); }
static void before_readlinkat(hook_fargs4_t *args, void *udata) { (void)udata; check_path4(args); }

static void before_statx(hook_fargs5_t *args, void *udata)
{
    char kpath[MAX_PATH_LEN];
    const char __user *up;
    (void)udata;
    if (is_trusted_uid()) return;
    up = (const char __user *)(uintptr_t)args->arg1;
    if (!up) return;
    if (compat_strncpy_from_user(kpath, up, sizeof(kpath) - 1) < 0) return;
    kpath[sizeof(kpath) - 1] = 0;
    if (is_blocked(kpath) || is_proc_maps_path(kpath)) {
        args->ret = -ENOENT_CODE;
        args->skip_origin = 1;
    }
}

static int u_read(void *dst, const char *src, int n)
{
    return compat_copy_to_user((void __user *)dst, (const void *)src, n);
}

static int u_reclen(const char *p)
{
    unsigned char hdr[8];
    if (u_read(hdr, p, 8) != 0) return -1;
    return (int)(hdr[4] | (hdr[5] << 8));
}

static void compact_block(char __user *base, long from, long to, long n)
{
    char tmp[512];
    long done = 0;
    while (done < n) {
        int chunk = (int)(n - done);
        if (chunk > (int)sizeof(tmp)) chunk = (int)sizeof(tmp);
        if (u_read(tmp, (const char *)(base + from + done), chunk) != 0) return;
        if (compat_copy_to_user((void __user *)(base + to + done), tmp, chunk) != 0) return;
        done += chunk;
    }
}

static void getdents64_after(hook_fargs3_t *args, void *udata)
{
    char __user *base;
    long ret, pos = 0, out = 0;
    (void)udata;
    if (is_trusted_uid()) return;
    ret = (long)args->ret;
    if (ret <= 0) return;
    base = (char __user *)syscall_argn(args, 1);
    if (!base) return;
    while (pos + 19 <= ret) {
        int reclen = u_reclen((const char *)(base + pos));
        char name[256];
        int drop = 0;
        if (reclen < 19 || pos + reclen > ret) break;
        memset(name, 0, sizeof(name));
        if (compat_strncpy_from_user(name, (const char __user *)(base + pos + 19),
                                     sizeof(name) - 1) >= 0) {
            name[sizeof(name) - 1] = 0;
            if (name_blocked(name)) drop = 1;
        }
        if (!drop) {
            if (out != pos) compact_block(base, pos, out, reclen);
            out += reclen;
        }
        pos += reclen;
    }
    if (out != ret) args->ret = out;
}

static int buf_cat(char *buf, int pos, int lim, const char *s)
{
    while (pos < lim - 1 && *s) buf[pos++] = *s++;
    buf[pos] = 0;
    return pos;
}

static long adf_ctl0(const char *args, char *__user out_msg, int outlen)
{
    char out[256];
    int n = 0, i;
    if (args && !strcmp(args, "off")) { for (i = 0; i < s_count; i++) s_list[i].active = 0; }
    else if (args && !strcmp(args, "on")) { for (i = 0; i < s_count; i++) s_list[i].active = 1; }
    n = buf_cat(out, n, sizeof(out), "adf-stealth active=");
    n = buf_cat(out, n, sizeof(out), "ok");
    if (out_msg && outlen > 0) compat_copy_to_user(out_msg, out, n < outlen ? n : outlen);
    return 0;
}

static long adf_init(const char *args, const char *event, void *__user reserved)
{
    int rc = 0;
    blocklist_init();
    pr_info(TAG "init kpver=0x%x entries=%d\n", kpver, s_count);
    rc |= hook_syscalln(__NR_openat, 4, before_openat, NULL, NULL);
    rc |= hook_syscalln(__NR_faccessat, 4, before_faccessat, NULL, NULL);
    rc |= hook_syscalln(__NR_newfstatat, 4, before_newfstatat, NULL, NULL);
    rc |= hook_syscalln(__NR_statx, 5, before_statx, NULL, NULL);
    rc |= hook_syscalln(__NR_readlinkat, 4, before_readlinkat, NULL, NULL);
    rc |= hook_syscalln(__NR_getdents64, 3, NULL, getdents64_after, NULL);
    pr_info(TAG "hooks rc=%d\n", rc);
    return 0;
}

static long adf_exit(void *__user reserved)
{
    unhook_syscalln(__NR_getdents64, NULL, getdents64_after);
    unhook_syscalln(__NR_readlinkat, before_readlinkat, NULL);
    unhook_syscalln(__NR_statx, before_statx, NULL);
    unhook_syscalln(__NR_newfstatat, before_newfstatat, NULL);
    unhook_syscalln(__NR_faccessat, before_faccessat, NULL);
    unhook_syscalln(__NR_openat, before_openat, NULL);
    pr_info(TAG "unloaded\n");
    return 0;
}

KPM_INIT(adf_init);
KPM_CTL0(adf_ctl0);
KPM_EXIT(adf_exit);
