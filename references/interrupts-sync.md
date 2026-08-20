# 中断处理与内核同步

## 目录

- [中断处理](#中断处理)
- [顶半部与底半部](#顶半部与底半部)
- [工作队列 workqueue](#工作队列-workqueue)
- [软中断 tasklet](#软中断-tasklet)
- [内核同步机制](#内核同步机制)
- [原子操作](#原子操作)
- [等待队列](#等待队列)

## 中断处理

### 注册中断

```c
// 通用中断注册
int request_irq(unsigned int irq, irq_handler_t handler,
                unsigned long flags, const char *name, void *dev);

// 设备托管版本（自动释放）
int devm_request_irq(struct device *dev, unsigned int irq,
                     irq_handler_t handler, unsigned long irqflags,
                     const char *devname, void *dev_id);

// 释放
void free_irq(unsigned int irq, void *dev_id);
```

### 中断标志

| 标志 | 含义 |
|------|------|
| `IRQF_TRIGGER_RISING` | 上升沿触发 |
| `IRQF_TRIGGER_FALLING` | 下降沿触发 |
| `IRQF_TRIGGER_HIGH` | 高电平触发 |
| `IRQF_TRIGGER_LOW` | 低电平触发 |
| `IRQF_SHARED` | 共享中断（多个设备共用一条中断线） |
| `IRQF_ONESHOT` | 中断处理完后保持禁用，直到线程化处理完成 |

### 中断处理函数

```c
static irqreturn_t my_irq_handler(int irq, void *dev_id)
{
    // 判断是否是本设备的中断（共享中断时必须）
    if (!is_my_irq(dev_id))
        return IRQ_NONE;

    // 清除中断标志（必须，否则会反复触发）
    clear_irq(dev_id);

    // 快速处理顶半部：读取硬件状态、唤醒等待队列等
    // 不能睡眠！不能调用可能睡眠的函数！
    return IRQ_HANDLED;
}
```

返回值：

- `IRQ_HANDLED`：中断已处理
- `IRQ_NONE`：不是本设备的中断（共享中断）
- `IRQ_WAKE_THREAD`：唤醒线程化中断处理函数

### 线程化中断（ threaded irq ）

当中断处理需要睡眠操作（如 I2C/SPI 通信）时，使用线程化中断：

```c
// 顶半部（硬中断上下文，不能睡眠）
static irqreturn_t my_irq(int irq, void *dev_id)
{
    // 快速清除中断，唤醒线程
    return IRQ_WAKE_THREAD;
}

// 底半部（线程上下文，可以睡眠）
static irqreturn_t my_irq_thread(int irq, void *dev_id)
{
    // 可以调用 i2c_transfer、msleep 等可能睡眠的函数
    do_slow_work(dev_id);
    return IRQ_HANDLED;
}

// 注册
request_threaded_irq(irq, my_irq, my_irq_thread,
                     IRQF_TRIGGER_RISING, "my_dev", dev);
```

## 顶半部与底半部

| 机制 | 上下文 | 能否睡眠 | 适用场景 |
|------|--------|---------|---------|
| 硬中断 handler | 中断上下文 | 否 | 极快操作：清中断、存数据 |
| tasklet | 软中断上下文 | 否 | 中等耗时，不睡眠 |
| workqueue | 内核线程 | 是 | 耗时操作，可能睡眠 |
| threaded irq | 内核线程 | 是 | 中断处理需睡眠 |

## 工作队列 workqueue

```c
#include <linux/workqueue.h>

// 定义工作
static struct work_struct my_work;

// 工作处理函数（进程上下文，可以睡眠）
static void my_work_handler(struct work_struct *work)
{
    // 可以睡眠、可以调用可能阻塞的函数
    msleep(100);
}

// 初始化（在 probe/init 中）
INIT_WORK(&my_work, my_work_handler);

// 调度执行
schedule_work(&my_work);

// 延迟执行
static struct delayed_work my_dwork;
INIT_DELAYED_WORK(&my_dwork, my_dwork_handler);
schedule_delayed_work(&my_dwork, msecs_to_jiffies(100));

// 取消
cancel_work_sync(&my_work);
cancel_delayed_work_sync(&my_dwork);
```

## 软中断 tasklet

```c
#include <linux/interrupt.h>

static void my_tasklet(unsigned long data);
DECLARE_TASKLET(my_tasklet, my_tasklet, 0);

// 在中断中调度
tasklet_schedule(&my_tasklet);

// 清理
tasklet_kill(&my_tasklet);
```

注意：tasklet 运行在软中断上下文，**不能睡眠**。新代码优先使用 workqueue。

## 内核同步机制

### 互斥锁 mutex（进程上下文，可睡眠）

```c
#include <linux/mutex.h>

static DEFINE_MUTEX(my_mutex);

mutex_lock(&my_mutex);
// 临界区（可以睡眠）
mutex_unlock(&my_mutex);

// 尝试加锁（不阻塞）
if (mutex_trylock(&my_mutex)) {
    // ...
    mutex_unlock(&my_mutex);
}
```

### 自旋锁 spinlock（中断/进程上下文，不可睡眠）

```c
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(my_lock);
unsigned long flags;

// 普通使用（进程上下文）
spin_lock(&my_lock);
// 临界区（不能睡眠！）
spin_unlock(&my_lock);

// 中断安全版本（保存中断状态）
spin_lock_irqsave(&my_lock, flags);
// 临界区
spin_unlock_irqrestore(&my_lock, flags);
```

选择原则：

- 临界区小、不睡眠 → spinlock
- 临界区大、可能睡眠 → mutex
- 中断和进程共享数据 → spin_lock_irqsave

### 信号量 semaphore

```c
#include <linux/semaphore.h>

static DEFINE_SEMAPHORE(my_sem);  // 初始值1，等价于mutex

down(&my_sem);      // 获取（可睡眠）
up(&my_sem);        // 释放
```

### 读写锁

```c
// 读写自旋锁
rwlock_t my_rwlock;
read_lock(&my_rwlock);  read_unlock(&my_rwlock);
write_lock(&my_rwlock); write_unlock(&my_rwlock);

// 读写信号量（可睡眠）
struct rw_semaphore my_rwsem;
down_read(&my_rwsem);   up_read(&my_rwsem);
down_write(&my_rwsem);  up_write(&my_rwsem);
```

## 原子操作

```c
#include <linux/atomic.h>

atomic_t my_counter = ATOMIC_INIT(0);

atomic_inc(&my_counter);       // +1
atomic_dec(&my_counter);       // -1
atomic_add(5, &my_counter);    // +5
int val = atomic_read(&my_counter);
atomic_set(&my_counter, 0);

// 原子位操作
unsigned long flags = 0;
set_bit(0, &flags);     // 置位
clear_bit(0, &flags);   // 清位
test_bit(0, &flags);    // 测试
test_and_set_bit(0, &flags);  // 测试并置位
```

## 等待队列

用于进程等待某个事件发生：

```c
#include <linux/wait.h>

static DECLARE_WAIT_QUEUE_HEAD(my_wq);
static int data_ready = 0;

// 读操作中等待
ssize_t my_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    // 等待 data_ready 变为真（可中断）
    wait_event_interruptible(my_wq, data_ready != 0);

    // 有数据了，拷贝给用户
    // ...
    data_ready = 0;
    return count;
}

// 中断中唤醒
static irqreturn_t my_irq(int irq, void *dev_id)
{
    data_ready = 1;
    wake_up_interruptible(&my_wq);
    return IRQ_HANDLED;
}
```

`wait_event` 宏会循环检查条件，条件为假时进程睡眠，被唤醒后重新检查。
