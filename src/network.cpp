#include "network.h"

#include <Arduino.h>
#include <WiFi.h>

static Config::Network::Interface activeInterface = Config::Network::Interface::None;

static bool connectWiFi(const char *ssid, const char *password) {
    Serial.print("Connecting to Wi-Fi... ");
    WiFi.mode(WIFI_MODE_STA);
    if (password == nullptr || password[0] == '\0') {
        WiFi.begin(ssid); // open network
    } else {
        WiFi.begin(ssid, password);
    }

    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Failed!");
        return false;
    }

    WiFi.setAutoReconnect(true);
    Serial.println("Done!");
    return true;
}

bool Network::connect(Config::Network config) {
    #if VERBOSE
    config.printDetails();
#endif

    auto connect = [&] () {
        if (config.isValid()) {
            switch (config.interface) {
                case Config::Network::Interface::None:
                    break;
                case Config::Network::Interface::Ethernet:
                    return false; // ethernet isn't supported yet
                case Config::Network::Interface::WiFi:
                    return connectWiFi(config.wifi.ssid, config.wifi.password);
            }
        }
        Serial.println("Invalid network config.");
        return false;
    };

    auto success = connect();
    activeInterface = success ? config.interface : Config::Network::Interface::None;
    return success;
}

bool Network::isConnected() {
    switch (activeInterface) {
        case Config::Network::Interface::None:
            break;
        case Config::Network::Interface::Ethernet:
            break; // ethernet isn't supported yet
        case Config::Network::Interface::WiFi:
            return WiFi.isConnected();
    }
    return false;
}

IPAddress Network::getIPAddress() {
    switch (activeInterface) {
        case Config::Network::Interface::None:
            break;
        case Config::Network::Interface::Ethernet:
            break; // ethernet isn't supported yet
        case Config::Network::Interface::WiFi:
            return WiFi.localIP();
    }
    return IPAddress();
}