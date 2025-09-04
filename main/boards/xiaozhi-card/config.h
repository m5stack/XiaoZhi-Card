#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>


/* ---------------------------------------------------------------- */
// Config 
#define BAT_VOL_EMPTY 3.4  // 4G模组 ML307R 供电最小值 3.4V，瞬态工作电流能达到2A
#define BAT_VOL_LOW   3.6  // 电池电压低于 3.5V 提示需要充电
#define BAT_VOL_FULL  4.2  // 满电电压 

/* ---------------------------------------------------------------- */
// Systerm I2C
#define SYS_I2C_PIN_SCL GPIO_NUM_1
#define SYS_I2C_PIN_SDA GPIO_NUM_2

/* ---------------------------------------------------------------- */
// I2C device address
#define I2C_ADDR_ES8311  0x18  // Audio CODEC
#define I2C_ADDR_AW32001 0x49  // Baterry Charger
#define I2C_ADDR_FT6336  0x38  // CTP
#define I2C_ADDR_BMI270  0x68  // IMU
#define I2C_ADDR_BQ27220 0x55  // Baterry Gauge

/* ---------------------------------------------------------------- */
// Audio CODEC ES8311
#define AUDIO_INPUT_REFERENCE    true
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_PIN_MCLK GPIO_NUM_3
#define AUDIO_I2S_PIN_WS   GPIO_NUM_6
#define AUDIO_I2S_PIN_BCLK GPIO_NUM_4
#define AUDIO_I2S_PIN_DIN  GPIO_NUM_5
#define AUDIO_I2S_PIN_DOUT GPIO_NUM_7

#define AUDIO_CODEC_ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR  // 0x30

// PA AW8737A
#define AUDIO_PIN_PA GPIO_NUM_8 // 电阻下拉，默认为关闭 

/* ---------------------------------------------------------------- */
// 显示屏相关参数配置
// #define EPD_PIN_INT  GPIO_NUM_16  // 触摸 FT6336 中断
#define EPD_PIN_BUSY GPIO_NUM_48  // 忙检查
#define EPD_PIN_RST  GPIO_NUM_47  // 复位
#define EPD_PIN_DC   GPIO_NUM_41  // 数据/命令选择
#define EPD_PIN_CS   GPIO_NUM_42  // 片选
#define EPD_PIN_SCK  GPIO_NUM_45  // SPI SCK
#define EPD_PIN_MOSI GPIO_NUM_46  // SPI MOSI
#define EPD_PIN_MISO GPIO_NUM_13  // SPI MISO nand flash  
#define EPD_SPI_HOST SPI2_HOST

#define EPD_RES_WIDTH  176  // 水平分辨率（176） 
#define EPD_RES_HEIGHT 264  // 垂直分辨率（264）

#define DISPLAY_WIDTH    EPD_RES_WIDTH  
#define DISPLAY_HEIGHT   EPD_RES_HEIGHT
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  false
#define DISPLAY_OFFSET_X 0 
#define DISPLAY_OFFSET_Y 0

/* ---------------------------------------------------------------- */
// 4G Module ML307R
#define ML307R_PIN_RX     GPIO_NUM_11  // UART0_RX
#define ML307R_PIN_TX     GPIO_NUM_9   // UART0_TX
#define ML307R_PIN_PWR    GPIO_NUM_39  // 电源控制
#define ML307R_PIN_RST    GPIO_NUM_40  // 复位
#define ML307R_PIN_DTR    GPIO_NUM_10  // 休眠控制
#define ML307R_PIN_WAKEUP GPIO_NUM_38  // 休眠状态输出
// #define ML307R_PIN_RTS GPIO_NUM_NC
// #define ML307R_PIN_CTS GPIO_NUM_NC

/* ---------------------------------------------------------------- */
// Others
#define USER_BUTTON_GPIO GPIO_NUM_21 // 背部按键
#define TOUCH_INT_GPIO  GPIO_NUM_16  // 触摸中断
#define IMU_INT_GPIO    GPIO_NUM_12  // IMU 中断
#define USB_SWC_GPIO    GPIO_NUM_15  // USB 切换（连接外部 USB 或 ML307R 模组，默认低电平连接外部 USB，高电平切换到 ML307R）
#define LED_GPIO        GPIO_NUM_0   // 底座 LED

#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_NC
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

#endif  // _BOARD_CONFIG_H_
