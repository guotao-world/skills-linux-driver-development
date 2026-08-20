# 设备树 (Device Tree)

## 目录

- [设备树基础](#设备树基础)
- [语法规则](#语法规则)
- [常用属性](#常用属性)
- [地址与中断](#地址与中断)
- [驱动中解析设备树](#驱动中解析设备树)
- [RK3568 设备树示例](#rk3568-设备树示例)
- [常用工具](#常用工具)

## 设备树基础

设备树是一种描述硬件的数据结构，用文本（DTS）编译成二进制（DTB），由 bootloader 传递给内核。目的是将硬件描述从内核代码中分离，避免大量板级文件硬编码。

- **DTS**：设备树源文件（文本）
- **DTSI**：设备树包含文件（被多个 DTS 引用）
- **DTB**：编译后的二进制设备树
- **DTC**：设备树编译器

## 语法规则

```dts
/dts-v1/;                    // 版本声明
/include/ "rk3568.dtsi"      // 包含 SoC 级 dtsi

/ {                          // 根节点
    model = "My Board";      // 属性
    compatible = "vendor,my-board", "rockchip,rk3568";

    aliases {                // 别名节点
        serial0 = &uart2;
    };

    chosen {                 // 运行时参数
        bootargs = "console=ttyS2,115200 root=/dev/mmcblk1p2 rw";
    };

    memory@80000000 {        // 内存节点
        device_type = "memory";
        reg = <0x80000000 0x20000000>;  // 起始地址 0x80000000, 大小 512MB
    };

    my_leds {                // 自定义节点
        compatible = "vendor,my-leds";
        led-gpios = <&gpio3 RK_PA5 GPIO_ACTIVE_HIGH>;
        status = "okay";
    };
};

&uart2 {                     // 引用已有节点并追加/修改
    status = "okay";
};
```

### 节点命名规则

格式：`node-name@unit-address`

- `node-name`：用小写字母、数字、连字符，最多 31 字符
- `unit-address`：设备的基地址（与 `reg` 第一个地址对应）
- 没有地址的节点可省略 `@unit-address`

### 属性值类型

```dts
// 32位无符号整数（大端）
clock-frequency = <400000>;

// 64位整数（两个32位）
reg = <0x80000000 0x20000000>;

// 字符串
compatible = "vendor,device";

// 字符串列表
compatible = "vendor,board", "rockchip,rk3568";

// 字节数组
mac-address = [00 11 22 33 44 55];

// 空属性（布尔标志）
spi-cpol;
```

## 常用属性

| 属性 | 含义 |
|------|------|
| `compatible` | 设备匹配字符串，格式 `"manufacturer,model"` |
| `reg` | 地址空间：`<address length>` 对 |
| `status` | `"okay"` 启用 / `"disabled"` 禁用 |
| `interrupts` | 中断描述 |
| `interrupt-parent` | 中断控制器 phandle |
| `clocks` | 时钟 phandle 列表 |
| `clock-names` | 时钟名称 |
| `pinctrl-0` | 引脚配置 phandle |
| `pinctrl-names` | 引脚状态名称，如 `"default"` |
| `*-gpios` | GPIO 引用，如 `reset-gpios` |
| `#address-cells` | 子节点 reg 中地址占几个 cell |
| `#size-cells` | 子节点 reg 中大小占几个 cell |

## 地址与中断

### 地址映射

```dts
soc {
    #address-cells = <1>;   // 子节点地址用1个cell
    #size-cells = <1>;      // 子节点大小用1个cell

    peripheral@02000000 {
        reg = <0x02000000 0x1000>;  // 基地址0x02000000, 长度0x1000
    };
};
```

### 中断

```dts
// 中断控制器节点
intc: interrupt-controller@fd000000 {
    compatible = "arm,gic-v3";
    #interrupt-cells = <3>;      // 中断描述用3个cell
    interrupt-controller;         // 标记为中断控制器
};

// 使用中断的设备
my_device@02000000 {
    interrupt-parent = <&intc>;
    // GIC: <类型 中断号 触发方式>
    // 类型: GIC_SPI(共享外设中断), GIC_PPI(私有外设中断)
    // 触发方式: IRQ_TYPE_EDGE_RISING, IRQ_TYPE_LEVEL_HIGH 等
    interrupts = <GIC_SPI 25 IRQ_TYPE_LEVEL_HIGH>;
};
```

RK3568 的 GIC 中断号换算：SPI 中断号从 0 开始，对应硬件中断号 = SPI号 + 32。设备树中写的是 SPI 编号。

## 驱动中解析设备树

```c
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>

struct device_node *np = pdev->dev.of_node;

// 读取属性
u32 val;
of_property_read_u32(np, "clock-frequency", &val);
of_property_read_u32_array(np, "reg", arr, 2);
const char *str;
of_property_read_string(np, "compatible", &str);

// 检查属性是否存在
if (of_property_read_bool(np, "spi-cpol")) { ... }

// 获取内存映射
void __iomem *base = of_iomap(np, 0);

// 获取中断号
int irq = irq_of_parse_and_map(np, 0);

// 获取 GPIO（旧 API）
int gpio = of_get_named_gpio(np, "reset-gpios", 0);

// 遍历子节点
struct device_node *child;
for_each_child_of_node(np, child) {
    // 处理每个子节点
}
```

## RK3568 设备树示例

### 目录结构

```
arch/arm64/boot/dts/rockchip/
├── rk3568.dtsi              # SoC 公共定义
└── rk3568-your-board.dts    # 板级文件（#include "rk3568.dtsi"）
```

### 启用 UART

```dts
&uart2 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&uart2m0_xfer>;
};
```

RK3568 调试串口通常是 uart2，console 参数：`console=ttyS2,115200`。

### 自定义 I2C 设备

```dts
&i2c1 {
    status = "okay";
    clock-frequency = <400000>;

    my_sensor@48 {
        compatible = "vendor,my-sensor";
        reg = <0x48>;
        interrupt-parent = <&gpio0>;
        interrupts = <RK_PB5 IRQ_TYPE_EDGE_FALLING>;
    };
};
```

### GPIO 按键与 LED

```dts
gpio_keys {
    compatible = "gpio-keys";
    pinctrl-names = "default";
    pinctrl-0 = <&key_pins>;

    key_power: power-key {
        gpios = <&gpio0 RK_PB5 GPIO_ACTIVE_LOW>;
        label = "power";
        linux,code = <KEY_POWER>;
        debounce-interval = <100>;
    };
};

gpio_leds {
    compatible = "gpio-leds";

    led_user {
        gpios = <&gpio3 RK_PA5 GPIO_ACTIVE_HIGH>;
        label = "user_led";
        linux,default-trigger = "heartbeat";
    };
};
```

### 自定义 pinctrl

```dts
&pinctrl {
    my_device {
        my_pins: my-pins {
            rockchip,pins = <0 RK_PB5 RK_FUNC_GPIO &pcfg_pull_up>;
            /* bank=0, pin=PB5, 功能=GPIO, 配置=上拉 */
        };
    };
};
```

更多 RK3568 平台细节见 `platform-rk3568.md`。

## 常用工具

```bash
# 编译 DTS 为 DTB
dtc -I dts -O dtb -o board.dtb board.dts

# 反编译 DTB 为 DTS
dtc -I dtb -O dts -o board.dts board.dtb

# 运行时查看设备树
ls /proc/device-tree/
cat /proc/device-tree/model

# 查看设备树解析后的节点
ls /sys/firmware/devicetree/base/
```
