# 内核 API 速查表

## 目录

- [模块](#模块)
- [打印与调试](#打印与调试)
- [内存管理](#内存管理)
- [字符设备](#字符设备)
- [时间与延时](#时间与延时)
- [用户态交互](#用户态交互)
- [错误码](#错误码)
- [常用头文件](#常用头文件)

## 模块

```c
#include <linux/module.h>

module_init(init_func);    // 模块入口
module_exit(exit_func);    // 模块出口

MODULE_LICENSE("GPL");           // 许可证（必须，否则内核报错）
MODULE_AUTHOR("Name");           // 作者
MODULE_DESCRIPTION("desc");      // 描述
MODULE_VERSION("1.0");           // 版本
MODULE_ALIAS("alias");           // 别名
MODULE_DEVICE_TABLE(of, table);  // 设备树匹配表导出
```

## 打印与调试

```c
#include <linux/kernel.h>

// 日志级别（数字越小级别越高）
// KERN_EMERG   0  系统不可用
// KERN_ALERT   1  需立即处理
// KERN_CRIT    2  严重
// KERN_ERR     3  错误
// KERN_WARNING 4  警告
// KERN_NOTICE  5  注意
// KERN_INFO    6  信息
// KERN_DEBUG   7  调试

printk(KERN_INFO "msg: %d, %s\n", val, str);

// 便捷宏（自动加级别前缀）
pr_emerg("..."); pr_alert("..."); pr_crit("...");
pr_err("...");   pr_warn("...");  pr_notice("...");
pr_info("...");  pr_debug("...");

// 带设备指针的打印（自动包含设备名）
dev_err(dev, "msg\n");
dev_warn(dev, "msg\n");
dev_info(dev, "msg\n");
dev_dbg(dev, "msg\n");

// 动态调试（需 CONFIG_DYNAMIC_DEBUG）
pr_debug("only with dynamic debug\n");
```

查看日志：`dmesg`、`dmesg -w`（实时）、`journalctl -k`

## 内存管理

```c
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>

// 内核内存分配（物理连续，小内存首选）
void *kmalloc(size_t size, gfp_t flags);
void kfree(const void *objp);

// 清零分配
void *kzalloc(size_t size, gfp_t flags);

// 虚拟连续（大内存，不保证物理连续）
void *vmalloc(unsigned long size);
void vfree(const void *addr);

// 按页分配
struct page *alloc_page(gfp_t gfp_mask);
void __free_page(struct page *page);
unsigned long __get_free_page(gfp_t gfp_mask);

// GFP 标志
// GFP_KERNEL   - 普通内核分配，可能睡眠（进程上下文用）
// GFP_ATOMIC   - 原子分配，不睡眠（中断上下文用）
// GFP_DMA      - DMA 区域分配
// GFP_USER     - 用户页分配

// 设备托管分配（自动释放）
void *devm_kzalloc(struct device *dev, size_t size, gfp_t gfp);
```

## 字符设备

```c
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

// 设备号
dev_t dev = MKDEV(major, minor);
int major = MAJOR(dev);
int minor = MINOR(dev);

// 分配设备号
int alloc_chrdev_region(dev_t *dev, unsigned baseminor,
                        unsigned count, const char *name);
int register_chrdev_region(dev_t from, unsigned count, const char *name);
void unregister_chrdev_region(dev_t from, unsigned count);

// cdev
void cdev_init(struct cdev *cdev, const struct file_operations *fops);
int cdev_add(struct cdev *p, dev_t dev, unsigned count);
void cdev_del(struct cdev *p);

// 类与设备节点
struct class *class_create(struct module *owner, const char *name);
void class_destroy(struct class *cls);
struct device *device_create(struct class *cls, struct device *parent,
                             dev_t devt, void *drvdata, const char *fmt, ...);
void device_destroy(struct class *cls, dev_t devt);
```

## 时间与延时

```c
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

// 忙等待（不睡眠，任何上下文可用）
void ndelay(unsigned long nsecs);   // 纳秒
void udelay(unsigned long usecs);   // 微秒
void mdelay(unsigned long msecs);   // 毫秒（尽量避免，太久）

// 睡眠延时（进程上下文，会让出 CPU）
void msleep(unsigned int msecs);
unsigned long msleep_interruptible(unsigned int msecs);
void ssleep(unsigned int seconds);

// jiffies 与时间换算
unsigned long jiffies;  // 系统启动以来的节拍数
HZ;                     // 每秒节拍数（通常 100/250/300/1000）
msecs_to_jiffies(msec);
usecs_to_jiffies(usec);
jiffies_to_msecs(j);

// 超时判断
if (time_after(jiffies, timeout)) { /* 超时 */ }
if (time_before(jiffies, start)) { ... }

// 内核定时器
struct timer_list timer;
timer_setup(&timer, callback, 0);
timer.expires = jiffies + msecs_to_jiffies(1000);
add_timer(&timer);
del_timer_sync(&timer);
mod_timer(&timer, jiffies + new_timeout);
```

## 用户态交互

```c
#include <linux/uaccess.h>

// 拷贝数据（返回未拷贝的字节数，0表示成功）
unsigned long copy_to_user(void __user *to, const void *from, unsigned long n);
unsigned long copy_from_user(void *to, const void __user *from, unsigned long n);

// 简单变量
get_user(x, ptr);     // 从用户态读，x 是变量，ptr 是用户指针
put_user(x, ptr);     // 写用户态

// 字符串
strncpy_from_user(dst, src, count);
strnlen_user(src, maxlen);

// ioctl 命令码构造
#include <linux/ioctl.h>
_IO(type, nr)          // 无数据
_IOR(type, nr, size)   // 读（内核→用户）
_IOW(type, nr, size)   // 写（用户→内核）
_IOWR(type, nr, size)  // 读写
```

## 错误码

```c
#include <linux/errno.h>

// 常用错误码
-EINVAL    // 无效参数
-ENOMEM    // 内存不足
-EFAULT    // 地址错误（用户指针无效）
-EBUSY     // 设备忙
-ENODEV    // 无此设备
-ENOENT    // 无此文件/节点
-EAGAIN    // 暂时不可用（重试）
-EPERM     // 权限不足
-ETIMEDOUT // 超时
-ERESTARTSYS // 被信号中断，需重启系统调用

// 用 IS_ERR / PTR_ERR 处理指针型错误返回
void *ptr = some_func();
if (IS_ERR(ptr)) {
    int err = PTR_ERR(ptr);
    return err;
}
```

## 常用头文件

| 头文件 | 内容 |
|--------|------|
| `<linux/module.h>` | 模块宏 |
| `<linux/kernel.h>` | printk、pr_*、min/max |
| `<linux/init.h>` | __init/__exit 宏 |
| `<linux/fs.h>` | 文件系统、file_operations |
| `<linux/cdev.h>` | 字符设备 cdev |
| `<linux/device.h>` | class、device |
| `<linux/uaccess.h>` | copy_to/from_user |
| `<linux/slab.h>` | kmalloc/kfree |
| `<linux/gfp.h>` | GFP 标志 |
| `<linux/io.h>` | ioremap/iounmap、readl/writel |
| `<linux/of.h>` | 设备树 |
| `<linux/of_address.h>` | of_iomap |
| `<linux/of_irq.h>` | 中断解析 |
| `<linux/gpio.h>` | 传统 GPIO API |
| `<linux/gpio/consumer.h>` | GPIO 描述符 API |
| `<linux/interrupt.h>` | 中断、tasklet、workqueue |
| `<linux/spinlock.h>` | 自旋锁 |
| `<linux/mutex.h>` | 互斥锁 |
| `<linux/wait.h>` | 等待队列 |
| `<linux/delay.h>` | 延时函数 |
| `<linux/jiffies.h>` | jiffies |
| `<linux/errno.h>` | 错误码 |
| `<linux/err.h>` | IS_ERR/PTR_ERR/ERR_PTR |
