// SPDX-License-Identifier: GPL-2.0
/* 最小安全模块：只打印一行日志，不挂任何钩子。用于验证编译产物能否被内核接受。 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("adf");
MODULE_DESCRIPTION("adf minimal probe - printk only, no hooks");

static int __init adfmini_init(void)
{
    pr_info("adfmini: module loaded OK\n");
    return 0;
}

static void __exit adfmini_exit(void)
{
    pr_info("adfmini: module unloaded\n");
}

module_init(adfmini_init);
module_exit(adfmini_exit);
