#include <Arduino.h>

#include "config/smart_hub.h"
#include "config/network.h"
#include "smart_hub.h"
#include "network.h"
#include "portal.h"

void setup() {
    Serial.begin(115200);

    if (!SmartHub::setup()) {
        Serial.println("Failed to setup Smart Hub.");
        while (1); // halt execution
    }

    auto smartHubConfig = Config::SmartHub();
    smartHubConfig.load();
    if (smartHubConfig.isValid()) {
        SmartHub::startListening(smartHubConfig.endpoint);
    }

    auto networkConfig = Config::Network();
    networkConfig.load();
    if (networkConfig.isValid()) {
        Network::connect(networkConfig);
    }
    
    if (!Portal::setup()) {
        Serial.println("Failed to setup config portal.");
        while (1); // halt execution
    }
}

void loop() {
    SmartHub::loop();
    Portal::loop();
}