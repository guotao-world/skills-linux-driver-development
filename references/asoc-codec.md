# ASoC Codec 驱动开发

## 目录

- [ASoC 框架概述](#asoc-框架概述)
- [三层分离架构](#三层分离架构)
- [Codec 驱动核心结构](#codec-驱动核心结构)
- [开发步骤](#开发步骤)
- [DAI 能力描述](#dai-能力描述)
- [Component 注册](#component-注册)
- [设备树绑定](#设备树绑定)
- [完整示例（dummy codec）](#完整示例dummy-codec)
- [4.19 注意事项](#419-注意事项)
- [调试技巧](#调试技巧)

## ASoC 框架概述

ASoC（ALSA System on Chip）是 Linux 内核中针对嵌入式 SoC 音频子系统的框架，在标准 ALSA 基础上做了分层设计，解决了传统 ALSA 驱动中硬件相关代码与板级逻辑耦合的问题。

ASoC 的核心目标：

- **代码复用**：codec 驱动与平台无关，可在不同板子上复用
- **硬件抽象**：将 DMA、I2S 控制器、codec、机器板级配置分离
- **电源管理**：统一管理音频子系统的时钟和电源
- **动态 PCM**：支持复杂的音频路由（如多 codec、DSP 转发）

## 三层分离架构

ASoC 将音频驱动分为三层，每层独立开发，通过设备树和 sound card 绑定在一起：

```
┌─────────────────────────────────────────────┐
│              Machine 驱动（板级）              │
│  负责：CPU DAI ↔ Codec DAI 绑定、widget 路由、  │
│        时钟配置、偏置电压、耳机检测等板级逻辑     │
│  常见：simple-audio-card、rk3399-sound 等      │
├──────────────────┬──────────────────────────┤
│   Platform 驱动   │      Codec 驱动          │
│  (SoC 厂商提供)   │   (codec 厂商提供)        │
│  负责：            │  负责：                   │
│  - DMA 传输        │  - 寄存器配置             │
│  - I2S/PCM 控制器  │  - 音量/MUX/ADC/DAC 控件  │
│  - 时钟管理        │  - DAPM 电源域            │
│  - 平台特有逻辑    │  - DAI 格式/时钟主从配置   │
└──────────────────┴──────────────────────────┘
```

| 层级 | 驱动类型 | 典型文件 | 谁来写 |
|------|---------|---------|--------|
| Platform | CPU DAI + DMA | `sound/soc/rockchip/rockchip_i2s.c` | SoC 厂商 |
| Codec | codec 芯片驱动 | `sound/soc/codecs/es8388.c` | codec 厂商/开发者 |
| Machine | 板级绑定 | `sound/soc/generic/simple-card.c` | 板级开发者 |

本技能聚焦于 **Codec 驱动**的开发。

## Codec 驱动核心结构

### snd_soc_dai_driver

描述一个数字音频接口（DAI）的能力，是 codec 驱动中最核心的数据结构：

```c
static struct snd_soc_dai_driver my_codec_dai = {
    .name = "my-codec-dai",

    .playback = {
        .stream_name  = "Playback",
        .channels_min = 1,
        .channels_max = 2,
        .rates        = SNDRV_PCM_RATE_8000_48000,
        .formats      = SNDRV_PCM_FMTBIT_S16_LE,
    },

    .capture = {
        .stream_name  = "Capture",
        .channels_min = 1,
        .channels_max = 2,
        .rates        = SNDRV_PCM_RATE_8000_48000,
        .formats      = SNDRV_PCM_FMTBIT_S16_LE,
    },

    .ops = &my_codec_dai_ops,  /* DAI 操作函数集，可选 */
};
```

### snd_soc_component_driver

ASoC 通用组件描述结构，codec/platform/machine 三层共用：

```c
static const struct snd_soc_component_driver my_codec_component = {
    .name        = "my-codec",
    .probe       = my_codec_component_probe,   /* 可选 */
    .remove      = my_codec_component_remove,  /* 可选 */
    .controls    = my_codec_controls,          /* 控件数组 */
    .num_controls = ARRAY_SIZE(my_codec_controls),
    .dapm_widgets = my_codec_dapm_widgets,    /* DAPM 电源域 */
    .num_dapm_widgets = ARRAY_SIZE(my_codec_dapm_widgets),
    .dapm_routes = my_codec_dapm_routes,       /* 音频路由 */
    .num_dapm_routes = ARRAY_SIZE(my_codec_dapm_routes),
};
```

最简 dummy codec 只需设置 `.name`，其余回调全部留空。

### snd_soc_dai_ops

DAI 操作函数集，定义硬件相关的配置回调：

```c
static const struct snd_soc_dai_ops my_codec_dai_ops = {
    .hw_params   = my_codec_hw_params,    /* 配置采样率、格式、声道 */
    .set_fmt     = my_codec_set_fmt,      /* 设置 DAI 格式（I2S/左对齐/右对齐） */
    .set_clkdiv  = my_codec_set_clkdiv,   /* 设置时钟分频 */
    .set_sysclk  = my_codec_set_sysclk,   /* 设置系统时钟 */
    .digital_mute = my_codec_digital_mute, /* 数字静音 */
};
```

dummy codec 不需要实现这些回调。

## 开发步骤

1. **定义 DAI 能力**：用 `snd_soc_dai_driver` 描述 playback/capture 的声道、采样率、格式
2. **定义 component**：用 `snd_soc_component_driver` 描述组件名称和可选回调
3. **实现 probe**：在 `platform_driver.probe` 中调用 `devm_snd_soc_register_component()` 注册
4. **设备树匹配**：定义 `of_device_id` 匹配表，通过 `compatible` 匹配
5. **注册 platform_driver**：用 `module_platform_driver()` 宏注册

## DAI 能力描述

### 采样率标志

```c
SNDRV_PCM_RATE_8000      /* 8kHz */
SNDRV_PCM_RATE_16000     /* 16kHz */
SNDRV_PCM_RATE_44100     /* 44.1kHz */
SNDRV_PCM_RATE_48000     /* 48kHz */
SNDRV_PCM_RATE_8000_48000  /* 连续范围 8k~48k */
SNDRV_PCM_RATE_CONTINUOUS   /* 连续采样率 */
```

### 数据格式标志

```c
SNDRV_PCM_FMTBIT_S8          /* 8位有符号 */
SNDRV_PCM_FMTBIT_S16_LE      /* 16位小端（最常用） */
SNDRV_PCM_FMTBIT_S24_LE      /* 24位小端 */
SNDRV_PCM_FMTBIT_S32_LE      /* 32位小端 */
```

### 声道数

```c
.channels_min = 1,   /* 最少声道数 */
.channels_max = 2,   /* 最多声道数（立体声设为2） */
```

## Component 注册

### devm 托管版本（推荐，4.18+）

```c
ret = devm_snd_soc_register_component(&pdev->dev,
                                       &component_driver,
                                       &dai_driver,
                                       1);  /* DAI 数量 */
if (ret < 0)
    return ret;
```

### 非托管版本（4.18 以前）

```c
ret = snd_soc_register_component(&pdev->dev,
                                  &component_driver,
                                  &dai_driver,
                                  1);
if (ret < 0)
    return ret;

/* remove 中需手动注销 */
snd_soc_unregister_component(&pdev->dev);
```

## 设备树绑定

### Codec 节点

```dts
my_dummy_codec: dummy-codec {
    compatible = "my,dummy-codec";
    #sound-dai-cells = <0>;   /* 0 表示 codec 只有一个 DAI */
};
```

### 机器驱动节点（simple-audio-card）

```dts
sound {
    compatible = "simple-audio-card";
    simple-audio-card,name = "my-audio-card";
    simple-audio-card,format = "i2s";   /* I2S 格式 */
    simple-audio-card,bitclock-master = <&cpu_dai>;
    simple-audio-card,frame-master = <&cpu_dai>;

    cpu_dai: simple-audio-card,cpu {
        sound-dai = <&i2s0>;   /* 引用 CPU DAI（RK3568 的 I2S 控制器） */
    };

    simple-audio-card,codec {
        sound-dai = <&my_dummy_codec>;  /* 引用 codec DAI */
    };
};
```

### RK3568 I2S 控制器

RK3568 有多个 I2S/PCM 控制器，设备树中通常为 `&i2s0`、`&i2s1` 等，需在板级 dts 中启用：

```dts
&i2s0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&i2s0m0_clk  /* I2S0_BCLK */
                 &i2s0m0_fs   /* I2S0_LRCK */
                 &i2s0m0_sdo  /* I2S0_SDO */
                 &i2s0m0_sdi>;/* I2S0_SDI */
};
```

## 完整示例（dummy codec）

见 `assets/templates/asoc_dummy_codec_template.c`，包含完整的可编译代码。

核心逻辑仅三步：

1. 定义 `snd_soc_dai_driver`（描述 DAI 能力）
2. 定义 `snd_soc_component_driver`（描述组件名）
3. 在 probe 中调用 `devm_snd_soc_register_component()` 注册

## 4.19 注意事项

| 特性 | 4.19 行为 |
|------|----------|
| `devm_snd_soc_register_component()` | 可用（从 4.18 引入） |
| `snd_soc_register_component()` | 可用，但需手动注销 |
| `snd_soc_codec_driver` | 已废弃，统一用 `snd_soc_component_driver` |
| DAPM（动态音频电源管理） | 完全支持，widget 和 route 通过 component 注册 |
| `#sound-dai-cells` | 设备树中必须声明，0 表示单 DAI |
| simple-audio-card | 内核自带，位于 `sound/soc/generic/simple-card.c` |

> 注意：4.19 之前的内核中，codec 驱动使用 `snd_soc_codec_driver` 而非 `snd_soc_component_driver`，注册函数为 `snd_soc_register_codec()`。4.19 已统一为 component 框架。

## 调试技巧

### 查看注册的声卡

```bash
cat /proc/asound/cards          # 查看已注册的声卡
cat /proc/asound/pcm            # 查看 PCM 设备
aplay -l                        # 列出播放设备
arecord -l                      # 列出录制设备
```

### 查看 ASoC 组件

```bash
ls /sys/kernel/debug/asoc/     # ASoC 调试目录（需 debugfs）
cat /sys/kernel/debug/asoc/components
cat /sys/kernel/debug/asoc/dais
```

### 常见问题

| 现象 | 可能原因 | 解决 |
|------|---------|------|
| `aplay -l` 无设备 | 机器驱动未匹配或 codec probe 失败 | 检查 dmesg 中 ASoC 相关日志；确认设备树 compatible 匹配 |
| codec probe 成功但无声卡 | 机器驱动（simple-audio-card）未绑定 codec | 检查 sound 节点的 `sound-dai` 引用是否正确 |
| 播放报错 `Invalid argument` | DAI 能力不匹配（采样率/格式/声道超出 codec 支持范围） | 检查 `snd_soc_dai_driver` 的 rates/formats/channels 设置 |
| I2S 无时钟 | 时钟主从配置错误 | 检查 `simple-audio-card,bitclock-master` 和 `frame-master` 设置 |
| 注册返回 -ENODEV | `#sound-dai-cells` 缺失或不匹配 | 在 codec 节点添加 `#sound-dai-cells = <0>;` |

### printk 调试

在 probe 和关键回调中添加打印：

```c
dev_info(&pdev->dev, "codec probe\n");
dev_info(&pdev->dev, "register component ret=%d\n", ret);
```

查看日志：

```bash
dmesg | grep -i "asoc\|sound\|codec\|my-dummy"
```
