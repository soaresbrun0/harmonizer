#include "config/network.h"
#include <Preferences.h>

static constexpr const char* PREFERENCES_NETWORK = "network";
static constexpr const char* PREFERENCES_NETWORK_INTERFACE = "interface";
static constexpr const char* PREFERENCES_NETWORK_WIFI_SSID = "wifi.ssid";
static constexpr const char* PREFERENCES_NETWORK_WIFI_PASSWORD = "wifi.password";

Config::Network::Network() : Base(PREFERENCES_NETWORK) {}

void Config::Network::load() {
    interface = static_cast<Interface>(preferences.getUChar(PREFERENCES_NETWORK_INTERFACE, static_cast<uint8_t>(Interface::None)));
    preferences.getString(PREFERENCES_NETWORK_WIFI_SSID, wifi.ssid, sizeof(wifi.ssid));
    preferences.getString(PREFERENCES_NETWORK_WIFI_PASSWORD, wifi.password, sizeof(wifi.password));
}

void Config::Network::save() const {
    preferences.putUChar(PREFERENCES_NETWORK_INTERFACE, static_cast<uint8_t>(interface));
    preferences.putString(PREFERENCES_NETWORK_WIFI_SSID, wifi.ssid);
    preferences.putString(PREFERENCES_NETWORK_WIFI_PASSWORD, wifi.password);
}

bool Config::Network::isValid() const {
    switch (interface) {
        case Interface::None:
            return false;
        case Interface::Ethernet:
            return false; // ethernet isn't supported yet
        case Interface::WiFi:
            return strlen(wifi.ssid) > 0;
    }
    return false;
}

void Config::Network::printKeyValuePairs() const {
    switch (interface) {
        case Interface::None:
            printKeyValuePair("Interface", "None");
            break;
        case Interface::Ethernet:
            printKeyValuePair("Interface", "Ethernet");
            break;
        case Interface::WiFi:
            printKeyValuePair("Interface", "Wi-Fi");
            printKeyValuePair("SSID", wifi.ssid);
            printKeyValuePair("Password", wifi.password);
            break;
    }
}
