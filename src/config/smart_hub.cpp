#include "config/smart_hub.h"
#include <Preferences.h>

static constexpr const char* PREFERENCES_SMART_HUB = "smart-hub";
static constexpr const char* PREFERENCES_SMART_HUB_ENDPOINT_CHANNEL = "endp.chan";
static constexpr const char* PREFERENCES_SMART_HUB_ENDPOINT_ADDRESS = "endp.addr";

Config::SmartHub::SmartHub() : Base(PREFERENCES_SMART_HUB) {}

void Config::SmartHub::load() {
    endpoint.channel = preferences.getUChar(PREFERENCES_SMART_HUB_ENDPOINT_CHANNEL, 0);
    endpoint.address = preferences.getULong64(PREFERENCES_SMART_HUB_ENDPOINT_ADDRESS, 0);
}

void Config::SmartHub::save() const {
    preferences.putUChar(PREFERENCES_SMART_HUB_ENDPOINT_CHANNEL, endpoint.channel);
    preferences.putULong64(PREFERENCES_SMART_HUB_ENDPOINT_ADDRESS, endpoint.address);
}

bool Config::SmartHub::isValid() const {
    return endpoint.isValid();
}

void Config::SmartHub::printKeyValuePairs() const {
    printKeyValuePair("Channel", endpoint.channel);
    printCustomKeyValuePair("Address", [&]() {
        Serial.print("0x");
        Serial.print(endpoint.address, HEX);
    });
}