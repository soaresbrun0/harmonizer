#include <Arduino.h>

#include "config/smart_hub.h"
#include "config/network.h"
#include "config/mqtt.h"
#include "home_assistant.h"
#include "metrics.h"
#include "smart_hub.h"
#include "network.h"
#include "portal.h"

void setup() {
    Serial.begin(115200);

    if (!Metrics::setup()) {
        Serial.println("Failed to setup Metrics.");
        while (1); // halt execution
    }

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

    // MQTT is best-effort: a failed broker connection retries in the
    // background and must never block the portal.
    auto mqttConfig = Config::Mqtt();
    mqttConfig.load();
    if (mqttConfig.isValid()) {
        HomeAssistant::setup(mqttConfig);
    }
}

void loop() {
    Metrics::loop();
    SmartHub::loop();
    Portal::loop();
    HomeAssistant::loop();
}