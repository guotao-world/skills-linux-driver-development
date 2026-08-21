/*
 * ASoC dummy codec 驱动模板
 *
 * 功能：实现一个最简的 ASoC codec 驱动，仅注册 DAI 和 component，
 *       不做任何实际硬件控制，用于验证 ASoC 音频通路和机器驱动匹配。
 *
 * 目标内核：Linux 4.19
 * 架构：ARM64（RK3568 等）
 *
 * 配套设备树节点：
 *   my_dummy_codec: dummy-codec {
 *       compatible = "my,dummy-codec";
 *       #sound-dai-cells = <0>;
 *   };
 *
 * 机器驱动中通过 simple-audio-card 引用：
 *   sound {
 *       compatible = "simple-audio-card";
 *       simple-audio-card,cpu {
 *           sound-dai = <&i2s0>;
 *       };
 *       simple-audio-card,codec {
 *           sound-dai = <&my_dummy_codec>;
 *       };
 *   };
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/soc.h>

#define DRIVER_NAME "my-dummy-codec"

/*
 * 1. 定义 codec 的 DAI 能力
 *
 * snd_soc_dai_driver 描述一个数字音频接口（DAI）的能力：
 * - playback: 播放方向支持的声道数、采样率、数据格式
 * - capture:  录制方向支持的声道数、采样率、数据格式
 * - ops:      DAI 操作函数集（hw_params、set_fmt 等，可选）
 */
static struct snd_soc_dai_driver my_dummy_dai = {
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
};

/*
 * 2. component 描述
 *
 * snd_soc_component_driver 是 ASoC 中 codec/platform/machine
 * 三层共用的组件描述结构。codec 驱动中只需设置 name，
 * 其余回调（probe、remove、controls 等）按需实现。
 */
static const struct snd_soc_component_driver my_dummy_component = {
    .name = DRIVER_NAME,
};

/*
 * 3. probe 函数
 *
 * 设备树匹配成功后调用，核心工作是注册 component 和 DAI。
 * 使用 devm_snd_soc_register_component() 注册的资源会在
 * 设备 detach 时自动释放，无需手动清理。
 *
 * 注意：devm_snd_soc_register_component() 从 Linux 4.18 开始可用，
 * 4.19 完全支持。更早内核需用 snd_soc_register_component()。
 */
static int my_dummy_codec_probe(struct platform_device *pdev)
{
    int ret;

    dev_info(&pdev->dev, "my dummy codec probe\n");

    ret = devm_snd_soc_register_component(&pdev->dev,
                                           &my_dummy_component,
                                           &my_dummy_dai,
                                           1);
    if (ret < 0) {
        dev_err(&pdev->dev,
                "failed to register component: %d\n", ret);
        return ret;
    }

    dev_info(&pdev->dev, "component registered successfully\n");
    return 0;
}

/*
 * 4. 设备树匹配表
 */
static const struct of_device_id my_dummy_codec_of_match[] = {
    { .compatible = "my,dummy-codec", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_dummy_codec_of_match);

/*
 * 5. platform driver
 *
 * ASoC codec 驱动通常基于 platform bus，通过设备树 compatible 匹配。
 */
static struct platform_driver my_dummy_codec_driver = {
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = my_dummy_codec_of_match,
    },
    .probe = my_dummy_codec_probe,
};
module_platform_driver(my_dummy_codec_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal ASoC dummy codec driver (Linux 4.19)");
MODULE_AUTHOR("Your Name");
