/*
 * I2C 客户端驱动模板
 * 功能：I2C 传感器驱动框架，包含寄存器读写
 *
 * 配套设备树节点：
 *   &i2c1 {
 *       my_sensor@48 {
 *           compatible = "vendor,my-i2c-sensor";
 *           reg = <0x48>;
 *       };
 *   };
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define DRIVER_NAME "my-i2c-sensor"

/* 寄存器地址定义（根据实际芯片修改） */
#define REG_WHO_AM_I  0x0F
#define REG_CTRL      0x10
#define REG_DATA      0x11

struct my_i2c_data {
    struct i2c_client *client;
    /* 在此添加其他私有数据 */
};

/* 读一个寄存器 */
static int my_read_reg(struct my_i2c_data *data, u8 reg, u8 *val)
{
    s32 ret = i2c_smbus_read_byte_data(data->client, reg);
    if (ret < 0) {
        dev_err(&data->client->dev, "read reg 0x%02x failed: %d\n", reg, ret);
        return ret;
    }
    *val = (u8)ret;
    return 0;
}

/* 写一个寄存器 */
static int my_write_reg(struct my_i2c_data *data, u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(data->client, reg, val);
    if (ret < 0) {
        dev_err(&data->client->dev, "write reg 0x%02x failed: %d\n", reg, ret);
        return ret;
    }
    return 0;
}

/* 连续读多个字节 */
static int my_read_block(struct my_i2c_data *data, u8 reg, u8 len, u8 *buf)
{
    s32 ret = i2c_smbus_read_i2c_block_data(data->client, reg, len, buf);
    if (ret < 0) {
        dev_err(&data->client->dev, "read block reg 0x%02x failed: %d\n", reg, ret);
        return ret;
    }
    return 0;
}

static int my_i2c_probe(struct i2c_client *client)
{
    struct my_i2c_data *data;
    u8 whoami;
    int ret;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);

    /* 检测设备：读 WHO_AM_I 寄存器 */
    ret = my_read_reg(data, REG_WHO_AM_I, &whoami);
    if (ret)
        return ret;
    dev_info(&client->dev, "WHO_AM_I = 0x%02x\n", whoami);

    /* 初始化设备 */
    my_write_reg(data, REG_CTRL, 0x01);

    dev_info(&client->dev, "probed at addr 0x%02x\n", client->addr);
    return 0;
}

static void my_i2c_remove(struct i2c_client *client)
{
    struct my_i2c_data *data = i2c_get_clientdata(client);

    /* 关闭设备 */
    my_write_reg(data, REG_CTRL, 0x00);

    dev_info(&client->dev, "removed\n");
}

/* 设备树匹配表 */
static const struct of_device_id my_i2c_of_match[] = {
    { .compatible = "vendor,my-i2c-sensor", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_i2c_of_match);

/* 传统 I2C ID 表（非设备树系统用） */
static const struct i2c_device_id my_i2c_id[] = {
    { "my_i2c_sensor", 0 },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, my_i2c_id);

static struct i2c_driver my_i2c_driver = {
    .probe    = my_i2c_probe,
    .remove   = my_i2c_remove,
    .id_table = my_i2c_id,
    .driver   = {
        .name           = DRIVER_NAME,
        .of_match_table = my_i2c_of_match,
    },
};
module_i2c_driver(my_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("I2C client driver template");
