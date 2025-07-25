#ifndef ML307_BOARD_H
#define ML307_BOARD_H

#include "board.h"
#include <ml307_at_modem.h>

class Ml307Board : public Board {
protected:
    virtual std::string GetBoardJson() override;
    void WaitForNetworkReady();

public:
    Ml307AtModem modem_;
    Ml307Board(gpio_num_t tx_pin, gpio_num_t rx_pin, size_t rx_buffer_size = 4096);
    Ml307Board(gpio_num_t tx_pin, gpio_num_t rx_pin, gpio_num_t rst_pin, gpio_num_t pwr_pin, gpio_num_t dtr_pin, gpio_num_t wakeup_pin, size_t rx_buffer_size = 4096);
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    virtual Http* CreateHttp() override;
    virtual WebSocket* CreateWebSocket() override;
    virtual Mqtt* CreateMqtt() override;
    virtual Udp* CreateUdp() override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual bool GetPowerSaveMode() override;
    virtual AudioCodec* GetAudioCodec() override { return nullptr; }
    virtual std::string GetDeviceStatusJson() override;

    void Sleep() override;
    void WakeUp() override;
    void Shutdown() override;
    void SetIndicator(uint8_t r, uint8_t g, uint8_t b) override;
    void ClearDisplay(uint8_t color) override;
    void UpdateDisplay() override;
    void SetRefreshCode(uint8_t code) override;
    bool PowerOff();
};

#endif // ML307_BOARD_H
