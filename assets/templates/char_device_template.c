/*
 * 字符设备驱动模板
 * 功能：实现一个可读写的虚拟字符设备
 * 编译后加载，会在 /dev 下创建 mydev 节点
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DEVICE_NAME "mydev"
#define CLASS_NAME  "mydev_class"
#define BUF_SIZE    1024

static char *kernel_buf;
static dev_t devno;
static struct cdev my_cdev;
static struct class *my_class;

static int my_open(struct inode *inode, struct file *filp)
{
    pr_info("%s: device opened\n", DEVICE_NAME);
    return 0;
}

static int my_release(struct inode *inode, struct file *filp)
{
    pr_info("%s: device closed\n", DEVICE_NAME);
    return 0;
}

static ssize_t my_read(struct file *filp, char __user *buf,
                       size_t count, loff_t *f_pos)
{
    if (*f_pos >= BUF_SIZE)
        return 0;
    if (count > BUF_SIZE - *f_pos)
        count = BUF_SIZE - *f_pos;
    if (copy_to_user(buf, kernel_buf + *f_pos, count))
        return -EFAULT;
    *f_pos += count;
    return count;
}

static ssize_t my_write(struct file *filp, const char __user *buf,
                        size_t count, loff_t *f_pos)
{
    if (*f_pos >= BUF_SIZE)
        return -ENOSPC;
    if (count > BUF_SIZE - *f_pos)
        count = BUF_SIZE - *f_pos;
    if (copy_from_user(kernel_buf + *f_pos, buf, count))
        return -EFAULT;
    *f_pos += count;
    return count;
}

static const struct file_operations my_fops = {
    .owner   = THIS_MODULE,
    .open    = my_open,
    .release = my_release,
    .read    = my_read,
    .write   = my_write,
};

static int __init mydev_init(void)
{
    int ret;

    /* 1. 动态分配设备号 */
    ret = alloc_chrdev_region(&devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("%s: alloc_chrdev_region failed\n", DEVICE_NAME);
        return ret;
    }

    /* 2. 初始化并注册 cdev */
    cdev_init(&my_cdev, &my_fops);
    ret = cdev_add(&my_cdev, devno, 1);
    if (ret < 0)
        goto err_cdev;

    /* 3. 创建设备类和设备节点 */
    my_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        goto err_class;
    }
    device_create(my_class, NULL, devno, NULL, DEVICE_NAME);

    /* 4. 分配内核缓冲区 */
    kernel_buf = kzalloc(BUF_SIZE, GFP_KERNEL);
    if (!kernel_buf) {
        ret = -ENOMEM;
        goto err_alloc;
    }

    pr_info("%s: loaded, major=%d\n", DEVICE_NAME, MAJOR(devno));
    return 0;

err_alloc:
    device_destroy(my_class, devno);
    class_destroy(my_class);
err_class:
    cdev_del(&my_cdev);
err_cdev:
    unregister_chrdev_region(devno, 1);
    return ret;
}

static void __exit mydev_exit(void)
{
    kfree(kernel_buf);
    device_destroy(my_class, devno);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(devno, 1);
    pr_info("%s: unloaded\n", DEVICE_NAME);
}

module_init(mydev_init);
module_exit(mydev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Character device driver template");
