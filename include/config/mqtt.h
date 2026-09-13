#include "config/base.h"
#include "defaults.h"

#ifndef NETWORK_CONFIG_MQTT_H
#define NETWORK_CONFIG_MQTT_H

namespace Config {
    struct Mqtt : Base {
        IPAddress ipAddress;
        uint16_t port = MQTT_DEFAULT_PORT;
        char username[MQTT_USERNAME_MAX_LENGTH + 1] = { 0 };
        char password[MQTT_PASSWORD_MAX_LENGTH + 1] = { 0 };

        Mqtt();
        void load() override;
        void save() const override;
        bool isValid() const override;
        void printKeyValuePairs() const override;
    };
}

#endif
