# 字符设备驱动

## 目录

- [核心数据结构](#核心数据结构)
- [开发步骤](#开发步骤)
- [完整示例](#完整示例)
- [设备号管理](#设备号管理)
- [用户态交互](#用户态交互)

## 核心数据结构

### file_operations

字符设备的核心，定义用户态操作对应的内核函数：

```c
static const struct file_operations my_fops = {
    .owner      = THIS_MODULE,
    .open       = my_open,
    .release    = my_release,
    .read       = my_read,
    .write      = my_write,
    .unlocked_ioctl = my_ioctl,
    .llseek     = my_llseek,
};
```

### cdev

表示一个字符设备：

```c
static struct cdev my_cdev;
cdev_init(&my_cdev, &my_fops);   // 绑定 fops
cdev_add(&my_cdev, devno, 1);    // 注册到内核
cdev_del(&my_cdev);              // 注销
```

## 开发步骤

1. **分配设备号**
   - 静态：`register_chrdev_region(dev_t from, unsigned count, const char *name)`
   - 动态（推荐）：`alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count, const char *name)`

2. **初始化 cdev 并添加**
   - `cdev_init(&cdev, &fops)`
   - `cdev_add(&cdev, devno, count)`

3. **创建设备类和设备节点（自动 mdev/udev）**
   - `class_create(THIS_MODULE, "name")`
   - `device_create(cls, NULL, devno, NULL, "name")`

4. **实现 file_operations 函数**

5. **模块退出时逆序清理**
   - `device_destroy(cls, devno)` → `class_destroy(cls)` → `cdev_del(&cdev)` → `unregister_chrdev_region(devno, count)`

## 完整示例（虚拟字符设备）

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define BUF_SIZE 1024

static char *kernel_buf;
static dev_t devno;
static struct cdev my_cdev;
static struct class *my_class;

static int my_open(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "mydev: open\n");
    return 0;
}

static int my_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "mydev: release\n");
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

    ret = alloc_chrdev_region(&devno, 0, 1, "mydev");
    if (ret < 0) return ret;

    cdev_init(&my_cdev, &my_fops);
    ret = cdev_add(&my_cdev, devno, 1);
    if (ret < 0) goto err_cdev;

    my_class = class_create(THIS_MODULE, "mydev_class");
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        goto err_class;
    }
    device_create(my_class, NULL, devno, NULL, "mydev");

    kernel_buf = kzalloc(BUF_SIZE, GFP_KERNEL);
    if (!kernel_buf) {
        ret = -ENOMEM;
        goto err_alloc;
    }

    printk(KERN_INFO "mydev: loaded, major=%d\n", MAJOR(devno));
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
    printk(KERN_INFO "mydev: unloaded\n");
}

module_init(mydev_init);
module_exit(mydev_exit);
MODULE_LICENSE("GPL");
```

## 设备号管理

```c
// 主设备号 + 次设备号合成 dev_t
dev_t dev = MKDEV(major, minor);
int major = MAJOR(dev);
int minor = MINOR(dev);
```

- 主设备号：标识驱动程序
- 次设备号：标识同驱动下的不同设备
- 查看已注册：`cat /proc/devices`

## 用户态交互

- `copy_to_user()` / `copy_from_user()`：安全的数据拷贝，会检查用户指针有效性
- 不能直接解引用用户态指针！
- `ioctl`：用于控制类操作，命令码用 `_IO/_IOR/_IOW/_IOWR` 宏构造
- `mmap`：将内核内存映射到用户态，高性能场景使用
