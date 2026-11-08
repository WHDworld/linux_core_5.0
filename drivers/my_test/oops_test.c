#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

static void create_oops(void)
{
    *(int *)0 = 0;
}

static int __init oops_test_init(void)
{
    printk("oops module init\n");
    create_oops();
    return 0;
}

static void __exit oops_test_exit(void)
{
    printk("goodbye\n");
}

MODULE_LICENSE("GPL");

module_init(oops_test_init);
module_exit(oops_test_exit);