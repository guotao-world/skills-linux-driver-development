/*
 * block_device_template.c - 块设备驱动模板（blk-mq 框架）
 *
 * 本模板以 ramdisk（内存模拟块设备）为默认实现，展示块设备驱动的
 * 完整结构。开发真实硬件块设备驱动时，在标注 TODO 的位置替换为
 * 实际的硬件读写操作即可。
 *
 * 目标内核：Linux 4.19（RK3568 / IMX6 ULL 等嵌入式平台常用）
 * 架构：任意（ARM/ARM64/x86）
 *
 * 编译：
 *   make -C /path/to/kernel M=$(pwd) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
 *
 * 加载：
 *   insmod block_device_template.ko rd_size=8192
 *
 * 使用：
 *   mkfs.ext4 /dev/myblk0
 *   mount /dev/myblk0 /mnt
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

/* ============================================================
 * 模块参数
 * ============================================================ */
static unsigned int rd_size = 4096;   /* 设备大小，单位 KB，默认 4MB */
module_param(rd_size, uint, 0444);
MODULE_PARM_DESC(rd_size, "Device size in KB (default 4096)");

#define MYBLK_NAME     "myblk"
#define MYBLK_MINORS   1               /* 设为 1 表示不支持分区 */

/* ============================================================
 * 私有设备结构体
 *
 * TODO: 根据实际硬件添加寄存器地址、时钟、DMA 描述符等字段
 * ============================================================ */
struct myblk_dev {
    unsigned long         size;       /* 设备总大小（字节） */
    u8                   *data;       /* backing store（ramdisk 用 vzalloc） */
    spinlock_t            lock;       /* 并发保护锁 */
    struct gendisk       *disk;       /* 通用磁盘结构 */
    struct request_queue *queue;      /* blk-mq 请求队列 */
    struct blk_mq_tag_set tag_set;    /* blk-mq 标签集 */
    /* TODO: 硬件相关字段，例如：
     * void __iomem *iobase;
     * struct clk *clk;
     * struct dma_chan *dma;
     */
};

static struct myblk_dev *myblks;
static int myblk_major;

/* ============================================================
 * 核心回调：处理单个 I/O 请求
 *
 * 这是块设备驱动的核心。blk-mq 框架为每个请求调用此函数。
 *
 * TODO: 开发真实硬件驱动时，将 rq_for_each_segment 循环中的
 *       memcpy 替换为实际的硬件读写操作（如写寄存器、启动 DMA 等）。
 *
 * 注意：此函数运行在软中断上下文，不能睡眠！
 *       - 用 spin_lock_irqsave，不能用 mutex_lock
 *       - 用 kmalloc(GFP_ATOMIC)，不能用 kmalloc(GFP_KERNEL)
 * ============================================================ */
static blk_status_t myblk_queue_rq(struct blk_mq_hw_ctx *hctx,
                                   const struct blk_mq_queue_data *bd)
{
    struct request *req = bd->rq;
    struct myblk_dev *dev = req->q->queuedata;
    loff_t pos = blk_rq_pos(req) << SECTOR_SHIFT;  /* 起始字节偏移 */
    unsigned int nr_bytes = blk_rq_cur_bytes(req);
    blk_status_t status = BLK_STS_OK;
    unsigned long flags;

    /* ---- 边界检查 ---- */
    if (unlikely(pos + nr_bytes > dev->size)) {
        dev_err(dev->disk->disk_name,
                "I/O out of range: pos=%lld bytes=%u size=%lu\n",
                pos, nr_bytes, dev->size);
        return BLK_STS_IOERR;
    }

    /* 通知块层开始处理请求 */
    blk_mq_start_request(req);

    /* ---- 遍历请求中的每个 bio 段，执行数据传输 ---- */
    {
        struct bio_vec bvec;
        struct req_iterator iter;
        rq_for_each_segment(bvec, req, iter) {
            unsigned int len = bvec.bv_len;
            void *buf = page_address(bvec.bv_page) + bvec.bv_offset;

            spin_lock_irqsave(&dev->lock, flags);
            if (op_is_write(req_op(req))) {
                /* TODO: 写操作 — 替换为实际硬件写
                 * 例如：写寄存器、启动 DMA 从 buf 传输到设备
                 */
                memcpy(dev->data + pos, buf, len);  /* ramdisk 默认实现 */
            } else {
                /* TODO: 读操作 — 替换为实际硬件读
                 * 例如：启动 DMA 从设备传输到 buf、读 FIFO 等
                 */
                memcpy(buf, dev->data + pos, len);  /* ramdisk 默认实现 */
            }
            spin_unlock_irqrestore(&dev->lock, flags);
            pos += len;
        }
    }

    /* ---- 完成请求 ---- */
    if (blk_update_request(req, status, nr_bytes))
        BUG_ON(1);
    __blk_mq_end_request(req, status);
    return BLK_STS_OK;
}

/* ============================================================
 * blk-mq 操作表
 * ============================================================ */
static const struct blk_mq_ops myblk_mq_ops = {
    .queue_rq = myblk_queue_rq,
    /* TODO: 如需 per-request 私有数据，设置 cmd_size 并实现：
     * .init_request = myblk_init_request,
     * .exit_request = myblk_exit_request,
     */
};

/* ============================================================
 * block_device_operations
 *
 * 4.19 中 open/release/ioctl 的模式参数类型是 fmode_t
 * （5.10+ 改为 blk_mode_t）。
 * ============================================================ */
static int myblk_open(struct gendisk *disk, fmode_t mode)
{
    struct myblk_dev *dev = disk->private_data;
    /* TODO: 硬件初始化（如上电、复位）放在这里 */
    dev_info(dev->disk->disk_name, "device opened\n");
    return 0;
}

static void myblk_release(struct gendisk *disk)
{
    struct myblk_dev *dev = disk->private_data;
    /* TODO: 硬件去初始化（如下电）放在这里 */
    dev_info(dev->disk->disk_name, "device released\n");
}

/*
 * ioctl：实现 HDIO_GETGEO，让 fdisk 等工具获取磁盘几何参数。
 * TODO: 如需自定义 ioctl 命令，在此 switch 中添加 case。
 */
static int myblk_ioctl(struct block_device *bdev, fmode_t mode,
                       unsigned int cmd, unsigned long arg)
{
    struct myblk_dev *dev = bdev->bd_disk->private_data;
    switch (cmd) {
    case HDIO_GETGEO: {
        struct hd_geometry geo;
        unsigned long sectors = dev->size >> SECTOR_SHIFT;
        geo.heads     = 255;
        geo.sectors   = 63;
        geo.cylinders = sectors / (255 * 63);
        geo.start     = 0;
        if (copy_to_user((void __user *)arg, &geo, sizeof(geo)))
            return -EFAULT;
        return 0;
    }
    /* TODO: 添加自定义 ioctl 命令 */
    default:
        return -ENOTTY;
    }
}

static const struct block_device_operations myblk_fops = {
    .owner   = THIS_MODULE,
    .open    = myblk_open,
    .release = myblk_release,
    .ioctl   = myblk_ioctl,
};

/* ============================================================
 * 设备初始化
 * ============================================================ */
static int __init myblk_alloc_dev(struct myblk_dev *dev, int index)
{
    int ret;
    dev->size = (unsigned long)rd_size * 1024;
    spin_lock_init(&dev->lock);

    /* ---- 1. 分配设备存储 ----
     * TODO: 真实硬件不需要分配内存，改为映射寄存器、初始化硬件等。
     *       ramdisk 用 vzalloc 因为不需要物理连续。
     */
    dev->data = vzalloc(dev->size);
    if (!dev->data) {
        pr_err("myblk: failed to vmalloc %lu bytes\n", dev->size);
        return -ENOMEM;
    }

    /* ---- 2. 配置 blk-mq 标签集 ---- */
    dev->tag_set.ops          = &myblk_mq_ops;
    dev->tag_set.nr_hw_queues = 1;     /* TODO: 多队列硬件设为实际硬件队列数 */
    dev->tag_set.queue_depth  = 128;   /* 每队列最大并发请求数 */
    dev->tag_set.numa_node    = NUMA_NO_NODE;
    dev->tag_set.flags        = BLK_MQ_F_SHOULD_MERGE;
    dev->tag_set.cmd_size     = 0;     /* TODO: 如需 per-request 私有数据，设置大小 */
    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret) {
        pr_err("myblk: blk_mq_alloc_tag_set failed: %d\n", ret);
        goto err_vfree;
    }

    /* ---- 3. 创建请求队列 ---- */
    dev->queue = blk_mq_init_queue(&dev->tag_set);
    if (IS_ERR(dev->queue)) {
        ret = PTR_ERR(dev->queue);
        pr_err("myblk: blk_mq_init_queue failed: %d\n", ret);
        goto err_tag_set;
    }
    dev->queue->queuedata = dev;  /* 保存私有数据，queue_rq 中取回 */

    /* 设置块大小 */
    blk_queue_logical_block_size(dev->queue, 512);
    blk_queue_physical_block_size(dev->queue, 512);

    /* TODO: 如硬件支持 DMA，设置 DMA 掩码和队列属性：
     * blk_queue_max_hw_sectors(dev->queue, 1024);
     * blk_queue_dma_alignment(dev->queue, 511);
     * dma_set_mask_and_coherent(dev->dev, DMA_BIT_MASK(32));
     */

    /* ---- 4. 分配 gendisk ---- */
    dev->disk = alloc_disk(MYBLK_MINORS);
    if (!dev->disk) {
        ret = -ENOMEM;
        pr_err("myblk: alloc_disk failed\n");
        goto err_queue;
    }
    dev->disk->queue        = dev->queue;  /* 绑定队列（4.19 手动绑定） */
    dev->disk->major        = myblk_major;
    dev->disk->first_minor  = index * MYBLK_MINORS;
    dev->disk->minors       = MYBLK_MINORS;
    dev->disk->fops         = &myblk_fops;
    dev->disk->private_data = dev;
    set_capacity(dev->disk, dev->size >> SECTOR_SHIFT);
    snprintf(dev->disk->disk_name, DISK_NAME_LEN, "myblk%d", index);

    /* ---- 5. 注册磁盘 ---- */
    add_disk(dev->disk);  /* 4.19 返回 void；5.13+ 返回 int */
    pr_info("myblk%d: allocated %u KB (%lu sectors)\n",
            index, rd_size, dev->size >> SECTOR_SHIFT);
    return 0;

err_queue:
    blk_cleanup_queue(dev->queue);
err_tag_set:
    blk_mq_free_tag_set(&dev->tag_set);
err_vfree:
    vfree(dev->data);
    return ret;
}

/* ============================================================
 * 设备销毁
 * ============================================================ */
static void myblk_free_dev(struct myblk_dev *dev)
{
    if (dev->disk) {
        del_gendisk(dev->disk);
        put_disk(dev->disk);
    }
    if (dev->queue)
        blk_cleanup_queue(dev->queue);
    blk_mq_free_tag_set(&dev->tag_set);
    if (dev->data)
        vfree(dev->data);
    /* TODO: 释放硬件资源（取消映射、关闭时钟等） */
}

/* ============================================================
 * 模块初始化
 * ============================================================ */
static int __init myblk_init(void)
{
    int ret;

    /* 注册块设备，动态分配主设备号 */
    myblk_major = register_blkdev(0, MYBLK_NAME);
    if (myblk_major < 0) {
        pr_err("myblk: register_blkdev failed: %d\n", myblk_major);
        return myblk_major;
    }

    /* 本模板只创建 1 个设备，多设备可参考 ramdisk 示例用循环 */
    myblks = kzalloc(sizeof(struct myblk_dev), GFP_KERNEL);
    if (!myblks) {
        ret = -ENOMEM;
        goto err_unregister;
    }

    ret = myblk_alloc_dev(myblks, 0);
    if (ret)
        goto err_free;

    pr_info("myblk: loaded, major=%d\n", myblk_major);
    return 0;

err_free:
    kfree(myblks);
err_unregister:
    unregister_blkdev(myblk_major, MYBLK_NAME);
    return ret;
}

/* ============================================================
 * 模块退出
 * ============================================================ */
static void __exit myblk_exit(void)
{
    myblk_free_dev(myblks);
    kfree(myblks);
    unregister_blkdev(myblk_major, MYBLK_NAME);
    pr_info("myblk: unloaded\n");
}

module_init(myblk_init);
module_exit(myblk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Driver Learning");
MODULE_DESCRIPTION("Block device driver template (blk-mq, for 4.19)");
MODULE_VERSION("1.0");
