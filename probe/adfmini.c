// SPDX-License-Identifier: GPL-2.0
/* 最小对照模块：只做 printk，用来判断 ENOEXEC 是我们的代码问题还是环境问题 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("adf");
MODULE_DESCRIPTION("adf minimal probe module");

static int __init adfmini_init(void)
{
    pr_info("adfmini: hello from kernel\n");
    return 0;
}

static void __exit adfmini_exit(void)
{
    pr_info("adfmini: bye\n");
}

module_init(adfmini_init);
module_exit(adfmini_exit);
