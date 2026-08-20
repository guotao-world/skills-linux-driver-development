/*
 * GPIO 驱动模板
 * 功能：通过设备树获取 GPIO，实现 LED 控制和按键输入
 *
 * 配套设备树节点：
 *   my_gpio_dev {
 *       compatible = "vendor,my-gpio-dev";
 *       led-gpios = <&gpio1 3 GPIO_ACTIVE_HIGH>;
 *       button-gpios = <&gpio1 4 GPIO_ACTIVE_LOW>;
 *   };
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>

#define DRIVER_NAME "my-gpio-dev"

struct my_gpio_dev {
    struct gpio_desc *led_gpiod;
    struct gpio_desc *button_gpiod;
    int button_irq;
    struct device *dev;
};

static irqreturn_t button_irq_handler(int irq, void *dev_id)
{
    struct my_gpio_dev *mdev = dev_id;
    int val = gpiod_get_value(mdev->button_gpiod);

    dev_info(mdev->dev, "button pressed, val=%d\n", val);

    /* 翻转 LED */
    gpiod_set_value(mdev->led_gpiod, !gpiod_get_value(mdev->led_gpiod));

    return IRQ_HANDLED;
}

static int my_gpio_probe(struct platform_device *pdev)
{
    struct my_gpio_dev *mdev;
    int ret;

    mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
    if (!mdev)
        return -ENOMEM;
    mdev->dev = &pdev->dev;

    /* 获取 LED GPIO（输出，初始低电平） */
    mdev->led_gpiod = devm_gpiod_get(&pdev->dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(mdev->led_gpiod)) {
        ret = PTR_ERR(mdev->led_gpiod);
        dev_err(&pdev->dev, "failed to get led gpio: %d\n", ret);
        return ret;
    }

    /* 获取按键 GPIO（输入） */
    mdev->button_gpiod = devm_gpiod_get(&pdev->dev, "button", GPIOD_IN);
    if (IS_ERR(mdev->button_gpiod)) {
        ret = PTR_ERR(mdev->button_gpiod);
        dev_err(&pdev->dev, "failed to get button gpio: %d\n", ret);
        return ret;
    }

    /* 将按键 GPIO 转为中断号 */
    mdev->button_irq = gpiod_to_irq(mdev->button_gpiod);
    if (mdev->button_irq < 0) {
        dev_err(&pdev->dev, "gpiod_to_irq failed: %d\n", mdev->button_irq);
        return mdev->button_irq;
    }

    /* 注册中断（双边沿触发） */
    ret = devm_request_irq(&pdev->dev, mdev->button_irq,
                           button_irq_handler,
                           IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                           DRIVER_NAME, mdev);
    if (ret) {
        dev_err(&pdev->dev, "request_irq failed: %d\n", ret);
        return ret;
    }

    platform_set_drvdata(pdev, mdev);
    dev_info(&pdev->dev, "probed\n");
    return 0;
}

static int my_gpio_remove(struct platform_device *pdev)
{
    struct my_gpio_dev *mdev = platform_get_drvdata(pdev);

    /* 关闭 LED */
    gpiod_set_value(mdev->led_gpiod, 0);

    dev_info(&pdev->dev, "removed\n");
    return 0;
}

static const struct of_device_id my_gpio_of_match[] = {
    { .compatible = "vendor,my-gpio-dev", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_gpio_of_match);

static struct platform_driver my_gpio_driver = {
    .probe  = my_gpio_probe,
    .remove = my_gpio_remove,
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = my_gpio_of_match,
    },
};
module_platform_driver(my_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("GPIO driver template with LED and button");
