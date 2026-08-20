# 驱动调试技巧

## 目录

- [printk 调试](#printk-调试)
- [proc 文件系统](#proc-文件系统)
- [sysfs 调试接口](#sysfs-调试接口)
- [debugfs](#debugfs)
- [内核崩溃分析](#内核崩溃分析)
- [常见问题排查](#常见问题排查)
- [动态调试](#动态调试)
- [性能分析](#性能分析)

## printk 调试

最基础也最有效的调试手段。

```c
// 在关键路径加打印
pr_info("%s: enter, irq=%d\n", __func__, irq);

// 打印寄存器值
pr_info("reg 0x%08x = 0x%08x\n", REG_CTRL, readl(base + REG_CTRL));

// 打印设备树信息
pr_info("compatible: %s\n", np->name);
```

### 控制 printk 输出级别

```bash
# 查看当前控制台日志级别
cat /proc/sys/kernel/printk
# 输出: 4 4 1 7
# 分别是: 控制台级别, 默认消息级别, 最低控制台级别, 启动时默认

# 临时调整（显示所有级别）
echo 8 > /proc/sys/kernel/printk

# dmesg 查看所有级别（不受控制台级别限制）
dmesg
dmesg -w        # 实时跟踪
dmesg -c        # 查看并清空
dmesg | grep mydriver
```

## proc 文件系统

创建只读/读写文件查看驱动内部状态：

```c
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *proc_entry;

static int my_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "status: %d\n", dev->status);
    seq_printf(m, "count: %d\n", dev->count);
    return 0;
}

static int my_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, my_proc_show, NULL);
}

static const struct proc_ops my_proc_ops = {
    .proc_open    = my_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

// 在 init/probe 中
proc_entry = proc_create("mydriver", 0444, NULL, &my_proc_ops);

// 清理
proc_remove(proc_entry);
```

使用：`cat /proc/mydriver`

## sysfs 调试接口

通过 kobject 属性文件暴露状态：

```c
#include <linux/sysfs.h>

static ssize_t my_attr_show(struct device *dev,
                            struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", my_value);
}

static ssize_t my_attr_store(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf, size_t count)
{
    sscanf(buf, "%d", &my_value);
    return count;
}

static DEVICE_ATTR(my_value, 0644, my_attr_show, my_attr_store);

// 注册
device_create_file(dev, &dev_attr_my_value);

// 注销
device_remove_file(dev, &dev_attr_my_value);
```

使用：

```bash
cat /sys/class/myclass/mydev/my_value
echo 5 > /sys/class/myclass/mydev/my_value
```

## debugfs

专门用于调试的虚拟文件系统（不保证在生产环境存在）：

```c
#include <linux/debugfs.h>

static struct dentry *debug_dir;

debug_dir = debugfs_create_dir("mydriver", NULL);
debugfs_create_u32("reg_val", 0644, debug_dir, &reg_val);
debugfs_create_x32("reg_hex", 0644, debug_dir, &reg_val);
debugfs_create_bool("enabled", 0644, debug_dir, &enabled);

// 清理
debugfs_remove_recursive(debug_dir);
```

挂载：`mount -t debugfs none /sys/kernel/debug`

## 内核崩溃分析

### Oops 信息解读

内核崩溃时打印 Oops 信息，关键部分：

```
Unable to handle kernel NULL pointer dereference at virtual address 00000000
pgd = 8c624000
[00000000] *pgd=8c62c831, *pte=00000000, *ppte=00000000
Internal error: Oops: 17 [#1] PREEMPT SMP ARM
Modules linked in: mydriver(O+)
CPU: 0 PID: 1234 Comm: insmod Tainted: G           O   5.4.0
...
PC is at my_probe+0x48/0x120 [mydriver]
LR is at my_probe+0x30/0x120 [mydriver]
...
Stack: (0xec08bda0 to 0xec08c000)
...
Code: e59b0008 e3500000 0a000003 e59b300c (e5933000)
```

关键点：

- **错误类型**：`NULL pointer dereference`、`prefetch abort` 等
- **PC 值**：`my_probe+0x48` — 崩溃在哪个函数的哪个偏移
- **调用栈**：从栈回溯找到调用链
- **Code**：崩溃点附近的指令，`()` 中是触发崩溃的指令

### 用 addr2line 定位代码行

```bash
# 找到函数偏移对应的源码行
arm-linux-gnueabihf-addr2line -e mydriver.ko -f -C 0x48
# 输出:
# my_probe
# /path/to/driver.c:123
```

### 反汇编模块

```bash
arm-linux-gnueabihf-objdump -d mydriver.ko > mydriver.dis
# 搜索 my_probe 查看汇编
```

## 常见问题排查

### 1. 模块加载失败

```bash
# 查看详细错误
dmesg | tail -20

# 常见原因
# - 版本不匹配（vermagic）：modprobe 提示 Invalid module format
# - 缺少符号：dmesg 提示 unknown symbol
# - 依赖未加载：先加载依赖模块
```

### 2. probe 不执行

```bash
# 检查设备树节点是否存在
ls /proc/device-tree/my_device/

# 检查 compatible 是否匹配
cat /proc/device-tree/my_device/compatible | xxd

# 检查驱动是否注册
cat /sys/bus/platform/drivers/my_driver/

# 手动绑定（测试用）
echo "my_device" > /sys/bus/platform/drivers/my_driver/bind
```

### 3. 中断不触发

```bash
# 检查中断号是否正确
cat /proc/interrupts | grep mydriver

# 检查中断是否被禁用
cat /proc/irq/<irq>/smp_affinity

# 确认中断标志与硬件触发方式匹配
# 上升沿设备不能用电平触发
```

### 4. 系统卡死/死机

- 检查是否在中断上下文调用了睡眠函数（mutex_lock、kmalloc(GFP_KERNEL) 等）
- 检查是否死锁（同一把锁重复加锁）
- 检查是否有死循环
- 加 watchdog 或在关键位置加 printk 定位卡死点

### 5. copy_to/from_user 失败

- 检查用户指针是否有效
- 确认在进程上下文调用（中断上下文不能用）
- 返回值非 0 表示有未拷贝字节，通常是用户地址无效

## 动态调试

```bash
# 挂载 debugfs
mount -t debugfs none /sys/kernel/debug

# 启用某文件的 pr_debug
echo 'file mydriver.c +p' > /sys/kernel/debug/dynamic_debug/control

# 启用某模块所有调试
echo 'module mydriver +p' > /sys/kernel/debug/dynamic_debug/control

# 启用某函数
echo 'func my_probe +p' > /sys/kernel/debug/dynamic_debug/control

# 查看当前启用的调试点
cat /sys/kernel/debug/dynamic_debug/control | grep +p

# 关闭
echo 'file mydriver.c -p' > /sys/kernel/debug/dynamic_debug/control
```

驱动中使用 `pr_debug()` 或 `dev_dbg()`，配合动态调试可在运行时开关，无需重新编译。

## 性能分析

```bash
# 查看函数执行时间（ftrace）
cd /sys/kernel/debug/tracing
echo function > current_tracer
echo my_driver_func > set_ftrace_filter
cat trace

# 测量代码段执行时间
#include <linux/ktime.h>
ktime_t start, end;
s64 delta;

start = ktime_get();
// ... 被测代码 ...
end = ktime_get();
delta = ktime_to_us(ktime_sub(end, start));
pr_info("took %lld us\n", delta);
```
