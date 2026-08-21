/*
 * ASoC I2C dummy codec 驱动模板
 *
 * 功能：实现一个通过 I2C 总线连接的最简 ASoC codec 驱动，
 *       包含 regmap 寄存器映射、音量控件、DAPM 电源域和音频路由。
 *       用于验证 I2C 音频通路、regmap 物理访问和机器驱动匹配。
 *
 * 目标内核：Linux 4.19
 * 架构：ARM64（RK3568 等）
 *
 * 与 platform dummy codec 的区别：
 *   - 基于 I2C 总线（i2c_driver），而非 platform bus
 *   - 使用 regmap 子系统管理寄存器访问（支持缓存）
 *   - 包含 ALSA 控件（Playback Volume）
 *   - 包含 DAPM widget 和 route（DAC/ADC/LINEOUT/MIC）
 *   - component probe 中绑定 regmap
 *
 * 配套设备树节点：
 *   &i2c1 {
 *       status = "okay";
 *       clock-frequency = <400000>;
 *
 *       my_i2c_codec: i2c-dummy-codec@1a {
 *           compatible = "my,i2c-dummy-codec";
 *           reg = <0x1a>;           // I2C 7位地址
 *           #sound-dai-cells = <0>;
 *       };
 *   };
 *
 * 机器驱动中通过 simple-audio-card 引用：
 *   sound {
 *       compatible = "simple-audio-card";
 *       simple-audio-card,name = "my-audio-card";
 *       simple-audio-card,format = "i2s";
 *       simple-audio-card,bitclock-master = <&cpu_dai>;
 *       simple-audio-card,frame-master = <&cpu_dai>;
 *
 *       cpu_dai: simple-audio-card,cpu {
 *           sound-dai = <&i2s0>;
 *       };
 *
 *       simple-audio-card,codec {
 *           sound-dai = <&my_i2c_codec>;
 *       };
 *   };
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#define DRIVER_NAME "i2c-dummy-codec"

/*
 * 驱动私有数据结构
 *
 * 保存 regmap 指针和设备指针，在 I2C probe 中初始化，
 * 通过 i2c_set_clientdata() 保存，在 component probe 中通过
 * dev_get_drvdata() 取回。
 */
struct my_dummy_priv {
    struct regmap *regmap;
    struct device *dev;
};

/*
 * 寄存器默认值表
 *
 * regmap 初始化时会将这些默认值加载到缓存中。
 * 格式：{ 寄存器地址, 默认值 }
 *
 * 0x20 寄存器用作 Playback Volume，范围 0~63，默认 63（最大音量）。
 */
static const struct reg_default my_dummy_reg_defaults[] = {
    { 0x20, 63 },
};

/*
 * regmap 可写寄存器回调
 *
 * 返回 true 表示该寄存器可写。实际驱动中可根据寄存器地址
 * 做精细控制（如只读寄存器返回 false）。
 */
static bool my_dummy_writeable_reg(struct device *dev, unsigned int reg)
{
    return true;
}

/*
 * regmap 可读寄存器回调
 */
static bool my_dummy_readable_reg(struct device *dev, unsigned int reg)
{
    return true;
}

/*
 * regmap 配置结构
 *
 * - reg_bits: 寄存器地址位数（8位 = 256个寄存器）
 * - val_bits: 寄存器数据位数（8位）
 * - max_register: 最大寄存器地址
 * - reg_defaults: 寄存器默认值表
 * - cache_type: 缓存类型（REGCACHE_RBTREE = 红黑树缓存）
 * - writeable_reg/readable_reg: 可写/可读回调
 *
 * 注意：此处不调用 regmap_cache_only()，保持物理 I2C 访问。
 *       如果需要纯模拟（不访问真实硬件），可在 probe 中调用
 *       regmap_cache_only(regmap, true) 使所有读写走缓存。
 */
static const struct regmap_config my_dummy_regmap_config = {
    .reg_bits         = 8,
    .val_bits         = 8,
    .max_register     = 0xff,
    .reg_defaults     = my_dummy_reg_defaults,
    .num_reg_defaults = ARRAY_SIZE(my_dummy_reg_defaults),
    .cache_type       = REGCACHE_RBTREE,
    .writeable_reg    = my_dummy_writeable_reg,
    .readable_reg     = my_dummy_readable_reg,
};

/*
 * DAI 操作：设置系统时钟
 *
 * 机器驱动或用户空间设置采样率时会调用此回调。
 * 实际 codec 中需根据 freq 配置 PLL 和分频器。
 * dummy codec 仅打印日志，不做实际硬件操作。
 */
static int my_dummy_set_sysclk(struct snd_soc_dai *dai,
                                int clk_id, unsigned int freq, int dir)
{
    dev_info(dai->dev, "dummy codec set_sysclk freq=%u\n", freq);
    return 0;
}

/*
 * DAI 操作函数集
 *
 * 可选回调：hw_params、set_fmt、set_clkdiv、set_sysclk、
 *           digital_mute、set_bias_level 等。
 */
static const struct snd_soc_dai_ops my_dummy_dai_ops = {
    .set_sysclk = my_dummy_set_sysclk,
};

/*
 * DAI 驱动描述
 *
 * 注意：.name 字段在 4.19 中可以不设置（注释掉），
 * 不设置时 DAI 名称由 ASoC 核心自动生成（component name + 索引）。
 * 部分老内核中设置 .name 后可能导致机器驱动无法匹配 DAI，
 * 因此最简 dummy codec 建议不设置 .name。
 */
static struct snd_soc_dai_driver my_dummy_dai[] = {
    {
        /* .name = "my-codec", */  /* 4.19 可不设置，避免匹配问题 */
        .playback = {
            .stream_name  = "Playback",
            .channels_min = 1,
            .channels_max = 2,
            .rates        = SNDRV_PCM_RATE_8000,
            .formats      = SNDRV_PCM_FMTBIT_S16_LE,
        },
        .capture = {
            .stream_name  = "Capture",
            .channels_min = 1,
            .channels_max = 2,
            .rates        = SNDRV_PCM_RATE_8000,
            .formats      = SNDRV_PCM_FMTBIT_S16_LE,
        },
        .ops = &my_dummy_dai_ops,
    }
};

/*
 * TLV（Type-Length-Value）分贝缩放信息
 *
 * DECLARE_TLV_DB_SCALE(name, min, step, mute)
 *   - min:  最小分贝值 * 100（-6300 = -63.00 dB）
 *   - step: 每步分贝值 * 100（100 = 1.00 dB）
 *   - mute: 是否支持静音（0 = 不支持）
 *
 * 配合 SOC_SINGLE_TLV 使用，用户空间 amixer 会显示分贝值而非原始寄存器值。
 */
static const DECLARE_TLV_DB_SCALE(dac_tlv, -6300, 100, 0);

/*
 * ALSA 控件（kcontrol）数组
 *
 * SOC_SINGLE_TLV(name, reg, shift, max, invert, tlv)
 *   - name:   控件名称（用户空间 amixer 中显示）
 *   - reg:    关联的寄存器地址（0x20）
 *   - shift:  寄存器中值的偏移位（0 = 从 bit0 开始）
 *   - max:    最大值（63 = 6位分辨率）
 *   - invert: 是否反转（0 = 不反转）
 *   - tlv:    TLV 分贝缩放信息
 *
 * 控件通过 component->regmap 自动读写寄存器，无需手动实现 get/put 回调。
 */
static const struct snd_kcontrol_new my_dummy_controls[] = {
    SOC_SINGLE_TLV("Playback Volume", 0x20, 0, 63, 0, dac_tlv)
};

/*
 * DAPM（动态音频电源管理）widget 数组
 *
 * DAPM 自动管理音频路径上各部件的电源，仅在使用时上电。
 *
 * SND_SOC_DAPM_DAC(name, stream_name, reg, shift, invert)
 *   - DAC 数模转换器，连接播放路径
 * SND_SOC_DAPM_ADC(name, stream_name, reg, shift, invert)
 *   - ADC 模数转换器，连接录制路径
 * SND_SOC_DAPM_OUTPUT(name) - 物理输出引脚（如 LINEOUT）
 * SND_SOC_DAPM_INPUT(name)  - 物理输入引脚（如 MIC）
 *
 * SND_SOC_NOPM 表示该 widget 不关联实际电源寄存器（dummy codec 用）。
 */
static const struct snd_soc_dapm_widget my_dummy_widgets[] = {
    SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_ADC("ADC", NULL, SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_OUTPUT("LINEOUT"),
    SND_SOC_DAPM_INPUT("MIC")
};

/*
 * DAPM 音频路由（route）数组
 *
 * 格式：{ sink, control, source }
 *   - sink:    目标 widget（音频流向的终点）
 *   - control: 中间控件名称（NULL 表示直连）
 *   - source:  源 widget（音频流向的起点）
 *
 * 播放路径：DAC → LINEOUT
 * 录制路径：MIC → ADC
 */
static const struct snd_soc_dapm_route my_dummy_routes[] = {
    { "ADC",     NULL, "MIC" },     /* 录制：MIC → ADC */
    { "LINEOUT", NULL, "DAC" },     /* 播放：DAC → LINEOUT */
};

/*
 * ASoC component probe
 *
 * 在 component 注册后、声卡绑定前调用。
 * 核心工作：从 I2C client 的 drvdata 中取回私有数据，
 * 将 regmap 绑定到 component，使控件和 DAPM 能自动访问寄存器。
 */
static int my_dummy_component_probe(struct snd_soc_component *component)
{
    struct my_dummy_priv *priv;

    priv = dev_get_drvdata(component->dev);
    dev_info(component->dev, "component probe regmap=%p\n", priv->regmap);

    snd_soc_component_set_drvdata(component, priv);

    /*
     * 将 regmap 绑定到 component。
     * 绑定后，SOC_SINGLE_TLV 等控件会自动通过 regmap 读写寄存器，
     * DAPM widget 也能通过 regmap 控制电源位。
     *
     * 4.19 中直接赋值 component->regmap 是可行的。
     * 更规范的方式是在 snd_soc_component_driver 中设置 .regmap 字段，
     * 但 I2C codec 的 regmap 在 probe 中才创建，因此在 component probe
     * 中赋值是常见做法。
     */
    component->regmap = priv->regmap;

    return 0;
}

/*
 * ASoC component 驱动描述
 *
 * 包含：名称、控件、DAPM widget、DAPM route、probe 回调。
 * 这是 I2C codec 与 platform dummy codec 的主要区别——
 * platform 版本只有 .name，I2C 版本有完整的控件和 DAPM。
 */
static const struct snd_soc_component_driver my_dummy_component = {
    .name               = DRIVER_NAME,
    .controls           = my_dummy_controls,
    .num_controls       = ARRAY_SIZE(my_dummy_controls),
    .dapm_widgets       = my_dummy_widgets,
    .num_dapm_widgets   = ARRAY_SIZE(my_dummy_widgets),
    .dapm_routes        = my_dummy_routes,
    .num_dapm_routes    = ARRAY_SIZE(my_dummy_routes),
    .probe              = my_dummy_component_probe,
};

/*
 * I2C 设备 probe
 *
 * I2C 核心匹配到设备后调用，核心工作：
 * 1. 分配私有数据结构
 * 2. 初始化 regmap（devm_regmap_init_i2c）
 * 3. 保存私有数据到 client->dev 的 drvdata
 * 4. 注册 ASoC component（devm_snd_soc_register_component）
 *
 * 注意：4.19 的 i2c probe 签名为双参数
 * (struct i2c_client *client, const struct i2c_device_id *id)。
 */
static int my_dummy_codec_probe(struct i2c_client *client,
                                 const struct i2c_device_id *id)
{
    struct my_dummy_priv *priv;
    int ret;

    dev_info(&client->dev, "dummy i2c codec probe\n");

    /* 1. 分配私有数据 */
    priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    priv->dev = &client->dev;

    /* 2. 初始化 regmap（devm 托管，自动释放） */
    priv->regmap = devm_regmap_init_i2c(client, &my_dummy_regmap_config);
    if (IS_ERR(priv->regmap)) {
        ret = PTR_ERR(priv->regmap);
        dev_err(&client->dev, "failed to init regmap: %d\n", ret);
        return ret;
    }

    /*
     * 此处不调用 regmap_cache_only()，保持物理 I2C 访问。
     * 如果需要纯模拟模式（不访问真实 I2C 硬件），取消下一行注释：
     * regmap_cache_only(priv->regmap, true);
     */

    /* 3. 保存私有数据到 device drvdata，供 component probe 取回 */
    i2c_set_clientdata(client, priv);

    /* 4. 注册 ASoC component（devm 托管，自动释放） */
    ret = devm_snd_soc_register_component(&client->dev,
                                           &my_dummy_component,
                                           my_dummy_dai, 1);
    if (ret < 0) {
        dev_err(&client->dev,
                "failed to register component: %d\n", ret);
        return ret;
    }

    dev_info(&client->dev, "component registered successfully\n");
    return 0;
}

/*
 * 设备树匹配表
 */
static const struct of_device_id my_dummy_codec_of_match[] = {
    { .compatible = "my,i2c-dummy-codec", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_dummy_codec_of_match);

/*
 * 传统 I2C ID 匹配表（非设备树方式，保留以兼容）
 */
static const struct i2c_device_id my_dummy_i2c_id[] = {
    { "i2c-dummy-codec", 0 },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, my_dummy_i2c_id);

/*
 * I2C 驱动结构
 */
static struct i2c_driver my_dummy_codec_driver = {
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = my_dummy_codec_of_match,
    },
    .probe    = my_dummy_codec_probe,
    .id_table = my_dummy_i2c_id,
};
module_i2c_driver(my_dummy_codec_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal ASoC I2C dummy codec driver with regmap/kcontrol/DAPM (Linux 4.19)");
MODULE_AUTHOR("Your Name");
