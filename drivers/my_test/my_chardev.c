#include <linux/module.h>
#include <linux/fs.h>      // file_operations, register_chrdev_region
#include <linux/cdev.h>    // cdev
#include <linux/device.h>  // class_create, device_create
#include <linux/uaccess.h> // copy_to_user copy_from_user
#include <linux/slab.h>    // kmalloc

#define DEV_NAME "mychardev"
#define DEV_COUNT 1
#define BUF_SIZE 1024

struct my_dev
{
    struct cdev cdev; // 内嵌 cdev
    char buf[BUF_SIZE];
    size_t len;
};

static dev_t dev_num;          // 设备号（major+minor)
static struct class *my_class; // 自动在/sys/class下创建，触发udev建/dev节点
static struct my_dev *my_devp;

static int my_open(struct inode *inode, struct file *filp)
{
    // 通过inode->i_cdev(cdev类型)反查得到dev地址
    struct my_dev *dev = container_of(inode->i_cdev, struct my_dev, cdev);
    // 把设备实例指针存到file的private_data，read/write时直接使用
    filp->private_data = dev;
    pr_info("mychardev opened\n");
    return 0;
}

static int my_release(struct inode *inode, struct file *filp)
{
    pr_info("my_chardev closed\n");
    return 0;
};

static ssize_t my_write(struct file *filp, const char __user *ubuf, size_t count, loff_t *ppos)
{
    struct my_dev *dev = filp->private_data;
    size_t to_copy = min(count, (size_t)BUF_SIZE);

    if (copy_from_user(dev->buf, ubuf, to_copy))
        return -EFAULT;

    dev->len = to_copy;
    *ppos = 0; // 简化处理，每次写从头写
    return to_copy;
}

static ssize_t my_read(struct file *filp, char __user *ubuf, size_t count, loff_t *ppos)
{
    struct my_dev *dev = filp->private_data;
    size_t to_copy;
    if (*ppos >= dev->len)
        return 0;

    to_copy = min(count, dev->len - (size_t)*ppos);
    if (copy_to_user(ubuf, dev->buf + *ppos, to_copy))
    {
        return -EFAULT;
    }

    *ppos += to_copy;
    return to_copy;
}

static const struct file_operations my_fops = {
    .owner = THIS_MODULE, // 指向本模块，防止用户还在用设备时模块被rmmod卸载
    .open = my_open,
    .read = my_read,
    .write = my_write,
    .release = my_release,
};

static int __init my_init(void)
{
    int ret;
    // 动态申请设备号
    ret = alloc_chrdev_region(&dev_num, 0, DEV_COUNT, DEV_NAME);
    if (ret < 0)
        return ret;

    // 分配私有结构体
    my_devp = kzalloc(sizeof(struct my_dev), GFP_KERNEL);
    if (!my_devp)
    {
        ret = -ENOMEM;
        goto err_free_region;
    }

    // 初始化cdev并绑定fops
    cdev_init(&my_devp->cdev, &my_fops);
    my_devp->cdev.owner = THIS_MODULE;

    // 把cdev添加到内核，从此open这个设备号就会走my_fops
    ret = cdev_add(&my_devp->cdev, dev_num, DEV_COUNT);
    if (ret)
        goto err_free_dev;

    // 创建class(/sys/class/mychardev/) + device(触发udev建/dev/mychardev)
    my_class = class_create(THIS_MODULE, DEV_NAME);

    if (IS_ERR(my_class))
    {
        ret = PTR_ERR(my_class);
        goto err_cdev_del;
    }

    device_create(my_class, NULL, dev_num, NULL, DEV_NAME);

    pr_info("mychardev loaded, major=%d minor=%d\n", MAJOR(dev_num), MINOR(dev_num));

    return 0;

err_cdev_del:
    cdev_del(&my_devp->cdev);
err_free_dev:
    kfree(my_devp);
err_free_region:
    unregister_chrdev_region(dev_num, DEV_COUNT);

    return ret;
}

static void __exit my_exit(void)
{
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_devp->cdev);
    kfree(my_devp);
    unregister_chrdev_region(dev_num, DEV_COUNT);
    pr_info("mychardev unloaded\n");
}

module_init(my_init);
module_exit(my_exit);
MODULE_LICENSE("GPL");