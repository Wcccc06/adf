/* SPDX-License-Identifier: GPL-2.0 */
/*
 * adf_host_so.c - 宿主，编译成"可执行共享库" .so
 * 对标 lib20260914_082802.so：带 INTERP，能直接被启动器当程序运行，也能被 dlopen
 * 功能：隐藏进程信息 + 隐藏 ADB 路径（展示状态、自检、给内核模块下发配置）
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/system_properties.h>

#define ADF_VER "1.0.0"

static const char *k_adb_paths[] = {
    "/system/bin/adb", "/system/bin/adbd", "/data/adb",
    "/data/misc/adb", "/dev/usb-ffs/adb", NULL
};
static const char *k_names[] = {
    "adbd", "dfmenu", "linyu", "ksud", "magiskd", "su", NULL
};

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

static int scan_names(char *hit, size_t cap)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    int found = 0;
    if (hit && cap) hit[0] = 0;
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
                if (hit && strlen(hit) + strlen(comm) + 2 < cap) {
                    strcat(hit, comm);
                    strcat(hit, " ");
                }
            }
        }
    }
    closedir(d);
    return found;
}

static int kernel_module_loaded(void)
{
    return access("/sys/module/adf_lkm", F_OK) == 0;
}

static void push_config(void)
{
    const char *keys[] = { "hide_proc", "hide_adb", "filter_tgid", NULL };
    const char *vals[] = { "1", "1", "0", NULL };
    for (int i = 0; keys[i]; i++) {
        char p[128];
        FILE *f;
        snprintf(p, sizeof(p), "/sys/module/adf_lkm/parameters/%s", keys[i]);
        f = fopen(p, "w");
        if (f) { fputs(vals[i], f); fclose(f); }
    }
}

static void cmd_status(void)
{
    char hit[512];
    const char *props[] = { "init.svc.adbd", "service.adb.tcp.port", "persist.sys.usb.config", NULL };
    char val[256];

    printf("adf host v%s\n", ADF_VER);
    printf("  uid            = %d%s\n", getuid(), getuid() == 0 ? " (root)" : "");
    printf("  kernel module  = %s\n", kernel_module_loaded() ? "loaded" : "not loaded");
    printf("  visible pids   = %d\n", count_proc_pids());
    printf("  visible names  = %d  %s\n", scan_names(hit, sizeof(hit)), hit);

    printf("  ADB paths:\n");
    for (int i = 0; k_adb_paths[i]; i++) {
        struct stat st;
        printf("    %-28s %s\n", k_adb_paths[i], stat(k_adb_paths[i], &st) == 0 ? "VISIBLE" : "hidden");
    }
    printf("  ADB props:\n");
    for (int i = 0; props[i]; i++) {
        __system_property_get(props[i], val);
        printf("    %-24s = [%s]\n", props[i], val);
    }
}

static void cmd_check(void)
{
    char hit[512];
    int n = scan_names(hit, sizeof(hit));
    int bad = 0;

    printf("== hide check ==\n");
    if (n > 0) { printf("  [FAIL] still visible: %s\n", hit); bad++; }
    else printf("  [PASS] no hidden-name process visible\n");

    for (int i = 0; k_adb_paths[i]; i++) {
        struct stat st;
        if (stat(k_adb_paths[i], &st) == 0) {
            printf("  [FAIL] path visible: %s\n", k_adb_paths[i]);
            bad++;
        }
    }
    printf("  kernel module = %s\n", kernel_module_loaded() ? "loaded" : "not loaded");
    printf("== result: %s ==\n", bad ? "FAIL" : "PASS");
}

int adf_main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "status";
    if (cmd[0] == '-') cmd = "status";   /* 兼容 -k <卡密> --record-mirror */

    if (!strcmp(cmd, "status")) cmd_status();
    else if (!strcmp(cmd, "check")) cmd_check();
    else if (!strcmp(cmd, "config")) { push_config(); printf("config pushed\n"); cmd_status(); }
    else { printf("usage: %s [status|check|config]\n", argv[0]); cmd_status(); }
    return 0;
}

__attribute__((constructor)) static void adf_ctor(void) { }
int main(int argc, char **argv) { return adf_main(argc, argv); }
