#include "aw32001.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define AW32001_REG_PWR_CFG 0x01
#define AW32001_REG_CHR_CUR 0x02
#define AW32001_REG_CHR_VOL 0x04
#define AW32001_REG_SYS_STA 0x08

#define I2C_MASTER_TIMEOUT_MS 200

Aw32001::Aw32001(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev_handle));

    SetWatchdog(0, false); // disable watchdog timer

    uint8_t val = ReadReg(0x09);
    WriteReg(0x09, val & 0x3F); // Enter the shipping mode deglitch time 1S

    SetIntWakeupTime(1); // 设置 INT 低电平唤醒（退出 shipping mode）时间为 100ms
}

/** 
 * 设置 INT 引脚退出休眠的唤醒时间
 * @sel 1 为 100ms，其他为 2s（系统默认值）
 */
void Aw32001::SetIntWakeupTime(uint8_t sel)
{
    uint8_t val = ReadReg(0x22);
    if (sel) {
        val |= (1 << 3);
    } else {
        val &= ~(1 << 3);
    }
    WriteReg(0x22, val);
}

/**
 * 设置充电看门狗超时时间和放电看门狗状态
 * @note 开启看门狗并且超时，会中断充电/放电
 * @param timeout_s 超时时间（单位：秒），可选值为 0（关闭看门狗）、40、80、160
 * @param enable_discharge_watchdog 是否开启放电看门狗（1：开启，0：关闭）
 */
void Aw32001::SetWatchdog(uint16_t timeout_s, uint8_t enable_discharge_watchdog)
{
    uint8_t bits = 0;
    switch (timeout_s) {
        case 0: bits = 0b00; break;
        case 40: bits = 0b01; break;
        case 80: bits = 0b10; break;
        case 160: bits = 0b11; break;
        default: return;
    }
    uint8_t reg = ReadReg(0x05);
    if (enable_discharge_watchdog) {
        reg |= (1 << 7);
    } else {
        reg &= ~(1 << 7);
    }
    reg &= ~(0b11 << 5);
    reg |= (bits << 5);
    WriteReg(0x05, reg);
}

/**
 * 复位（喂狗）看门狗计时器
 */
void Aw32001::ResetWatchdog()
{
    uint8_t reg = ReadReg(0x02);
    reg |= (1 << 6);
    WriteReg(0x02, reg);
}

/**
 * 设置充电 
 * @en true: 开启充电，false: 关闭充电
 */
void Aw32001::SetCharge(bool en)
{
    uint8_t reg = ReadReg(AW32001_REG_PWR_CFG);
    if (en) {
        reg &= ~(1 << 3);
    } else {
        reg |= (1 << 3);
    }
    reg |= 0x07; // VBAT_UVLO 设为 3.03V 
    WriteReg(AW32001_REG_PWR_CFG, reg);
}

/**
 * 设置充电电流(默认值为 128 mA)
 * @current_ma 充电电流(mA)，范围 0 ~ 456 mA (步距值为 8，即实际值 0, 8, 16, ...)。
 */
void Aw32001::SetChargeCurrent(uint16_t current_ma)
{
    if (current_ma > 456) current_ma = 456;
    uint8_t reg = ReadReg(0x02);
    reg = (reg & 0xC0) | ((current_ma / 8) & 0x3F);
    WriteReg(0x02, reg);
}

/**
 * 设置放电电流(默认值为 2000 mA)
 * @current_ma 放电电流(mA)，范围 200 ~ 3200 mA (步距值为 200，即实际值 200, 400, 600 ...)。
 */
void Aw32001::SetDischargeCurrent(uint16_t current_ma)
{
    if (current_ma < 200) current_ma = 200;
    if (current_ma > 3200) current_ma = 3200;
    uint8_t level = current_ma / 200 - 1;
    uint8_t val = ReadReg(0x03);
    val &= 0x0F;
    val |= (level << 4);
    WriteReg(0x03, val);
}

/**
 * 设置电池充满时的电压上限 
 * @voltage_mv 充电电压(mV)，范围 3600 ~ 4545 mV (步距值为 15，即实际值 3600, 3615, 3630 ...)。 
 */
void Aw32001::SetChargeVoltage(uint16_t voltage_mv)
{
    if (voltage_mv < 3600) voltage_mv = 3600;
    if (voltage_mv > 4545) voltage_mv = 4545;
    uint8_t value = (voltage_mv - 3600) / 15;
    uint8_t reg = ReadReg(0x04);
    reg = (reg & 0x03) | (value << 2);
    WriteReg(0x04, reg);
}

/**
 * 设置运输模式（适合产品长期存放，电池漏电流小于20μA） 
 * @note: 长按主机背部按键（时间 2秒/100ms 可配置），或重新插拔 USB，可退出运输模式。
 * @en true: 进入运输模式，false: 关闭运输模式(手动退出无效)
 */
void Aw32001::SetShippingMode(bool en)
{
    uint8_t val = 0;
 
    //printf("\nEnable INT Pin function durning SHIPPING mode \n");
    val = ReadReg(0x0C);
    val &= 0x0B;
    WriteReg(0x0C, val);

    val = ReadReg(0x06);
    if (en) {
        val |= (1 << 5); // FET_DIS = 1
    } else {
        val &= ~(1 << 5);
        for (uint8_t i = 0; i < 10; i++) {
            val = ReadReg(0x06);
            if (val == 0x40) { 
                break;
            }
            printf("\n AW32001 exit shipping mode faild!\n");
            vTaskDelay(pdMS_TO_TICKS(100));
            // WriteReg(0x06, 0x40); // 手动退出无效 
        }
    }
    WriteReg(0x06, val);
}

/**
 * 获取充电状态 
 * @return 充电状态
 *    0: 没有充电
 *    1: 预充电
 *    2: 充电中
 *    3: 充电完成
 */
uint8_t Aw32001::GetChargeState()
{
    uint8_t reg = ReadReg(0x08);
    return (reg >> 3) & 0x03;
}

/**
 * 设置预充电电流 
 * @param current_ma 预充电电流，单位 mA，范围 1~31，步进 2mA（即1,3,5...31）
 */
void Aw32001::SetPreChargeCurrent(uint16_t current_ma)
{
    if (current_ma < 1) current_ma = 1;
    if (current_ma > 31) current_ma = 31;
    uint8_t level = (current_ma) / 2 << 1; // 1mA对应0，3mA对应1，... 31mA对应15
    uint8_t val = ReadReg(0x0B);           
    val &= 0xE1;                           
    val |= (level & 0x1E);          
    val |= 0x01; // USB 插入检测消抖时间 100ms         
    WriteReg(0x0B, val);      
}

/**
 * 设置预充转快充阈值
 * @sel: 0 为 2.8V，1 为 3.0V (复位后默认值)
 */
void Aw32001::SetPrechargeToFastchargeThreshold(uint8_t sel)
{
    uint8_t val = ReadReg(0x04);
    if (sel == 0) {
        val &= ~(1 << 1);  
    } else if (sel == 1) {
        val |= (1 << 1);    
    }
    val &= 0xFE; // recharge threshold 100mV
    WriteReg(0x04, val);
}

/**
 * 设置 NTC 功能 
 * @en true: 开启 NTC，false: 关闭 NTC
 */
void Aw32001::SetNtcFunction(bool en)
{
    uint8_t reg = ReadReg(0x06);
    if (en) {
        reg |= (1 << 7);
    } else {
        reg &= ~(1 << 7);
    }
    WriteReg(0x06, reg);
}

/** 
 * 打印寄存器值
 * @start 起始地址 
 * @end 结束地址 
 */
void Aw32001::DumpRegs(uint8_t start, uint8_t end)
{
    if (start > end || end > 0x0C) {
        printf("Invalid address range: 0x%02X - 0x%02X\n", start, end);
        return;
    }

    for (uint8_t addr = start; addr <= end; addr++) {
        uint8_t val = ReadReg(addr);
        printf("Reg 0x%02X: 0b", addr);
        for (int i = 7; i >= 0; i--) {
            printf("%d", (val >> i) & 0x01);
        }
        printf(" (0x%02X)\n", val);
    }
}

void Aw32001::WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t write_buf[] = {reg, value};
    i2c_master_transmit(i2c_dev_handle, write_buf, 2, I2C_MASTER_TIMEOUT_MS);  
}

uint8_t Aw32001::ReadReg(uint8_t reg)
{
    uint8_t value;
    i2c_master_transmit_receive(i2c_dev_handle, &reg, 1, &value, 1, I2C_MASTER_TIMEOUT_MS);
    return value;
}
