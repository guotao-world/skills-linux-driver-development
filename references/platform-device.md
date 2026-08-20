# 平台设备驱动 (Platform Driver)

## 目录

- [为什么需要平台驱动](#为什么需要平台驱动)
- [核心结构](#核心结构)
- [开发步骤](#开发步骤)
- [设备树匹配](#设备树匹配)
- [资源获取](#资源获取)
- [完整示例](#完整示例)

## 为什么需要平台驱动

SoC 内置外设（如 I2C 控制器、SPI 控制器、GPIO 控制器）不是通过 PCI/USB 等可枚举总线连接的，需要一种机制来描述和匹配这些"平台设备"。平台驱动框架将**设备信息**（资源、中断）和**驱动逻辑**分离。

现代内核中，平台设备通常由**设备树**自动实例化，驱动通过 `compatible` 字符串匹配。

## 核心结构

### platform_driver

```c
static struct platform_driver my_driver = {
    .probe      = my_probe,       // 设备匹配成功时调用
    .remove     = my_remove,      // 设备移除时调用
    .driver     = {
        .name   = "my-device",    // 用于传统名称匹配
        .of_match_table = my_of_match,  // 设备树匹配表
        .pm     = &my_pm_ops,     // 电源管理（可选）
    },
};
module_platform_driver(my_driver);  // 宏：自动注册+注销
```

### of_device_id

```c
static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-device-v1", },
    { .compatible = "vendor,my-device-v2", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_of_match);
```

## 开发步骤

1. 定义 `of_device_id` 匹配表
2. 实现 `probe` 函数：获取资源、初始化硬件、注册字符设备/其他接口
3. 实现 `remove` 函数：逆序清理
4. 注册 `platform_driver`

## 设备树匹配

驱动匹配优先级：

1. `of_match_table` 中的 `compatible`（设备树方式，最优先）
2. `acpi_match_table`（ACPI 方式，x86 常见）
3. `driver.name` 与 `platform_device.name` 字符串匹配（传统方式）

## 资源获取

在 probe 中从设备树或 platform_device 获取资源：

```c
// 获取内存映射资源（设备树）
void __iomem *base = devm_platform_ioremap_resource(pdev, 0);

// 获取中断号（设备树）
int irq = platform_get_irq(pdev, 0);

// 获取设备树属性
u32 val;
of_property_read_u32(np, "property-name", &val);
of_property_read_string(np, "label", &str);

// 获取 GPIO 描述符（推荐）
struct gpio_desc *gpiod = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
```

### devm_ 托管资源（推荐）

`devm_` 前缀的函数分配的资源会在设备 detach 时自动释放，无需手动清理，减少错误：

```c
devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
devm_ioremap(&pdev->dev, phys, size);
devm_request_irq(&pdev->dev, irq, handler, flags, name, dev);
```

## 完整示例

```c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>

struct my_dev {
    void __iomem *base;
    int irq;
    struct device *dev;
};

static irqreturn_t my_irq_handler(int irq, void *dev_id)
{
    struct my_dev *mdev = dev_id;
    // 处理中断
    return IRQ_HANDLED;
}

static int my_probe(struct platform_device *pdev)
{
    struct my_dev *mdev;
    int ret;

    mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
    if (!mdev) return -ENOMEM;
    mdev->dev = &pdev->dev;

    // 映射寄存器
    mdev->base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(mdev->base))
        return PTR_ERR(mdev->base);

    // 获取中断
    mdev->irq = platform_get_irq(pdev, 0);
    if (mdev->irq < 0)
        return mdev->irq;

    ret = devm_request_irq(&pdev->dev, mdev->irq, my_irq_handler,
                           IRQF_TRIGGER_RISING, "my_dev", mdev);
    if (ret)
        return ret;

    platform_set_drvdata(pdev, mdev);
    dev_info(&pdev->dev, "probed\n");
    return 0;
}

static int my_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "removed\n");
    return 0;
}

static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-device", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_of_match);

static struct platform_driver my_driver = {
    .probe  = my_probe,
    .remove = my_remove,
    .driver = {
        .name = "my-device",
        .of_match_table = my_of_match,
    },
};
module_platform_driver(my_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
```

## 设备树节点示例

```dts
my_device@02000000 {
    compatible = "vendor,my-device";
    reg = <0x02000000 0x1000>;      // 物理地址 + 长度
    interrupts = <GIC_SPI 25 IRQ_TYPE_LEVEL_HIGH>;
    label = "my-device";
};
```
