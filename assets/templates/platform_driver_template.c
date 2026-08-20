/*
 * 平台设备驱动模板
 * 功能：基于设备树匹配的平台驱动框架，包含寄存器映射和中断
 *
 * 配套设备树节点：
 *   my_device@02000000 {
 *       compatible = "vendor,my-device";
 *       reg = <0x02000000 0x1000>;
 *       interrupts = <GIC_SPI 25 IRQ_TYPE_LEVEL_HIGH>;
 *   };
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>

#define DRIVER_NAME "my-device"

/* 设备私有数据 */
struct my_dev {
    void __iomem *base;
    int irq;
    struct device *dev;
    /* 在此添加其他私有数据 */
};

static irqreturn_t my_irq_handler(int irq, void *dev_id)
{
    struct my_dev *mdev = dev_id;

    /* TODO: 读取并清除中断状态寄存器 */
    /* u32 status = readl(mdev->base + REG_IRQ_STATUS);
       writel(status, mdev->base + REG_IRQ_CLEAR); */

    dev_dbg(mdev->dev, "irq handled\n");
    return IRQ_HANDLED;
}

static int my_probe(struct platform_device *pdev)
{
    struct my_dev *mdev;
    int ret;

    /* 分配私有数据（devm 托管，自动释放） */
    mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
    if (!mdev)
        return -ENOMEM;
    mdev->dev = &pdev->dev;

    /* 1. 映射寄存器资源 */
    mdev->base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(mdev->base)) {
        dev_err(&pdev->dev, "ioremap failed\n");
        return PTR_ERR(mdev->base);
    }

    /* 2. 获取中断号 */
    mdev->irq = platform_get_irq(pdev, 0);
    if (mdev->irq < 0)
        return mdev->irq;

    /* 3. 注册中断 */
    ret = devm_request_irq(&pdev->dev, mdev->irq, my_irq_handler,
                           IRQF_TRIGGER_RISING, DRIVER_NAME, mdev);
    if (ret) {
        dev_err(&pdev->dev, "request_irq failed: %d\n", ret);
        return ret;
    }

    /* 4. 硬件初始化（写寄存器等） */
    /* writel(0x01, mdev->base + REG_CTRL); */

    /* 5. 保存私有数据指针 */
    platform_set_drvdata(pdev, mdev);

    dev_info(&pdev->dev, "probed, base=%p, irq=%d\n",
             mdev->base, mdev->irq);
    return 0;
}

static int my_remove(struct platform_device *pdev)
{
    struct my_dev *mdev = platform_get_drvdata(pdev);

    /* 硬件关闭 */
    /* writel(0x00, mdev->base + REG_CTRL); */

    /* devm 托管的资源自动释放，无需手动清理 */
    dev_info(&pdev->dev, "removed\n");
    return 0;
}

/* 设备树匹配表 */
static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-device", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_of_match);

static struct platform_driver my_driver = {
    .probe  = my_probe,
    .remove = my_remove,
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = my_of_match,
    },
};
module_platform_driver(my_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Platform device driver template");
