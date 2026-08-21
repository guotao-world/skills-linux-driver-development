# ASoC I2C Codec 驱动开发

## 目录

- [概述](#概述)
- [I2C codec 与 platform codec 的区别](#i2c-codec-与-platform-codec-的区别)
- [核心数据结构](#核心数据结构)
- [regmap 子系统](#regmap-子系统)
- [ALSA 控件（kcontrol）](#alsa-控件kcontrol)
- [DAPM 动态音频电源管理](#dapm-动态音频电源管理)
- [component probe 与 regmap 绑定](#component-probe-与-regmap-绑定)
- [I2C probe 完整流程](#i2c-probe-完整流程)
- [设备树绑定](#设备树绑定)
- [4.19 注意事项](#419-注意事项)
- [调试技巧](#调试技巧)

## 概述

大多数真实音频 codec 芯片（如 ES8388、RT5651、WM8960、NAU88C22 等）通过 **I2C 总线**与 SoC 通信，用于配置寄存器（音量、采样率、电源、输入输出路由等）。音频数据则通过 I2S/PCM 总线传输。

I2C codec 驱动在 platform dummy codec 的基础上增加了：

- **regmap 子系统**：统一管理寄存器读写，支持缓存、缓存同步、寄存器默认值
- **ALSA 控件（kcontrol）**：暴露音量、MUX、开关等控件到用户空间（amixer/alsamixer）
- **DAPM（动态音频电源管理）**：自动管理音频路径上各部件的电源，仅在使用时上电
- **component probe**：在 component 注册后绑定 regmap，使控件和 DAPM 能自动访问寄存器

## I2C codec 与 platform codec 的区别

| 特性 | platform dummy codec | I2C dummy codec |
|------|---------------------|-----------------|
| 总线类型 | platform bus | I2C bus |
| 驱动结构 | `platform_driver` | `i2c_driver` |
| 寄存器访问 | 无（纯虚拟） | regmap（物理 I2C） |
| ALSA 控件 | 无 | Playback Volume（TLV） |
| DAPM widget | 无 | DAC、ADC、LINEOUT、MIC |
| DAPM route | 无 | MIC→ADC、DAC→LINEOUT |
| component probe | 无 | 绑定 regmap |
| 私有数据 | 无 | `struct my_dummy_priv`（含 regmap） |
| 适用场景 | 验证音频通路、机器驱动匹配 | 真实 I2C codec 开发起点 |

## 核心数据结构

### 私有数据结构

```c
struct my_dummy_priv {
    struct regmap *regmap;   /* regmap 实例，I2C probe 中创建 */
    struct device *dev;      /* 设备指针 */
};
```

私有数据在 I2C probe 中分配并初始化，通过 `i2c_set_clientdata(client, priv)` 保存到 device 的 drvdata，在 component probe 中通过 `dev_get_drvdata(component->dev)` 取回。

### regmap_config

```c
static const struct regmap_config my_dummy_regmap_config = {
    .reg_bits         = 8,                    /* 寄存器地址 8 位 */
    .val_bits         = 8,                    /* 寄存器数据 8 位 */
    .max_register     = 0xff,                 /* 最大寄存器地址 */
    .reg_defaults     = my_dummy_reg_defaults,/* 默认值表 */
    .num_reg_defaults = ARRAY_SIZE(...),
    .cache_type       = REGCACHE_RBTREE,     /* 红黑树缓存 */
    .writeable_reg    = my_dummy_writeable_reg,
    .readable_reg     = my_dummy_readable_reg,
};
```

### snd_soc_component_driver

```c
static const struct snd_soc_component_driver my_dummy_component = {
    .name             = "i2c-dummy-codec",
    .controls         = my_dummy_controls,      /* 控件数组 */
    .num_controls     = ARRAY_SIZE(...),
    .dapm_widgets     = my_dummy_widgets,       /* DAPM widget */
    .num_dapm_widgets = ARRAY_SIZE(...),
    .dapm_routes      = my_dummy_routes,        /* 音频路由 */
    .num_dapm_routes  = ARRAY_SIZE(...),
    .probe            = my_dummy_component_probe,/* component probe */
};
```

## regmap 子系统

regmap 是 Linux 内核提供的统一寄存器访问抽象层，支持 I2C、SPI、MMIO、AC97 等多种后端。使用 regmap 的好处：

- **统一 API**：`regmap_read()` / `regmap_write()` 不关心底层总线
- **缓存支持**：读操作可命中缓存，减少物理 I2C 访问
- **缓存同步**：`regcache_sync()` 将缓存批量写回硬件（resume 时用）
- **默认值管理**：`reg_defaults` 表自动初始化缓存
- **寄存器访问控制**：`writeable_reg` / `readable_reg` / `volatile_reg` 回调

### 初始化 regmap

```c
/* devm 托管版本，设备 detach 时自动释放 */
priv->regmap = devm_regmap_init_i2c(client, &my_dummy_regmap_config);
if (IS_ERR(priv->regmap))
    return PTR_ERR(priv->regmap);
```

### 缓存类型

| 类型 | 说明 | 适用场景 |
|------|------|---------|
| `REGCACHE_NONE` | 不缓存 | 寄存器少、每次都需物理访问 |
| `REGCACHE_RBTREE` | 红黑树缓存 | 寄存器稀疏、地址不连续 |
| `REGCACHE_FLAT` | 数组缓存 | 寄存器地址连续（如 0x00~0xff） |
| `REGCACHE_MAPLE` | Maple 树缓存（5.x+） | 大寄存器空间 |

4.19 推荐用 `REGCACHE_RBTREE` 或 `REGCACHE_FLAT`。

### 纯模拟模式（不访问真实硬件）

如果 codec 芯片未焊接或只想验证音频通路，可使能 cache-only 模式，所有读写走缓存：

```c
regmap_cache_only(priv->regmap, true);
```

> 注意：cache-only 模式下 `regmap_write()` 只写缓存不写硬件，`regmap_read()` 只读缓存。resume 时 `regcache_sync()` 也不会执行。适合纯调试场景。

### 常用 regmap API

```c
unsigned int val;
regmap_read(regmap, 0x20, &val);       /* 读寄存器 */
regmap_write(regmap, 0x20, 0x3f);      /* 写寄存器 */
regmap_update_bits(regmap, reg, mask, val);  /* 读-改-写指定位 */
regmap_set_bits(regmap, reg, mask);     /* 置位 */
regmap_clear_bits(regmap, reg, mask);   /* 清位 */
regcache_cache_only(regmap, true);      /* 切换 cache-only */
regcache_sync(regmap);                   /* 缓存同步到硬件 */
```

## ALSA 控件（kcontrol）

ALSA 控件是用户空间与驱动交互的接口，通过 `amixer`、`alsamixer`、`pactl` 等工具访问。常见控件类型：

| 宏 | 类型 | 说明 |
|----|------|------|
| `SOC_SINGLE` | 单值控件 | 音量、增益等，无分贝信息 |
| `SOC_SINGLE_TLV` | 单值+TLV | 带分贝缩放的音量控件 |
| `SOC_DOUBLE` | 双值控件 | 左右声道独立音量 |
| `SOC_ENUM` | 枚举控件 | 输入源选择（MUX） |
| `SOC_ENUM_DOUBLE` | 双枚举 | 左右声道独立输入选择 |
| `SOC_MUX` | MUX 控件 | DAPM 多路复用器 |
| `SOC_MIXER` | Mixer 控件 | DAPM 混音器 |

### SOC_SINGLE_TLV 详解

```c
static const DECLARE_TLV_DB_SCALE(dac_tlv, -6300, 100, 0);

static const struct snd_kcontrol_new my_dummy_controls[] = {
    SOC_SINGLE_TLV("Playback Volume",  /* 控件名称 */
                   0x20,                 /* 寄存器地址 */
                   0,                    /* 偏移位（从 bit0 开始） */
                   63,                   /* 最大值（6位分辨率） */
                   0,                    /* 是否反转（0=不反转） */
                   dac_tlv)              /* TLV 分贝信息 */
};
```

### TLV 分贝缩放

```c
DECLARE_TLV_DB_SCALE(name, min, step, mute)
```

| 参数 | 含义 | 示例 |
|------|------|------|
| `min` | 最小分贝值 × 100 | -6300 = -63.00 dB |
| `step` | 每步分贝值 × 100 | 100 = 1.00 dB/步 |
| `mute` | 是否支持静音（值为 mute 时的寄存器值） | 0 = 不支持 |

控件值 0 → -63 dB，值 63 → 0 dB（-63 + 63×1 = 0 dB）。

> 控件通过 `component->regmap` 自动读写寄存器，无需手动实现 get/put 回调。前提是 component probe 中已绑定 regmap。

## DAPM 动态音频电源管理

DAPM（Dynamic Audio Power Management）自动管理音频路径上各部件的电源状态，仅在实际使用时上电，达到省电目的。

### DAPM widget 类型

| 宏 | 类型 | 说明 |
|----|------|------|
| `SND_SOC_DAPM_INPUT` | 物理输入 | MIC、LINEIN |
| `SND_SOC_DAPM_OUTPUT` | 物理输出 | LINEOUT、HP（耳机） |
| `SND_SOC_DAPM_DAC` | 数模转换器 | 播放路径 |
| `SND_SOC_DAPM_ADC` | 模数转换器 | 录制路径 |
| `SND_SOC_DAPM_MUX` | 多路复用器 | 输入源选择 |
| `SND_SOC_DAPM_MIXER` | 混音器 | 多路信号混合 |
| `SND_SOC_DAPM_SWITCH` | 开关 | 通断控制 |
| `SND_SOC_DAPM_PRE`/`POST` | 前置/后置处理 | 特效、滤波 |

### widget 定义

```c
static const struct snd_soc_dapm_widget my_dummy_widgets[] = {
    SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_ADC("ADC", NULL, SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_OUTPUT("LINEOUT"),
    SND_SOC_DAPM_INPUT("MIC")
};
```

`SND_SOC_NOPM` 表示该 widget 不关联实际电源寄存器（dummy codec 用）。真实 codec 中应指定寄存器地址和电源位。

### DAPM route（音频路由）

```c
static const struct snd_soc_dapm_route my_dummy_routes[] = {
    { "ADC",     NULL, "MIC" },     /* 录制：MIC → ADC */
    { "LINEOUT", NULL, "DAC" },     /* 播放：DAC → LINEOUT */
};
```

格式：`{ sink, control, source }`
- `sink`：音频流向的终点（如 ADC、LINEOUT）
- `control`：中间控件名称（NULL 表示直连，MUX/Mixer 时填控件名）
- `source`：音频流向的起点（如 MIC、DAC）

完整播放路径：`CPU DAI → DAC → LINEOUT`
完整录制路径：`MIC → ADC → CPU DAI`

## component probe 与 regmap 绑定

component probe 在 `devm_snd_soc_register_component()` 注册后、声卡绑定前调用，是连接 I2C probe 与 ASoC 核心的桥梁。

```c
static int my_dummy_component_probe(struct snd_soc_component *component)
{
    struct my_dummy_priv *priv;

    /* 从 device drvdata 取回 I2C probe 中保存的私有数据 */
    priv = dev_get_drvdata(component->dev);

    /* 保存到 component drvdata，供后续回调使用 */
    snd_soc_component_set_drvdata(component, priv);

    /* 绑定 regmap，使控件和 DAPM 能自动访问寄存器 */
    component->regmap = priv->regmap;

    return 0;
}
```

### 为什么要在 component probe 中绑定 regmap？

- I2C probe 中 regmap 才创建，而 `snd_soc_component_driver` 是静态 const 结构，无法在定义时设置 regmap
- 绑定后，`SOC_SINGLE_TLV` 等控件自动通过 regmap 读写寄存器
- DAPM widget 的电源位也通过 regmap 控制
- 4.19 中直接赋值 `component->regmap` 是可行的；5.x+ 推荐用 `snd_soc_component_init_regmap()`

## I2C probe 完整流程

```
1. devm_kzalloc() 分配私有数据结构
2. devm_regmap_init_i2c() 创建 regmap 实例
3. （可选）regmap_cache_only() 切换纯模拟模式
4. i2c_set_clientdata() 保存私有数据到 device drvdata
5. devm_snd_soc_register_component() 注册 ASoC component
   └─ 内部调用 component probe → 绑定 regmap 到 component
```

```c
static int my_dummy_codec_probe(struct i2c_client *client,
                                 const struct i2c_device_id *id)
{
    struct my_dummy_priv *priv;
    int ret;

    priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv) return -ENOMEM;

    priv->regmap = devm_regmap_init_i2c(client, &my_dummy_regmap_config);
    if (IS_ERR(priv->regmap)) return PTR_ERR(priv->regmap);

    i2c_set_clientdata(client, priv);

    ret = devm_snd_soc_register_component(&client->dev,
                                           &my_dummy_component,
                                           my_dummy_dai, 1);
    return ret;
}
```

## 设备树绑定

### I2C codec 节点

```dts
&i2c1 {
    status = "okay";
    clock-frequency = <400000>;

    my_i2c_codec: i2c-dummy-codec@1a {
        compatible = "my,i2c-dummy-codec";
        reg = <0x1a>;              /* I2C 7位地址 */
        #sound-dai-cells = <0>;    /* 0 = 单 DAI */
    };
};
```

### 机器驱动节点（simple-audio-card）

```dts
sound {
    compatible = "simple-audio-card";
    simple-audio-card,name = "my-audio-card";
    simple-audio-card,format = "i2s";
    simple-audio-card,bitclock-master = <&cpu_dai>;
    simple-audio-card,frame-master = <&cpu_dai>;

    cpu_dai: simple-audio-card,cpu {
        sound-dai = <&i2s0>;       /* RK3568 I2S 控制器 */
    };

    simple-audio-card,codec {
        sound-dai = <&my_i2c_codec>;
    };
};
```

### RK3568 I2S 控制器启用

```dts
&i2s0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&i2s0m0_clk    /* BCLK */
                 &i2s0m0_fs     /* LRCK */
                 &i2s0m0_sdo    /* 数据输出 */
                 &i2s0m0_sdi>;  /* 数据输入 */
};
```

## 4.19 注意事项

| 特性 | 4.19 行为 |
|------|----------|
| `devm_regmap_init_i2c()` | 可用 |
| `devm_snd_soc_register_component()` | 可用（从 4.18 引入） |
| `component->regmap` 直接赋值 | 可用（推荐在 component probe 中赋值） |
| `snd_soc_component_set_drvdata()` | 可用 |
| `SOC_SINGLE_TLV` / `DECLARE_TLV_DB_SCALE` | 可用 |
| DAPM widget / route | 完全支持 |
| DAI `.name` 字段 | 可不设置，避免部分老内核匹配问题 |
| `i2c_driver.probe` 签名 | 双参数 `(client, id)` |
| `REGCACHE_RBTREE` / `REGCACHE_FLAT` | 可用（`REGCACHE_MAPLE` 5.x+ 才有） |

> 注意：4.19 之前的内核中，codec 驱动使用 `snd_soc_codec_driver` 和 `snd_soc_register_codec()`，而非 `snd_soc_component_driver`。4.19 已统一为 component 框架。

## 调试技巧

### 查看声卡和控件

```bash
cat /proc/asound/cards          # 查看已注册的声卡
aplay -l                        # 列出播放设备
arecord -l                      # 列出录制设备
amixer -c 0 contents            # 查看声卡0的所有控件
amixer -c 0 sget "Playback Volume"  # 查看指定控件
alsamixer -c 0                  # 图形化控件界面
```

### 查看 DAPM 状态

```bash
# 需挂载 debugfs
mount -t debugfs none /sys/kernel/debug

# 查看 DAPM widget 状态
cat /sys/kernel/debug/asoc/<card-name>/<codec-name>/dapm/widgets

# 查看音频路由
cat /sys/kernel/debug/asoc/<card-name>/<codec-name>/dapm/routes
```

### 查看 regmap 寄存器

```bash
# regmap debugfs（需 CONFIG_REGMAP_DEBUG_FS）
cat /sys/kernel/debug/regmap/<device-name>/registers   # 所有寄存器
cat /sys/kernel/debug/regmap/<device-name>/cache       # 缓存内容
```

### 常见问题

| 现象 | 可能原因 | 解决 |
|------|---------|------|
| I2C probe 不执行 | 设备树 compatible 不匹配或 I2C 地址错误 | 检查 `compatible` 和 `reg`；用 `i2cdetect -y 1` 扫描设备 |
| component 注册成功但无控件 | component probe 中未绑定 regmap | 确认 `component->regmap = priv->regmap` 已执行 |
| 控件读写失败（I/O error） | regmap 物理访问失败，硬件无应答 | 检查 I2C 接线、上拉电阻、芯片供电；或使能 cache-only 模式 |
| `amixer` 无 Playback Volume | 控件未注册或名称不匹配 | 检查 `controls` 数组和 `num_controls`；用 `amixer contents` 查看实际名称 |
| DAPM 路径不通 | route 定义错误或 widget 名称不匹配 | 检查 `sink`/`source` 名称是否与 widget 完全一致；查看 debugfs 的 route 图 |
| 播放有数据但无声 | codec 未上电或音量为 0 | 用 `amixer` 设置音量；检查 DAPM widget 电源状态；确认 `set_sysclk` 配置正确 |
| regmap 初始化失败 | `regmap_config` 配置错误或 I2C 适配器不支持 | 检查 `reg_bits`/`val_bits`；确认 I2C 适配器已启用 |

### printk 调试

在关键路径添加打印：

```c
dev_info(&client->dev, "i2c codec probe, addr=0x%02x\n", client->addr);
dev_info(&client->dev, "regmap=%p\n", priv->regmap);
dev_info(component->dev, "component probe, regmap=%p\n", priv->regmap);
```

查看日志：

```bash
dmesg | grep -i "codec\|asoc\|sound\|regmap"
```
