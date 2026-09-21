/* SPDX-License-Identifier: GPL-2.0 */
/*
 * adf_host.c - 宿主层（对标 LinYu 的 lib20260914_082802.so）
 * 一个可执行文件：检查驱动状态、下配置、自检隐藏效果。
 * 不带卡密、不联网、不加密 —— 全部可审计。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/system_properties.h>   /* __system_property_get */

#define ADF_VER "1.0.0"

static const char *k_adb_paths[] = {
    "/system/bin/adb", "/system/bin/adbd", "/data/adb",
    "/data/misc/adb", "/dev/usb-ffs/adb", NULL
};
static const char *k_names[] = {
    "adbd", "dfmenu", "linyu", "ksud", "magiskd", "su", NULL
};

static int have_root(void) { return getuid() == 0; }

/* 数一下 /proc 里可见的 pid 数量（作为前后对比基线） */
static int count_proc_pids(void)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d))) {
        const char *s = e->d_name;
        int i = 0, allnum = 1;
        while (s[i]) { if (s[i] < '0' || s[i] > '9') { allnum = 0; break; } i++; }
        if (allnum && i > 0) n++;
    }
    closedir(d);
    return n;
}

/* 检查名单里的进程名当前是否可见 */
static int scan_names(char *hit, size_t cap)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    int found = 0;
    hit[0] = 0;
    if (!d) return -1;
    while ((e = readdir(d))) {
        const char *s = e->d_name;
        int i = 0, allnum = 1;
        char path[64], comm[64];
        FILE *fp;
        while (s[i]) { if (s[i] < '0' || s[i] > '9') { allnum = 0; break; } i++; }
        if (!allnum || i == 0) continue;
        snprintf(path, sizeof(path), "/proc/%s/comm", s);
        fp = fopen(path, "r");
        if (!fp) continue;
        if (!fgets(comm, sizeof(comm), fp)) { fclose(fp); continue; }
        fclose(fp);
        comm[strcspn(comm, "\n")] = 0;
        for (int k = 0; k_names[k]; k++) {
            if (!strcmp(comm, k_names[k])) {
                found++;
                if (strlen(hit) + strlen(comm) + 2 < cap) {
                    strcat(hit, comm);
                    strcat(hit, " ");
                }
            }
        }
    }
    closedir(d);
    return found;
}

static void show_paths(void)
{
    printf("  ADB 路径可见性:\n");
    for (int i = 0; k_adb_paths[i]; i++) {
        struct stat st;
        printf("    %-28s %s\n", k_adb_paths[i],
               (stat(k_adb_paths[i], &st) == 0) ? "可见" : "不可见");
    }
}

static void show_props(void)
{
    const char *keys[] = { "init.svc.adbd", "service.adb.tcp.port",
                           "persist.sys.usb.config", "ro.debuggable", NULL };
    char val[256];
    printf("  ADB 属性可见性:\n");
    for (int i = 0; keys[i]; i++) {
        __system_property_get(keys[i], val);
        printf("    %-24s = [%s]\n", keys[i], val);
    }
}

static void cmd_status(void)
{
    printf("adf host v%s\n", ADF_VER);
    printf("  uid            = %d %s\n", getuid(), have_root() ? "(root)" : "(非 root)");
    printf("  /proc 可见 pid = %d\n", count_proc_pids());
    {
        char hit[512];
        int n = scan_names(hit, sizeof(hit));
        printf("  名单内进程可见 = %d  %s\n", n, hit);
    }
    show_paths();
    show_props();
}

static void cmd_check(void)
{
    char hit[512];
    int n = scan_names(hit, sizeof(hit));
    int bad = 0;

    printf("== 隐藏效果自检 ==\n");
    if (n > 0) { printf("  [FAIL] 仍可见的进程: %s\n", hit); bad++; }
    else printf("  [PASS] 名单内进程全部不可见\n");

    for (int i = 0; k_adb_paths[i]; i++) {
        struct stat st;
        if (stat(k_adb_paths[i], &st) == 0) {
            printf("  [FAIL] 路径仍可见: %s\n", k_adb_paths[i]);
            bad++;
        }
    }
    if (!bad) printf("  [PASS] ADB 路径全部不可见\n");

    printf("== 结果: %s ==\n", bad ? "FAIL" : "PASS");
    exit(bad ? 1 : 0);
}

static void cmd_config(int argc, char **argv)
{
    /* 通过内核模块参数/控制接口下发；这里给出与 dfl-kmod 一致的调用形式 */
    char cmd[512];
    int hproc = 1, hadb = 1, tgid = 0;

    for (int i = 2; i < argc; i++) {
        if (!strncmp(argv[i], "hide_proc=", 10)) hproc = atoi(argv[i] + 10);
        else if (!strncmp(argv[i], "hide_adb=", 9)) hadb = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "tgid=", 5)) tgid = atoi(argv[i] + 5);
    }
    snprintf(cmd, sizeof(cmd),
             "echo %d > /sys/module/adf_driver/parameters/hide_proc; "
             "echo %d > /sys/module/adf_driver/parameters/hide_adb; "
             "echo %d > /sys/module/adf_driver/parameters/filter_tgid",
             hproc, hadb, tgid);
    printf("下发配置: hide_proc=%d hide_adb=%d tgid=%d\n", hproc, hadb, tgid);
    int rc = system(cmd);
    printf("sysfs 写入 rc=%d\n", rc);
}

static void usage(void)
{
    printf("用法: adf_host <status|check|config [hide_proc=0|1] [hide_adb=0|1] [tgid=N]>\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) { cmd_status(); return 0; }
    if (!strcmp(argv[1], "status")) cmd_status();
    else if (!strcmp(argv[1], "check")) cmd_check();
    else if (!strcmp(argv[1], "config")) cmd_config(argc, argv);
    else usage();
    return 0;
}
