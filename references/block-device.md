# 块设备驱动（Block Device Driver）

## 目录

- [概述](#概述)
- [核心数据结构](#核心数据结构)
- [两种驱动框架](#两种驱动框架)
- [blk-mq 框架详解](#blk-mq-框架详解)
- [block_device_operations](#block_device_operations)
- [注册与注销流程](#注册与注销流程)
- [I/O 请求生命周期](#io-请求生命周期)
- [常用 API 速查](#常用-api-速查)
- [内核版本差异](#内核版本差异)
- [调试技巧](#调试技巧)
- [平台注意事项](#平台注意事项)

## 概述

块设备是 Linux 三大设备类型之一（字符设备、块设备、网络设备），以**固定大小的块**（通常 512 字节）为单位随机访问。典型块设备：硬盘、U盘、SD卡、eMMC、NVMe、ramdisk。

### 块设备 vs 字符设备

| 特性 | 字符设备 | 块设备 |
|------|---------|--------|
| 访问单位 | 字节流 | 块（512B / 4KB） |
| 访问方式 | 顺序 | 随机 |
| 缓存 | 无 | 页缓存（page cache） |
| 注册 | `cdev_add` | `register_blkdev` + `add_disk` |
| 操作结构 | `file_operations` | `block_device_operations` |
| I/O 路径 | 直接 `read/write` | 经块层调度 → `request_fn` / `queue_rq` |
| 典型设备 | 串口、LED、按键 | 磁盘、SD卡、ramdisk |

### 块设备在 I/O 栈中的位置

```
用户态 read()/write()
       │
       ▼
  VFS (虚拟文件系统)
       │
       ▼
  文件系统 (ext4/fat/...)
       │
       ▼
  页缓存 (page cache)
       │
       ▼
  块层 (block layer) ── 请求合并、调度、重试
       │
       ▼
  块设备驱动 (request_fn / queue_rq)
       │
       ▼
  硬件 (磁盘控制器 / 内存)
```

## 核心数据结构

### struct gendisk

代表内核中的一个块设备（一个磁盘），对应 `/dev/xxx` 设备节点。

```c
struct gendisk {
    int major;                    /* 主设备号 */
    int first_minor;              /* 起始次设备号 */
    int minors;                   /* 次设备号数量（= 支持的分区数 + 1） */
    char disk_name[DISK_NAME_LEN];/* 设备名，如 "sda"、"ramdisk0" */
    struct block_device_operations *fops;  /* 操作函数表 */
    struct request_queue *queue;  /* 请求队列 */
    void *private_data;           /* 驱动私有数据 */
    sector_t capacity;            /* 容量（扇区数） */
    unsigned int flags;           /* 标志，如 GENHD_FL_NO_PART */
    ...
};
```

关键操作：

- `alloc_disk(minors)` — 分配 gendisk
- `add_disk(disk)` — 注册到内核（创建设备节点）
- `del_gendisk(disk)` — 注销
- `put_disk(disk)` — 释放引用
- `set_capacity(disk, sectors)` — 设置容量

### struct request_queue

块设备的请求队列，所有 I/O 请求都经过这里。blk-mq 框架下由 `blk_mq_init_queue` 创建。

```c
struct request_queue {
    struct blk_mq_tag_set *tag_set;  /* blk-mq 标签集 */
    void *queuedata;                 /* 驱动私有数据（常用） */
    unsigned int logical_block_size; /* 逻辑块大小 */
    unsigned int physical_block_size;/* 物理块大小 */
    ...
};
```

`queue->queuedata` 是驱动传递私有数据的常用方式，在 `queue_rq` 中通过 `req->q->queuedata` 取回。

### struct bio

块 I/O 请求的基本单位，描述一次连续的块传输。一个 `request` 可能包含多个 `bio`。

```c
struct bio {
    struct bio_vec *bi_io_vec;   /* 段数组（页 + 偏移 + 长度） */
    sector_t bi_iter.bi_sector;  /* 起始扇区号 */
    unsigned int bi_iter.bi_size;/* 总字节数 */
    unsigned short bi_vcnt;      /* 段数量 */
    struct block_device *bi_bdev;/* 目标块设备 */
    ...
};
```

### struct request

块层经过合并/调度后产生的请求，可能包含多个 `bio`。是 `queue_rq` / `request_fn` 的处理对象。

```c
struct request {
    struct request_queue *q;     /* 所属队列 */
    struct gendisk *rq_disk;     /* 目标磁盘 */
    unsigned int __data_len;     /* 剩余数据长度 */
    sector_t __sector;           /* 当前扇区位置 */
    struct bio *bio;             /* 第一个 bio */
    struct bio *biotail;         /* 最后一个 bio */
    ...
};
```

常用辅助宏：

- `blk_rq_pos(req)` — 当前扇区号
- `blk_rq_cur_bytes(req)` — 当前要传输的字节数
- `req_op(req)` — 请求操作类型（读/写/丢弃等）
- `op_is_write(op)` — 判断是否写操作
- `rq_for_each_segment(bvec, req, iter)` — 遍历请求中所有段

### struct bio_vec

描述一个数据段：一页中的连续数据区域。

```c
struct bio_vec {
    struct page *bv_page;   /* 物理页 */
    unsigned int bv_len;    /* 数据长度 */
    unsigned int bv_offset; /* 页内偏移 */
};
```

## 两种驱动框架

### 1. 传统单队列（request_fn）— 已不推荐

```c
static void my_request(struct request_queue *q)
{
    struct request *req;
    while ((req = blk_fetch_request(q)) != NULL) {
        // 处理一个请求...
        __blk_end_request_all(req, BLK_STS_OK);
    }
}

// 初始化
rd->queue = blk_init_queue(my_request, &rd->lock);
```

特点：单队列、全局自旋锁、多核扩展性差。新驱动不应使用。

### 2. 多队列 blk-mq — 当前标准

```c
static blk_status_t my_queue_rq(struct blk_mq_hw_ctx *hctx,
                                const struct blk_mq_queue_data *bd)
{
    struct request *req = bd->rq;
    // 处理一个请求...
    blk_mq_start_request(req);
    // ... 数据传输 ...
    __blk_mq_end_request(req, BLK_STS_OK);
    return BLK_STS_OK;
}

static const struct blk_mq_ops my_mq_ops = {
    .queue_rq = my_queue_rq,
};

// 初始化
blk_mq_alloc_tag_set(&tag_set);
rd->queue = blk_mq_init_queue(&tag_set);
```

特点：per-CPU 软件队列、多硬件队列、无全局锁、多核高效。**所有新驱动应使用 blk-mq。**

## blk-mq 框架详解

### struct blk_mq_tag_set

管理请求标签（tag）的预分配池，是 blk-mq 的核心配置结构。

```c
struct blk_mq_tag_set {
    const struct blk_mq_ops *ops;   /* 操作回调表 */
    unsigned int nr_hw_queues;      /* 硬件队列数 */
    unsigned int queue_depth;       /* 每队列最大请求数 */
    unsigned int reserved_tags;     /* 保留标签数 */
    unsigned int cmd_size;          /* 每个请求的私有数据大小 */
    int numa_node;                  /* NUMA 节点 */
    unsigned int flags;             /* 标志 */
    void *driver_data;              /* 驱动私有数据 */
    ...
};
```

常用 flags：

- `BLK_MQ_F_SHOULD_MERGE` — 允许块层合并相邻请求
- `BLK_MQ_F_BLOCKING` — queue_rq 可能阻塞（支持睡眠）
- `BLK_MQ_F_NO_SCHED` — 不使用 I/O 调度器

### struct blk_mq_ops

blk-mq 操作回调表，最核心的是 `queue_rq`。

```c
struct blk_mq_ops {
    blk_status_t (*queue_rq)(struct blk_mq_hw_ctx *hctx,
                             const struct blk_mq_queue_data *bd);
    void (*complete)(struct request *req);
    int (*init_request)(struct blk_mq_tag_set *set, struct request *req,
                        unsigned int hctx_idx, unsigned int numa_node);
    void (*exit_request)(struct blk_mq_tag_set *set, struct request *req,
                         unsigned int hctx_idx);
    ...
};
```

- `queue_rq` — **必须实现**，处理一个请求
- `complete` — 请求完成回调（可选）
- `init_request` / `exit_request` — 请求对象初始化/销毁（可选，配合 `cmd_size` 使用）

### queue_rq 处理流程模板

```c
static blk_status_t my_queue_rq(struct blk_mq_hw_ctx *hctx,
                                const struct blk_mq_queue_data *bd)
{
    struct request *req = bd->rq;
    struct my_dev *dev = req->q->queuedata;
    loff_t pos = blk_rq_pos(req) << SECTOR_SHIFT;
    blk_status_t status = BLK_STS_OK;

    // 1. 边界检查
    if (pos + blk_rq_cur_bytes(req) > dev->size)
        return BLK_STS_IOERR;

    // 2. 通知块层开始处理
    blk_mq_start_request(req);

    // 3. 遍历所有 bio 段，执行实际数据传输
    struct bio_vec bvec;
    struct req_iterator iter;
    rq_for_each_segment(bvec, req, iter) {
        void *buf = page_address(bvec.bv_page) + bvec.bv_offset;
        unsigned int len = bvec.bv_len;

        if (op_is_write(req_op(req))) {
            // 写：buf -> 设备
            memcpy(dev->data + pos, buf, len);
        } else {
            // 读：设备 -> buf
            memcpy(buf, dev->data + pos, len);
        }
        pos += len;
    }

    // 4. 完成请求
    if (blk_update_request(req, status, blk_rq_cur_bytes(req)))
        BUG_ON(1);
    __blk_mq_end_request(req, status);
    return BLK_STS_OK;
}
```

## block_device_operations

块设备的文件操作表，类似字符设备的 `file_operations`，但只包含块设备特有的操作。

```c
struct block_device_operations {
    int (*open)(struct gendisk *disk, fmode_t mode);
    void (*release)(struct gendisk *disk);
    int (*ioctl)(struct block_device *bdev, fmode_t mode,
                 unsigned int cmd, unsigned long arg);
    int (*getgeo)(struct block_device *bdev, struct hd_geometry *geo);
    struct module *owner;
    ...
};
```

- `open` / `release` — 设备打开/关闭
- `ioctl` — 设备特定控制命令（如 `HDIO_GETGEO` 获取磁盘几何参数）
- `getgeo` — 获取磁盘几何参数（老接口，ioctl 中实现 HDIO_GETGEO 即可）
- `owner` — 必须设为 `THIS_MODULE`

> 注意：块设备的 `read`/`write` 不在这里实现，而是通过块层的 `queue_rq` / `request_fn` 处理。

## 注册与注销流程

### 注册（初始化）

```
1. register_blkdev(0, "name")        → 动态分配主设备号
2. vzalloc/kmalloc(size)             → 分配设备存储（如 ramdisk）
3. blk_mq_alloc_tag_set(&tag_set)    → 分配 blk-mq 标签集
4. blk_mq_init_queue(&tag_set)       → 创建请求队列
5. queue->queuedata = dev            → 保存私有数据
6. blk_queue_logical_block_size()    → 设置块大小
7. alloc_disk(minors)                → 分配 gendisk
8. disk->queue = queue               → 绑定队列（4.19 及以前）
9. disk->major/fops/private_data...  → 配置 gendisk
10. set_capacity(disk, sectors)      → 设置容量
11. add_disk(disk)                   → 注册到内核
```

### 注销（退出）

```
1. del_gendisk(disk)      → 从内核注销
2. put_disk(disk)         → 释放 gendisk
3. blk_cleanup_queue(queue) → 清理请求队列
4. blk_mq_free_tag_set(&tag_set) → 释放标签集
5. vfree/kfree(data)      → 释放设备存储
6. unregister_blkdev(major, "name") → 释放主设备号
```

## I/O 请求生命周期

```
用户态 read()/write()
      │
      ▼
VFS → 文件系统 → 页缓存
      │
      ▼
块层提交 bio (submit_bio)
      │
      ▼
blk-mq 软件队列 (per-CPU)
      │  请求合并、调度
      ▼
blk-mq 硬件队列 (hctx)
      │
      ▼
驱动 queue_rq() 回调
      │
      ├─ 读：设备内存 → 页缓存
      └─ 写：页缓存 → 设备内存
      │
      ▼
__blk_mq_end_request()  → 通知块层完成
      │
      ▼
bio 完成回调 → 唤醒等待的进程
```

## 常用 API 速查

```c
/* 注册/注销 */
register_blkdev(major, name);       /* 注册块设备，返回主设备号 */
unregister_blkdev(major, name);     /* 注销 */

/* gendisk */
alloc_disk(minors);                 /* 分配 gendisk */
add_disk(disk);                     /* 注册（4.19 返回 void，5.13+ 返回 int） */
del_gendisk(disk);                  /* 注销 */
put_disk(disk);                     /* 释放引用 */
set_capacity(disk, sectors);        /* 设置容量（扇区数） */
get_capacity(disk);                 /* 获取容量 */

/* blk-mq */
blk_mq_alloc_tag_set(&tag_set);     /* 分配标签集 */
blk_mq_free_tag_set(&tag_set);      /* 释放标签集 */
blk_mq_init_queue(&tag_set);        /* 创建请求队列 */
blk_mq_alloc_disk(&tag_set, data);  /* 5.14+ 一步分配 disk+queue */

/* 队列 */
blk_cleanup_queue(queue);           /* 清理队列 */
blk_queue_logical_block_size(q, sz);/* 设置逻辑块大小 */
blk_queue_physical_block_size(q,sz);/* 设置物理块大小 */

/* 请求处理 */
blk_mq_start_request(req);          /* 开始处理请求 */
blk_update_request(req, err, bytes);/* 更新请求进度 */
__blk_mq_end_request(req, err);     /* 结束请求 */
blk_rq_pos(req);                    /* 当前扇区号 */
blk_rq_cur_bytes(req);              /* 当前字节数 */
req_op(req);                        /* 请求操作类型 */
op_is_write(op);                    /* 是否写操作 */

/* 遍历 */
rq_for_each_segment(bvec, req, iter);  /* 遍历所有 bio 段 */
bio_for_each_segment(bvec, bio, iter); /* 遍历单个 bio 的段 */

/* 状态码 */
BLK_STS_OK;      /* 成功 */
BLK_STS_IOERR;   /* I/O 错误 */
BLK_STS_TIMEOUT; /* 超时 */
BLK_STS_NOSPC;   /* 空间不足 */
```

## 内核版本差异

| API / 特性 | 4.19（及以前） | 5.x+ |
|-----------|---------------|------|
| 分配 gendisk | `alloc_disk()` + `disk->queue = queue` | `blk_mq_alloc_disk()`（5.14+） |
| open/release 参数 | `fmode_t` | `blk_mode_t`（5.10+） |
| `add_disk` 返回值 | `void` | `int`（5.13+） |
| 禁止分区 | `minors = 1` | `GENHD_FL_NO_PART`（5.15+） |
| `register_blkdev` | 正常使用 | 标记为 deprecated（但仍可用） |
| `blk_mq_ops.complete` | 可用 | 5.18+ 移除 |
| ioctl 签名 | `struct block_device *bdev, fmode_t mode` | 5.10+ 用 `blk_mode_t` |

> 编写跨版本驱动时，用 `LINUX_VERSION_CODE` + `KERNEL_VERSION()` 做条件编译。

## 调试技巧

### 查看设备信息

```bash
lsblk                                # 块设备列表
cat /proc/devices                    # 已注册的设备号
cat /sys/block/<dev>/size             # 容量（扇区数）
cat /sys/block/<dev>/stat             # I/O 统计
blockdev --getsize64 /dev/<dev>      # 总字节数
blockdev --getss /dev/<dev>          # 扇区大小
```

### 跟踪 I/O

```bash
# blktrace（需 CONFIG_BLK_DEV_IO_TRACE）
blktrace -d /dev/<dev> -o - | blkparse -i -

# 查看请求队列信息
cat /sys/block/<dev>/queue/scheduler
cat /sys/block/<dev>/queue/nr_requests
cat /sys/block/<dev>/queue/logical_block_size
```

### printk 调试

在 `queue_rq` 中打印关键信息：

```c
pr_info("queue_rq: op=%s sector=%llu bytes=%u\n",
        op_is_write(req_op(req)) ? "WRITE" : "READ",
        (unsigned long long)blk_rq_pos(req),
        blk_rq_cur_bytes(req));
```

### 常见问题

| 现象 | 可能原因 | 解决 |
|------|---------|------|
| insmod 后 /dev 无节点 | add_disk 失败或 udev 未运行 | 检查 dmesg；手动 mknod |
| mount 失败 wrong fs type | 未格式化 | 先 mkfs.ext4 |
| 读写越界 oops | 边界检查缺失 | 在 queue_rq 中检查 pos + len <= size |
| 并发数据损坏 | 未加锁 | 用 spinlock 保护共享数据 |
| rmmod 提示 in use | 设备仍挂载 | umount 后再 rmmod |
| queue_rq 中睡眠死锁 | 用了 mutex/sleep | queue_rq 在软中断上下文，用 spinlock |

## 平台注意事项

### RK3568 (ARM64)

- 架构：ARM64，交叉编译工具链 `aarch64-linux-gnu-`
- 内核：厂商 BSP 通常基于 4.19 或 5.10
- 存储外设：eMMC、SDMMC、SATA、SPI NAND/NOR，均有现成驱动
- 自研块设备（如 ramdisk）不涉及具体硬件接口，通用即可
- 编译命令：
  ```bash
  make -C /path/to/kernel M=$(pwd) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
  ```

### 通用注意

- ramdisk 用 `vzalloc` 分配内存（不需要物理连续）
- 真实硬件块设备（如 SD 卡控制器）需要 DMA，用 `dma_alloc_coherent` 或 `dma_map_sg`
- `queue_rq` 运行在软中断上下文，**不能睡眠**，用 `spin_lock_irqsave`
- 大内存分配优先 `vzalloc`，小内存且需 DMA 时用 `kmalloc`（GFP_KERNEL）或 `dma_alloc_coherent`
