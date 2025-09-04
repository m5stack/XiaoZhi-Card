// ref: https://github.com/pr3y/Bruce/blob/main/lib/utility/bq27220.cpp
#include "bq27220.h"
#include "driver/i2c_master.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define I2C_MASTER_TIMEOUT_MS 200

Bq27220::Bq27220(i2c_master_bus_handle_t i2c_bus, uint8_t addr)  
{
    this->i2c_bus = i2c_bus;
    this->addr = addr; 
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = this->addr,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev_handle));
}

bool Bq27220::detect()
{
    esp_err_t ret = i2c_master_probe(this->i2c_bus, this->addr, pdMS_TO_TICKS(100));
    return (ret == ESP_OK); // 能探测到 BQ27220 表示电池正常 
}

bool Bq27220::unseal()
{
    OP_STATUS status;

    writeCtrlWord(BQ27220_UNSEAL_KEY1);
    vTaskDelay(pdMS_TO_TICKS(5));
    writeCtrlWord(BQ27220_UNSEAL_KEY2);
    vTaskDelay(pdMS_TO_TICKS(5));
    status = OP_STATUS(readWord(BQ27220_CONTROL_CONTROL_STATUS));
    if (status == OP_STATUS::UNSEALED) {
        return true;
    }
    return false;
}

bool Bq27220::seal()
{
    OP_STATUS status;

    writeCtrlWord(BQ27220_CONTROL_SEALED);
    vTaskDelay(pdMS_TO_TICKS(5));
    status = OP_STATUS(readWord(BQ27220_CONTROL_CONTROL_STATUS));
    if(status == OP_STATUS::SEALED)
    {
        return true;
    }
    return false;
}

uint16_t Bq27220::getTemp()
{
    return readWord(BQ27220_COMMAND_TEMP);
}

uint16_t Bq27220::getBatterySt(void)
{
    return readWord(BQ27220_COMMAND_BATTERY_ST);
}

bool Bq27220::getIsCharging(void)
{
    uint16_t ret = readWord(BQ27220_COMMAND_BATTERY_ST);
    bat_st.full = ret;
    return !bat_st.st.DSG;
}

uint16_t Bq27220::getTimeToEmpty()
{
    return readWord(BQ27220_COMMAND_TTE);
}

uint16_t Bq27220::getRemainCap()
{
    return readWord(BQ27220_COMMAND_REMAIN_CAPACITY);
}

uint16_t Bq27220::getFullChargeCap(void)
{
    return readWord(BQ27220_COMMAND_FCHG_CAPATICY);
}

uint16_t Bq27220::getChargePcnt(void)
{
    return readWord(BQ27220_COMMAND_STATE_CHARGE);
}

uint16_t Bq27220::getAvgPower(void)
{
    return readWord(BQ27220_COMMAND_AVG_PWR);
}

uint16_t Bq27220::getStandbyCur(void)
{
    return readWord(BQ27220_COMMAND_STANDBY_CURR);
}

uint16_t Bq27220::getVolt(VOLT_MODE type)
{
    switch (type)
    {
    case VOLT:
        return readWord(BQ27220_COMMAND_VOLT);
        break;
    case VOLT_CHARGING:
        return readWord(BQ27220_COMMAND_CHARGING_VOLT);
        break;
    case VOLT_RWA:
        return readWord(BQ27220_COMMAND_RAW_VOLT);
        break;
    default:
        break;
    }
    return 0;
}

int16_t Bq27220::getCurr(CURR_MODE type)
{
    switch (type)
    {
    case CURR_RAW:
        return (int16_t)readWord(BQ27220_COMMAND_RAW_CURR);
        break;
    case CURR_INSTANT:
        return (int16_t)readWord(BQ27220_COMMAND_CURR);
        break;
    case CURR_STANDBY:
        return (int16_t)readWord(BQ27220_COMMAND_STANDBY_CURR);
        break;
    case CURR_CHARGING:
        return (int16_t)readWord(BQ27220_COMMAND_CHARGING_CURR);
        break;
    case CURR_AVERAGE:
        return (int16_t)readWord(BQ27220_COMMAND_AVG_CURR);
        break;
    default:
        break;
    }
    return -1;
}

uint16_t Bq27220::readWord(uint16_t subAddress)
{
    uint8_t data[2];
    i2cReadBytes(subAddress, data, 2);
    return ((uint16_t)data[1] << 8) | data[0];
}

uint16_t Bq27220::getId()
{
    return 0x0220;
}

uint16_t Bq27220::readCtrlWord(uint16_t fun)
{
    uint8_t msb = (fun >> 8);
    uint8_t lsb = (fun & 0x00FF);
    uint8_t cmd[2] = {lsb, msb};
    uint8_t data[2] = {0};

    i2cWriteBytes((uint8_t)BQ27220_COMMAND_CONTROL, cmd, 2);

    if (i2cReadBytes((uint8_t)0, data, 2))
    {
        return ((uint16_t)data[1] << 8) | data[0];
    }
    return 0;
}


uint16_t Bq27220::writeCtrlWord(uint16_t fun)
{
    uint8_t msb = (fun >> 8);
    uint8_t lsb = (fun & 0x00FF);
    uint8_t cmd[2] = {lsb, msb};
    uint8_t data[2] = {0};

    i2cWriteBytes((uint8_t)BQ27220_COMMAND_CONTROL, cmd, 2);

    return 0;
}

bool Bq27220::i2cReadBytes(uint8_t subAddress, uint8_t *dest, uint8_t count)
{
    i2c_master_transmit_receive(i2c_dev_handle, &subAddress, 1, dest, count, I2C_MASTER_TIMEOUT_MS);

    return true;
}

bool Bq27220::i2cWriteBytes(uint8_t subAddress, uint8_t *src, uint8_t count)
{
    uint8_t write_buf[64];
    if (count + 1 > sizeof(write_buf)) return false;

    write_buf[0] = subAddress;
    memcpy(&write_buf[1], src, count);
    write_buf[0] = subAddress;
    i2c_master_transmit(i2c_dev_handle, write_buf, count + 1, I2C_MASTER_TIMEOUT_MS); // sleep=0

    return true;
}
 
