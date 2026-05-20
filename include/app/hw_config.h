#ifndef APP_HW_CONFIG_H_
#define APP_HW_CONFIG_H_

#include <driver/i2c_types.h>
#include <driver/spi_common.h>

#define APP_LCD_WIDTH                240
#define APP_LCD_HEIGHT               320
#define APP_LCD_SPI_HOST             SPI2_HOST
#define APP_LCD_PIXEL_CLOCK_HZ       (40 * 1000 * 1000)

#define APP_PIN_LCD_SCLK             12
#define APP_PIN_LCD_MOSI             11
#define APP_PIN_LCD_CS               10
#define APP_PIN_LCD_DC               4
#define APP_PIN_LCD_RST              8
#define APP_PIN_BACKLIGHT_PWM        9

#define APP_PIN_BH1750_SDA           1
#define APP_PIN_BH1750_SCL           2
#define APP_BH1750_I2C_PORT          I2C_NUM_0
#define APP_BH1750_I2C_HZ            400000
#define APP_BH1750_I2C_ADDR          0x23

#define APP_PIN_DHT11_DATA           3

#define APP_PIN_LD2410_UART_TX       17
#define APP_PIN_LD2410_UART_RX       18
#define APP_PIN_LD2410_OUT           16
#define APP_LD2410_UART_BAUDRATE     256000

#define APP_PIN_I2S_BCLK             13
#define APP_PIN_I2S_WS               14
#define APP_PIN_I2S_DOUT             21

#define APP_PIN_KEY1                 41
#define APP_PIN_KEY2                 42
#define APP_PIN_KEY3                 40
#define APP_PIN_KEY4                 45

#endif
