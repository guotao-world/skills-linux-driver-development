# RK3568 平台参考

## 目录

- [芯片概述](#芯片概述)
- [GPIO 编号计算](#gpio-编号计算)
- [设备树路径与结构](#设备树路径与结构)
- [常用外设节点](#常用外设节点)
- [内核源码与编译](#内核源码与编译)
- [Pinctrl 引脚复用](#pinctrl-引脚复用)
- [中断控制器](#中断控制器)
- [常见问题](#常见问题)

## 芯片概述

| 项目 | 规格 |
|------|------|
| CPU | 四核 ARM Cortex-A55，最高 2.0GHz |
| GPU | Mali-G52 2EE |
| NPU | 0.8 TOPS 算力（支持 INT8/INT16） |
| 内存 | DDR4/DDR3L/LPDDR4/LPDDR4X，最大 8GB |
| 存储 | eMMC、SDMMC、SPI NAND/NOR Flash、SATA |
| 显示 | 双路 MIPI-DSI / eDP / HDMI 2.0 / LVDS |
| 网络 | 双路 GMAC（千兆以太网） |
| USB | USB 3.0 x1、USB 2.0 OTG x2 |
| PCIe | PCIe 3.0 x2（1 通道） |
| GPIO | 5 组（GPIO0~GPIO4），每组 32 个引脚 |
| 其他 | I2C x6、SPI x4、UART x10、PWM x16、ADC、SARADC |

架构：ARM64（aarch64），交叉编译工具链用 `aarch64-linux-gnu-`。

## GPIO 编号计算

RK3568 有 5 组 GPIO（GPIO0~GPIO4），每组 32 个引脚，每组内分 A/B/C/D 四个 port，每个 port 8 个引脚：

```
引脚命名: GPIO<bank>_<port><pin>
例如: GPIO3_A5, GPIO0_D2

编号公式:
  gpio_number = bank * 32 + port_offset + pin
  其中 port_offset: A=0, B=8, C=16, D=24
```

| 引脚名 | bank | port | pin | 计算 | 编号 |
|--------|------|------|-----|------|------|
| GPIO0_A0 | 0 | A | 0 | 0×32+0+0 | 0 |
| GPIO0_B3 | 0 | B | 3 | 0×32+8+3 | 11 |
| GPIO0_D7 | 0 | D | 7 | 0×32+24+7 | 31 |
| GPIO1_A0 | 1 | A | 0 | 1×32+0+0 | 32 |
| GPIO2_C4 | 2 | C | 4 | 2×32+16+4 | 84 |
| GPIO3_A5 | 3 | A | 5 | 3×32+0+5 | 101 |
| GPIO4_D7 | 4 | D | 7 | 4×32+24+7 | 159 |

> 新驱动推荐使用 GPIO 描述符 API（`devm_gpiod_get`），无需手动计算编号，通过设备树的 `*-gpios` 属性引用即可。

## 设备树路径与结构

RK3568 设备树位于内核源码：

```
arch/arm64/boot/dts/rockchip/
├── rk3568.dtsi              # SoC 级公共定义
├── rk3568-evb.dts           # 官方 EVB 开发板
├── rk3568-firefly-itx-3568j.dts  # 萤火虫 ITX-3568J
└── ...（各厂商板级 dts）
```

板级 DTS 通常 `#include "rk3568.dtsi"`，然后在根节点追加板级信息，并通过 `&节点名` 引用并修改 SoC 节点。

## 常用外设节点

### UART

```dts
&uart2 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&uart2m0_xfer>;
};
```

RK3568 有 uart0~uart9，其中 uart0 通常用作调试串口（console）。

### I2C

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

RK3568 有 i2c0~i2c5，共 6 路 I2C。

### SPI

```dts
&spi0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&spi0m0_cs0 &spi0m0_pins>;

    my_spi_dev@0 {
        compatible = "vendor,my-spi-device";
        reg = <0>;
        spi-max-frequency = <10000000>;
    };
};
```

RK3568 有 spi0~spi3，共 4 路 SPI。

### PWM

```dts
&pwm0 {
    status = "okay";
    pinctrl-names = "active";
    pinctrl-0 = <&pwm0m0_pins>;
};
```

RK3568 有 pwm0~pwm15，共 16 路 PWM。

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

## 内核源码与编译

### 获取源码

Rockchip 官方内核通常基于 Linux 5.10 或 4.19：

```bash
# 厂商 BSP 内核（以 Firefly 为例）
git clone https://gitlab.com/firefly-linux/kernel.git -b rk3568/linux5.10
```

### 编译

```bash
# 设置环境
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

# 配置（选择对应板级 defconfig）
make rk3568_linux_defconfig
# 或 make firefly_rk3568_defconfig（视厂商而定）

# 编译内核镜像
make Image -j$(nproc)

# 编译设备树
make rk3568-your-board.dtb

# 编译模块
make modules -j$(nproc)
```

### 编译外部驱动模块

```bash
make -C /path/to/kernel M=$(pwd) modules \
     ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

## Pinctrl 引脚复用

RK3568 大部分引脚支持多种复用功能，通过 pinctrl 子系统配置。设备树中引用预定义的 pinctrl 节点：

```dts
&i2c3 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&i2c3m0_xfer>;   /* m0 = 复用功能0 */
};
```

命名规则：`<外设><编号>m<复用号>_<功能>`，例如：

- `i2c1m0_xfer` — I2C1 的第 0 组复用引脚（SDA+SCL）
- `uart2m1_xfer` — UART2 的第 1 组复用引脚
- `spi0m0_cs0` — SPI0 的第 0 组复用 + 片选 0

自定义 pinctrl 节点写在 `&pinctrl` 下：

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

引脚宏定义：`RK_PA0` ~ `RK_PD7`，对应每组内的 32 个引脚。

## 中断控制器

RK3568 使用 GIC（Generic Interrupt Controller），设备树中断描述格式：

```dts
interrupts = <GIC_SPI 25 IRQ_TYPE_LEVEL_HIGH>;
/*            类型    中断号 触发方式 */
```

- 类型：`GIC_SPI`（共享外设中断）或 `GIC_PPI`（私有外设中断）
- SPI 中断号：从 0 开始，对应硬件中断号 = SPI号 + 32
- 触发方式：`IRQ_TYPE_EDGE_RISING`、`IRQ_TYPE_EDGE_FALLING`、`IRQ_TYPE_LEVEL_HIGH`、`IRQ_TYPE_LEVEL_LOW`

GPIO 作为中断源时，在设备树中用 `interrupt-parent` + `interrupts`：

```dts
my_device {
    interrupt-parent = <&gpio3>;
    interrupts = <RK_PA5 IRQ_TYPE_EDGE_FALLING>;
};
```

## 常见问题

### 1. 串口无输出

- 确认 console 参数：`console=ttyS2,115200`（RK3568 调试串口通常是 uart2）
- 确认设备树中 uart2 状态为 `okay`
- 确认 pinctrl 配置正确

### 2. GPIO 控制无效

- 检查引脚是否被其他外设复用占用（pinctrl 冲突）
- 查看 `/sys/kernel/debug/pinctrl/pinctrl-handles` 确认引脚归属
- 确认 GPIO 编号计算正确

### 3. I2C 通信失败

- 确认 I2C 时钟频率不超过设备支持范围
- 确认 SDA/SCL 有上拉电阻
- 用 `i2cdetect -y 1` 扫描设备是否应答

### 4. 驱动 probe 不执行

- 确认设备树 `compatible` 与驱动 `of_match_table` 完全一致
- 确认节点 `status = "okay"`
- 查看 `/sys/bus/platform/devices/` 确认设备是否实例化
