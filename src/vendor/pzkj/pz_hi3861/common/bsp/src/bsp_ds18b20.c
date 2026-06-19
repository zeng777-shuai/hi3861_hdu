#include "bsp_ds18b20.h"
#include <unistd.h>
#include "hi_time.h"
#include "hi_i2c.h"

#define AHT20_I2C_IDX       0
#define AHT20_ADDR          0x38

// 全局变量（只在这里定义一次）
float g_temperature = 0.0f;
float g_humidity    = 0.0f;

static void AHT20_Init_Pins(void)
{
    hi_io_set_func(HI_IO_NAME_GPIO_13, HI_IO_FUNC_GPIO_13_I2C0_SDA);
    hi_io_set_func(HI_IO_NAME_GPIO_14, HI_IO_FUNC_GPIO_14_I2C0_SCL);
    hi_i2c_init(AHT20_I2C_IDX, 400000);
    usleep(20000);
}

static void AHT20_ReadData(void)
{
    uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    uint8_t data[6] = {0};

    hi_i2c_data i2c_write_data = {0};
    i2c_write_data.send_buf   = cmd;
    i2c_write_data.send_len   = 3;
    hi_i2c_write(AHT20_I2C_IDX, AHT20_ADDR << 1, &i2c_write_data);
    usleep(80000);

    hi_i2c_data i2c_read_data = {0};
    i2c_read_data.receive_buf = data;
    i2c_read_data.receive_len = 6;
    hi_i2c_read(AHT20_I2C_IDX, (AHT20_ADDR << 1) | 1, &i2c_read_data);

    uint32_t h = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t t = ((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    g_humidity    = h * 100.0f / 1048576.0f;
    g_temperature = t * 200.0f / 1048576.0f - 50.0f;
}

void ds18b20_io_out(void){}
void ds18b20_io_in(void){}
void ds18b20_reset(void){}
uint8_t ds18b20_read_bit(void){return 0;}
uint8_t ds18b20_read_byte(void){return 0;}
void ds18b20_write_byte(uint8_t dat){}
void ds18b20_start(void){}

uint8_t ds18b20_check(void){ return 0; }

uint8_t ds18b20_init(void)
{
    AHT20_Init_Pins();
    uint8_t cmd[3] = {0xBE, 0x08, 0x00};

    hi_i2c_data i2c_data = {0};
    i2c_data.send_buf = cmd;
    i2c_data.send_len = 3;
    hi_i2c_write(AHT20_I2C_IDX, AHT20_ADDR << 1, &i2c_data);

    usleep(40000);
    return 0;
}

float ds18b20_gettemperture(void)
{
    AHT20_ReadData();
    return g_temperature;
}