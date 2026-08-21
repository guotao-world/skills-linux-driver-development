---
name: linux-driver-development
description: Linux 内核驱动开发专业技能，覆盖字符设备、平台设备、GPIO、I2C、SPI、设备树、中断处理、内核同步机制、ASoC 音频 codec、调试技巧等。当用户需要编写、修改、调试 Linux 内核驱动模块，理解内核子系统，或解决驱动相关问题（如设备节点创建、probe 函数、compatible 匹配、中断注册、并发竞态、音频通路匹配等）时使用。适用于 ARM64 嵌入式平台（RK3568 等）的驱动开发学习与实践，所有模板以 Linux Kernel 4.19 为标准。
---

# Linux 驱动开发技能

## 工作流程

面对驱动开发任务时，按以下顺序推进：

### 1. 确定驱动类型与子系统

先判断用户需求属于哪类驱动，再加载对应参考：

| 驱动类型 | 典型场景 | 参考文档 |
|---------|---------|---------|
| 字符设备 | 虚拟设备、简单自定义设备、LED/按键 | `references/char-device.md` |
| 平台设备 | 基于设备树的 SoC 外设 | `references/platform-device.md` |
| GPIO | 引脚控制、按键、LED | `references/gpio.md` |
| I2C | 传感器、EEPROM、触摸屏、音频 codec | `references/i2c-spi.md` |
| SPI | Flash、显示屏、ADC | `references/i2c-spi.md` |
| 中断 | 按键中断、硬件中断 | `references/interrupts-sync.md` |
| 块设备 | ramdisk、SD卡、eMMC、硬盘等存储设备 | `references/block-device.md` |
| 设备树 | 硬件描述、节点编写 | `references/device-tree.md` |
| ASoC 音频 codec | 音频编解码器、dummy codec、机器驱动匹配 | `references/asoc-codec.md` |
| ASoC I2C codec | I2C 接口音频 codec、regmap、控件、DAPM | `references/asoc-i2c-codec.md` |
| RK3568 平台 | RK3568 专用：GPIO编号、pinctrl、设备树、编译 | `references/platform-rk3568.md` |

### 2. 选择代码模板

根据驱动类型从 `assets/templates/` 复制对应模板，在此基础上修改：

- `char_device_template.c` — 最基础的字符设备驱动
- `platform_driver_template.c` — 平台驱动框架（设备树匹配）
- `gpio_driver_template.c` — GPIO 控制驱动
- `i2c_driver_template.c` — I2C 客户端驱动
- `block_device_template.c` — 块设备驱动（blk-mq 框架，ramdisk 示例）
- `asoc_dummy_codec_template.c` — ASoC dummy codec 驱动（platform bus，最简）
- `asoc_i2c_dummy_codec_template.c` — ASoC I2C dummy codec 驱动（I2C + regmap + 控件 + DAPM）
- `Makefile.template` — 内核模块编译 Makefile

### 3. 编写与编译

- 内核模块必须包含 `#include <linux/module.h>`、`MODULE_LICENSE("GPL")`
- 使用模板中的 Makefile，设置 `KDIR` 指向目标内核源码树
- 交叉编译时设置 `ARCH=arm64` 和 `CROSS_COMPILE=aarch64-linux-gnu-`

### 4. 调试与验证

- 优先使用 `printk` + `dmesg` 跟踪执行流程
- 检查 `/proc/devices`、`/sys/class/`、`/dev/` 确认设备注册
- 音频驱动检查 `/proc/asound/cards`、`aplay -l`、`amixer contents`
- I2C 设备用 `i2cdetect` 扫描，regmap 用 debugfs 查看寄存器
- 复杂问题参考 `references/debugging.md`

## 核心原则

1. **内核编程不同于用户态**：不能用 libc，不能浮点运算，注意内核栈大小限制（通常 8KB/16KB）
2. **错误处理必须完整**：每个内核 API 调用都要检查返回值，失败时回滚已分配资源
3. **并发安全**：访问共享数据必须加锁，中断上下文用 spinlock，进程上下文可用 mutex
4. **内存分配**：小内存用 `kmalloc`（物理连续），大内存用 `vmalloc`（虚拟连续），必须检查返回值
5. **设备树优先**：新驱动使用设备树描述硬件，避免硬编码寄存器地址

## 常用 API 速查

完整速查表见 `references/kernel-api-cheatsheet.md`，以下是最常用的：

```c
// 模块
module_init(fn); module_exit(fn);
MODULE_LICENSE("GPL"); MODULE_AUTHOR("name"); MODULE_DESCRIPTION("desc");

// 打印
printk(KERN_INFO "msg: %d\n", val);  // 级别: KERN_ERR/WARN/INFO/DEBUG

// 字符设备
alloc_chrdev_region(&dev, 0, 1, "name");  // 动态分配设备号
cdev_init(&cdev, &fops); cdev_add(&cdev, dev, 1);
class_create(THIS_MODULE, "name"); device_create(cls, NULL, dev, NULL, "name");

// 内存
kmalloc(size, GFP_KERNEL); kfree(ptr);
vmalloc(size); vfree(ptr);
copy_to_user(dst, src, n); copy_from_user(dst, src, n);  // 可能睡眠

// GPIO
gpio_request(gpio, "label"); gpio_direction_output(gpio, val);
gpio_get_value(gpio); gpio_set_value(gpio, val); gpio_free(gpio);
devm_gpiod_get(dev, "con_id", flags);  // 推荐: 设备树+描述符API

// 中断
request_irq(irq, handler, flags, "name", dev); free_irq(irq, dev);

// 块设备（blk-mq）
register_blkdev(major, "name"); unregister_blkdev(major, "name");
alloc_disk(minors); add_disk(disk); del_gendisk(disk); put_disk(disk);
set_capacity(disk, sectors);
blk_mq_alloc_tag_set(&tag_set); blk_mq_free_tag_set(&tag_set);
blk_mq_init_queue(&tag_set); blk_cleanup_queue(queue);
blk_mq_start_request(req); __blk_mq_end_request(req, status);
blk_update_request(req, status, bytes);
blk_rq_pos(req); blk_rq_cur_bytes(req); req_op(req); op_is_write(op);
rq_for_each_segment(bvec, req, iter);  // 遍历请求中所有 bio 段

// ASoC codec（4.18+）
devm_snd_soc_register_component(dev, &component_drv, &dai_drv, num_dai);

// regmap（I2C codec 用）
devm_regmap_init_i2c(client, &regmap_config);
regmap_read(regmap, reg, &val); regmap_write(regmap, reg, val);
regmap_update_bits(regmap, reg, mask, val);
```

## 注意事项

- 回答用户驱动问题时，优先给出**可编译运行的完整代码**，而非片段
- 解释代码时按「初始化 → 核心逻辑 → 清理」三段式说明
- 涉及硬件寄存器时，提醒用户查阅对应芯片的 Reference Manual
- RK3568 平台相关问题，先查阅 `references/platform-rk3568.md`，注意是 ARM64 架构，工具链用 `aarch64-linux-gnu-`
- ASoC 音频 codec 基础问题，先查阅 `references/asoc-codec.md`
- ASoC I2C codec（含 regmap、控件、DAPM）问题，先查阅 `references/asoc-i2c-codec.md`，注意 4.19 用 `snd_soc_component_driver` 而非废弃的 `snd_soc_codec_driver`，component probe 中需绑定 `component->regmap`
- 所有代码模板以 Linux Kernel 4.19 为标准编写
- 用户有 C 语言基础，解释时可直接使用指针、结构体等概念，无需额外铺垫
