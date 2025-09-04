#pragma once

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif


class Aw32001 {
public:
    Aw32001(i2c_master_bus_handle_t i2c_bus, uint8_t addr);
    void SetIntWakeupTime(uint8_t sel);
    void SetWatchdog(uint16_t timeout_s, uint8_t enable_discharge_watchdog);
    void ResetWatchdog();
    void SetCharge(bool en);
    void SetChargeCurrent(uint16_t current_ma);
    void SetDischargeCurrent(uint16_t current_ma);
    void SetChargeVoltage(uint16_t voltage_mv);
    void SetShippingMode(bool en);
    uint8_t GetChargeState();
    void SetPreChargeCurrent(uint16_t current_ma);
    void SetPrechargeToFastchargeThreshold(uint8_t sel);
    void SetNtcFunction(bool en);
    void DumpRegs(uint8_t start, uint8_t end);
private:
    i2c_master_dev_handle_t i2c_dev_handle;
    uint8_t ReadReg(uint8_t reg);
    void WriteReg(uint8_t reg, uint8_t value);
};

#ifdef __cplusplus
}
#endif
