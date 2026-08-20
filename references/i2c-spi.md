# I2C 与 SPI 驱动

## 目录

- [I2C 驱动框架](#i2c-驱动框架)
- [I2C 客户端驱动示例](#i2c-客户端驱动示例)
- [I2C 通信 API](#i2c-通信-api)
- [SPI 驱动框架](#spi-驱动框架)
- [SPI 通信 API](#spi-通信-api)
- [设备树绑定](#设备树绑定)

## I2C 驱动框架

I2C 设备驱动分为两层：

- **I2C 总线驱动**（adapter driver）：SoC 厂商提供，控制 I2C 控制器
- **I2C 设备驱动**（client driver）：开发者编写，控制具体 I2C 外设

我们通常写的是**客户端驱动**。

### 核心结构

```c
static struct i2c_driver my_i2c_driver = {
    .probe      = my_i2c_probe,
    .remove     = my_i2c_remove,
    .id_table   = my_i2c_id,        // 传统 ID 匹配
    .driver     = {
        .name = "my_i2c_device",
        .of_match_table = my_of_match,  // 设备树匹配
    },
};
module_i2c_driver(my_i2c_driver);
```

## I2C 客户端驱动示例

```c
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>

struct my_i2c_data {
    struct i2c_client *client;
    // 私有数据
};

static int my_i2c_probe(struct i2c_client *client)
{
    struct my_i2c_data *data;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data) return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);

    dev_info(&client->dev, "probed at addr 0x%02x\n", client->addr);
    return 0;
}

static void my_i2c_remove(struct i2c_client *client)
{
    dev_info(&client->dev, "removed\n");
}

static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-i2c-sensor", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_of_match);

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
        .name = "my_i2c_sensor",
        .of_match_table = my_of_match,
    },
};
module_i2c_driver(my_i2c_driver);

MODULE_LICENSE("GPL");
```

## I2C 通信 API

```c
// 简单读写（适合大多数传感器）
s32 i2c_smbus_read_byte_data(const struct i2c_client *client, u8 reg);
s32 i2c_smbus_write_byte_data(const struct i2c_client *client, u8 reg, u8 val);
s32 i2c_smbus_read_word_data(const struct i2c_client *client, u8 reg);
s32 i2c_smbus_write_word_data(const struct i2c_client *client, u8 reg, u16 val);
s32 i2c_smbus_read_i2c_block_data(const struct i2c_client *client, u8 reg, u8 len, u8 *buf);
s32 i2c_smbus_write_i2c_block_data(const struct i2c_client *client, u8 reg, u8 len, const u8 *buf);

// 原始 I2C 传输（适合非标准协议）
int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);

// 示例：写寄存器
i2c_smbus_write_byte_data(client, REG_CTRL, 0x01);

// 示例：读寄存器
u8 val = i2c_smbus_read_byte_data(client, REG_STATUS);

// 示例：连续读多个字节
u8 buf[4];
i2c_smbus_read_i2c_block_data(client, REG_DATA, 4, buf);
```

### 自定义 I2C 传输

```c
// 写寄存器地址 + 读数据（常见传感器读取模式）
u8 reg = 0x10;
u8 rbuf[2];
struct i2c_msg msgs[2] = {
    {
        .addr  = client->addr,
        .flags = 0,          // 写
        .len   = 1,
        .buf   = &reg,
    },
    {
        .addr  = client->addr,
        .flags = I2C_M_RD,   // 读
        .len   = 2,
        .buf   = rbuf,
    },
};
i2c_transfer(client->adapter, msgs, 2);
```

## SPI 驱动框架

### 核心结构

```c
static struct spi_driver my_spi_driver = {
    .probe      = my_spi_probe,
    .remove     = my_spi_remove,
    .id_table   = my_spi_id,
    .driver     = {
        .name = "my_spi_device",
        .of_match_table = my_of_match,
    },
};
module_spi_driver(my_spi_driver);
```

### SPI 设备配置

在 probe 中设置 SPI 模式和速度：

```c
static int my_spi_probe(struct spi_device *spi)
{
    spi->mode = SPI_MODE_0;       // CPOL=0, CPHA=0
    spi->max_speed_hz = 1000000;  // 1MHz
    spi->bits_per_word = 8;
    spi_setup(spi);
    // ...
}
```

SPI 模式：

| 模式 | CPOL | CPHA | 空闲电平 | 采样边沿 |
|------|------|------|---------|---------|
| Mode 0 | 0 | 0 | 低 | 上升沿 |
| Mode 1 | 0 | 1 | 低 | 下降沿 |
| Mode 2 | 1 | 0 | 高 | 下降沿 |
| Mode 3 | 1 | 1 | 高 | 上升沿 |

## SPI 通信 API

```c
// 简单半双工读写
int spi_write(struct spi_device *spi, const void *buf, size_t len);
int spi_read(struct spi_device *spi, void *buf, size_t len);

// 全双工传输（同时收发）
int spi_write_then_read(struct spi_device *spi,
                        const void *txbuf, unsigned n_tx,
                        void *rxbuf, unsigned n_rx);

// 自定义消息传输
int spi_sync(struct spi_device *spi, struct spi_message *msg);
int spi_sync_transfer(struct spi_device *spi,
                      struct spi_transfer *xfers, unsigned num_xfers);
```

### 示例：SPI 读写寄存器

```c
// 写命令 + 读数据（常见 SPI 传感器模式）
u8 tx[3] = { 0x80 | REG_DATA, 0x00, 0x00 };  // 读命令 +  dummy
u8 rx[3];
struct spi_transfer xfer = {
    .tx_buf = tx,
    .rx_buf = rx,
    .len    = 3,
};
struct spi_message msg;
spi_message_init(&msg);
spi_message_add_tail(&xfer, &msg);
spi_sync(spi, &msg);
// rx[1], rx[2] 为读取的数据
```

## 设备树绑定

### I2C 设备

```dts
&i2c1 {
    clock-frequency = <400000>;  // 400kHz
    status = "okay";

    my_sensor@48 {
        compatible = "vendor,my-i2c-sensor";
        reg = <0x48>;            // I2C 7位地址
        vdd-supply = <&reg_3v3>;
    };
};
```

### SPI 设备

```dts
&ecspi1 {
    status = "okay";
    cs-gpios = <&gpio3 19 GPIO_ACTIVE_LOW>;

    my_spi_dev@0 {
        compatible = "vendor,my-spi-device";
        reg = <0>;               // 片选编号
        spi-max-frequency = <10000000>;  // 10MHz
        spi-cpol;                // 可选：CPOL=1
        spi-cpha;                // 可选：CPHA=1
    };
};
```
