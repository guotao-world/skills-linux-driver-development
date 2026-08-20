---
name: linux-driver-development
description: Linux 内核驱动开发专业技能，覆盖字符设备、平台设备、GPIO、I2C、SPI、设备树、中断处理、内核同步机制、调试技巧等。当用户需要编写、修改、调试 Linux 内核驱动模块，理解内核子系统，或解决驱动相关问题（如设备节点创建、probe 函数、compatible 匹配、中断注册、并发竞态等）时使用。适用于 ARM64 嵌入式平台（RK3568 等）的驱动开发学习与实践，所有模板以 Linux Kernel 4.19 为标准。
---

# Linux 驱动开发技能

本仓库是一个 **AI 助手技能（Skill）**，为嵌入式 Linux 驱动开发提供系统化的参考文档和可直接编译的代码模板。所有模板均以 **Linux Kernel 4.19** 为标准编写，兼容 **RK3568** 等 ARM64 嵌入式平台。

## 技能触发场景

当用户提出以下需求时，本技能被激活：

- 编写或修改 Linux 内核驱动模块（字符设备、平台设备、GPIO、I2C、SPI、块设备等）
- 调试驱动问题（probe 不执行、设备节点未创建、中断不触发、并发竞态、内核 Oops 等）
- 理解内核子系统（cdev、platform bus、blk-mq、设备树、中断子系统等）
- 基于 RK3568 等 ARM64 平台进行驱动开发实践

## 技能工作流程

面对驱动开发任务时，技能按以下顺序推进：

### 1. 确定驱动类型与子系统

先判断需求属于哪类驱动，再加载对应参考文档：

| 驱动类型 | 典型场景 | 参考文档 |
|---------|---------|---------|
| 字符设备 | 虚拟设备、简单自定义设备、LED/按键 | `references/char-device.md` |
| 平台设备 | 基于设备树的 SoC 外设 | `references/platform-device.md` |
| GPIO | 引脚控制、按键、LED | `references/gpio.md` |
| I2C | 传感器、EEPROM、触摸屏 | `references/i2c-spi.md` |
| SPI | Flash、显示屏、ADC | `references/i2c-spi.md` |
| 中断 | 按键中断、硬件中断 | `references/interrupts-sync.md` |
| 块设备 | ramdisk、SD卡、eMMC、硬盘等存储设备 | `references/block-device.md` |
| 设备树 | 硬件描述、节点编写 | `references/device-tree.md` |
| RK3568 平台 | RK3568 专用：GPIO编号、pinctrl、设备树、编译 | `references/platform-rk3568.md` |

### 2. 选择代码模板

根据驱动类型从 `assets/templates/` 复制对应模板，在此基础上修改：

| 模板文件 | 用途 |
|---------|------|
| `char_device_template.c` | 最基础的字符设备驱动（可读写虚拟设备） |
| `platform_driver_template.c` | 平台驱动框架（设备树匹配 + 寄存器映射 + 中断） |
| `gpio_driver_template.c` | GPIO 控制驱动（LED 输出 + 按键中断输入） |
| `i2c_driver_template.c` | I2C 客户端驱动（SMBus 寄存器读写） |
| `block_device_template.c` | 块设备驱动（blk-mq 框架，ramdisk 示例） |
| `Makefile.template` | 内核模块编译 Makefile |

### 3. 编写与编译

- 内核模块必须包含 `#include <linux/module.h>`、`MODULE_LICENSE("GPL")`
- 使用模板中的 Makefile，设置 `KDIR` 指向目标内核源码树
- 交叉编译时设置 `ARCH=arm64` 和 `CROSS_COMPILE=aarch64-linux-gnu-`

### 4. 调试与验证

- 优先使用 `printk` + `dmesg` 跟踪执行流程
- 检查 `/proc/devices`、`/sys/class/`、`/dev/` 确认设备注册
- 复杂问题参考 `references/debugging.md`

## 技能资源清单

```
.
├── SKILL.md                        # 技能定义与元数据
├── README.md                       # 本文件（技能说明）
├── LICENSE                         # GPL-3.0 许可证
├── .gitignore                      # Git 忽略规则
├── references/                     # 参考文档（10篇）
│   ├── char-device.md              # 字符设备驱动
│   ├── platform-device.md          # 平台设备驱动
│   ├── gpio.md                     # GPIO 子系统
│   ├── i2c-spi.md                  # I2C 与 SPI 驱动
│   ├── interrupts-sync.md          # 中断处理与内核同步
│   ├── block-device.md             # 块设备驱动（blk-mq）
│   ├── device-tree.md              # 设备树
│   ├── debugging.md                # 驱动调试技巧
│   ├── kernel-api-cheatsheet.md    # 内核 API 速查表
│   └── platform-rk3568.md          # RK3568 平台参考
└── assets/
    └── templates/                   # 代码模板（6个，均基于 4.19）
        ├── char_device_template.c
        ├── platform_driver_template.c
        ├── gpio_driver_template.c
        ├── i2c_driver_template.c
        ├── block_device_template.c
        └── Makefile.template
```

## 核心原则

1. **内核编程不同于用户态**：不能用 libc，不能浮点运算，注意内核栈大小限制（通常 8KB/16KB）
2. **错误处理必须完整**：每个内核 API 调用都要检查返回值，失败时回滚已分配资源
3. **并发安全**：访问共享数据必须加锁，中断上下文用 spinlock，进程上下文可用 mutex
4. **内存分配**：小内存用 `kmalloc`（物理连续），大内存用 `vmalloc`（虚拟连续），必须检查返回值
5. **设备树优先**：新驱动使用设备树描述硬件，避免硬编码寄存器地址

## 适用平台

| 平台 | 架构 | 工具链 | 内核版本 |
|------|------|--------|---------|
| RK3568 | ARM Cortex-A55 (64位) | aarch64-linux-gnu- | 4.19 / 5.10 |

## 许可证

本技能代码模板采用 [GPL-3.0](LICENSE) 许可证，与 Linux 内核保持一致。文档部分可自由引用。
