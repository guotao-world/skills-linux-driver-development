# GPIO 子系统

## 目录

- [两种 API](#两种-api)
- [传统 GPIO 编号 API](#传统-gpio-编号-api)
- [GPIO 描述符 API（推荐）](#gpio-描述符-api推荐)
- [设备树绑定](#设备树绑定)
- [中断 GPIO](#中断-gpio)
- [sysfs 用户态控制](#sysfs-用户态控制)

## 两种 API

| API | 函数前缀 | 特点 |
|-----|---------|------|
| 传统编号 API | `gpio_xxx` | 用整数编号，简单但不直观，已不推荐新代码使用 |
| 描述符 API | `gpiod_xxx` | 用 `struct gpio_desc *`，支持设备树命名，推荐 |

## 传统 GPIO 编号 API

```c
#include <linux/gpio.h>

// 请求 GPIO
gpio_request(gpio_num, "my_gpio");

// 设置方向
gpio_direction_input(gpio_num);
gpio_direction_output(gpio_num, initial_value);

// 读写
int val = gpio_get_value(gpio_num);
gpio_set_value(gpio_num, val);

// 释放
gpio_free(gpio_num);
```

## GPIO 描述符 API（推荐）

```c
#include <linux/gpio/consumer.h>

// 获取 GPIO（设备树中通过 -gpios 属性关联）
struct gpio_desc *gpiod = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
// con_id "reset" 对应设备树中的 "reset-gpios" 属性

// 批量获取多个 GPIO
struct gpio_descs *gpios = devm_gpiod_get_array(dev, "leds", GPIOD_OUT_LOW);

// 方向设置（获取时已指定，也可运行时改变）
gpiod_direction_input(gpiod);
gpiod_direction_output(gpiod, value);

// 读写
int val = gpiod_get_value(gpiod);
gpiod_set_value(gpiod, val);

// 带消费计数的开关（适合电源/复位等）
gpiod_set_value_cansleep(gpiod, 1);
```

### 获取标志

| 标志 | 含义 |
|------|------|
| `GPIOD_ASIS` | 不改变当前状态 |
| `GPIOD_IN` | 输入 |
| `GPIOD_OUT_LOW` | 输出，初始低电平 |
| `GPIOD_OUT_HIGH` | 输出，初始高电平 |
| `GPIOD_IN` + `GPIOD_ACTIVE_LOW` | 输入，低电平有效 |

## 设备树绑定

```dts
my_leds {
    compatible = "vendor,my-leds";
    led-gpios = <&gpio1 3 GPIO_ACTIVE_HIGH>,   // GPIO1_IO3
                <&gpio2 5 GPIO_ACTIVE_LOW>;    // GPIO2_IO5, 低有效
    power-gpios = <&gpio3 0 GPIO_ACTIVE_HIGH>;
};
```

驱动中：

```c
// "led" 对应 "led-gpios"，自动获取第一个
struct gpio_desc *led0 = devm_gpiod_get_index(dev, "led", 0, GPIOD_OUT_LOW);
struct gpio_desc *power = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
```

## 中断 GPIO

GPIO 引脚可作为中断源：

```c
// 方法1: 获取 GPIO 对应的 IRQ 号
int irq = gpiod_to_irq(gpiod);
request_irq(irq, handler, IRQF_TRIGGER_RISING, "my_irq", dev);

// 方法2: 直接从设备树获取中断（推荐）
int irq = platform_get_irq(pdev, 0);
```

设备树中：

```dts
my_button {
    compatible = "vendor,my-button";
    interrupt-parent = <&gpio1>;
    interrupts = <4 IRQ_TYPE_EDGE_BOTH>;  // GPIO1_IO4, 双边沿
};
```

## sysfs 用户态控制

```bash
# 导出 GPIO
echo 3 > /sys/class/gpio/export

# 设置方向
echo out > /sys/class/gpio/gpio3/direction
echo in  > /sys/class/gpio/gpio3/direction

# 读写值
echo 1 > /sys/class/gpio/gpio3/value
cat /sys/class/gpio/gpio3/value

# 取消导出
echo 3 > /sys/class/gpio/unexport
```

注意：新内核推荐使用 `gpiochip` 字符设备 + `libgpiod` 库替代 sysfs。

## RK3568 GPIO 编号计算

RK3568 有 5 组 GPIO（GPIO0~GPIO4），每组 32 个引脚，每组内分 A/B/C/D 四个 port（各 8 个）：

```
gpio_number = bank * 32 + port_offset + pin
// port_offset: A=0, B=8, C=16, D=24
// GPIO0_A0 = 0*32 + 0  + 0 = 0
// GPIO3_A5 = 3*32 + 0  + 5 = 101
// GPIO4_D7 = 4*32 + 24 + 7 = 159
```

设备树中用 `RK_PA0` ~ `RK_PD7` 宏引用引脚，例如 `<&gpio3 RK_PA5 GPIO_ACTIVE_HIGH>`。

详细平台信息见 `platform-rk3568.md`。
