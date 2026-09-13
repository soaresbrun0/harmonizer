#include "config/mqtt.h"
#include <Preferences.h>

static constexpr const char* PREFERENCES_MQTT = "mqtt";
static constexpr const char* PREFERENCES_MQTT_IP_ADDRESS = "ip.addr";
static constexpr const char* PREFERENCES_MQTT_PORT = "port";
static constexpr const char* PREFERENCES_MQTT_USERNAME = "username";
static constexpr const char* PREFERENCES_MQTT_PASSWORD = "password";

Config::Mqtt::Mqtt() : Base(PREFERENCES_MQTT) {}

void Config::Mqtt::load() {
    char ipString[16] = { 0 };
    preferences.getString(PREFERENCES_MQTT_IP_ADDRESS, ipString, sizeof(ipString));
    ipAddress.fromString(ipString);
    port = preferences.getUShort(PREFERENCES_MQTT_PORT, MQTT_DEFAULT_PORT);
    preferences.getString(PREFERENCES_MQTT_USERNAME, username, sizeof(username));
    preferences.getString(PREFERENCES_MQTT_PASSWORD, password, sizeof(password));
}

void Config::Mqtt::save() const {
    preferences.putString(PREFERENCES_MQTT_IP_ADDRESS, ipAddress.toString());
    preferences.putUShort(PREFERENCES_MQTT_PORT, port);
    preferences.putString(PREFERENCES_MQTT_USERNAME, username);
    preferences.putString(PREFERENCES_MQTT_PASSWORD, password);
}

bool Config::Mqtt::isValid() const {
    return static_cast<uint32_t>(ipAddress) != 0 && port > 0;
}

void Config::Mqtt::printKeyValuePairs() const {
    printCustomKeyValuePair("Host", [&]() {
        Serial.print(ipAddress);
        Serial.print(":");
        Serial.print(port);
    });
    printKeyValuePair("Username", username);
    printKeyValuePair("Password", password);
}
