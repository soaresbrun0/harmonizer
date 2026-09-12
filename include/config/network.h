#include "config/base.h"
#include "defaults.h"

#ifndef NETWORK_CONFIG_NETWORK_H
#define NETWORK_CONFIG_NETWORK_H

namespace Config {
    struct Network : Base {
        enum class Interface : uint8_t {
            None = 0,
            Ethernet = 1,
            WiFi = 2
        };

        struct WiFi {
            char ssid[WL_SSID_MAX_LENGTH + 1] = { 0 };
            char password[WL_WPA_KEY_MAX_LENGTH + 1] = { 0 };
        };

        Interface interface = Interface::None;
        WiFi wifi;

        Network();
        void load() override;
        void save() const override;
        bool isValid() const override;
        void printKeyValuePairs() const override;
    };
}

#endif