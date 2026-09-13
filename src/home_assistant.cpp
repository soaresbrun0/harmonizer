#include "home_assistant.h"

#include <Arduino.h>
#include <ArduinoHA.h>
#include <WiFi.h>

#include "defaults.h"
#include "portal.h"
#include "smart_hub.h"
#include "util.h"

struct HADeviceTriggerRegistry {
    struct Entry {
        uint8_t buttonGroupCode;
        uint16_t buttonCode;
        HADeviceTrigger *trigger;
    };

    const HADeviceTrigger::TriggerType type;
    const Entry *entries;
    const size_t count;

    HADeviceTriggerRegistry(HADeviceTrigger::TriggerType type, const SmartHub::Button *buttons, size_t count);
    HADeviceTriggerRegistry(const HADeviceTriggerRegistry&) = delete;
    HADeviceTriggerRegistry& operator=(const HADeviceTriggerRegistry&) = delete;
    ~HADeviceTriggerRegistry();

    HADeviceTrigger *getTriggerForButton(const SmartHub::Button &button);
};

WiFiClient wifiClient;
HADevice *device = nullptr;
HAMqtt *mqtt = nullptr;

HADeviceTriggerRegistry *buttonShortPressTriggers = nullptr;
HADeviceTriggerRegistry *buttonShortReleaseTriggers = nullptr;

static void mqttStateChangedCallback(HAMqtt::ConnectionState state);
static void smartHubButtonPressedCallback(const SmartHub::Endpoint endpoint, const SmartHub::Button *buttons, uint8_t count);

bool HomeAssistant::setup(const Config::Mqtt &config) {
    if (!config.isValid() || mqtt != nullptr) {
        return mqtt != nullptr;
    }

    // Create device
    device = new HADevice(SmartHub::getUniqueId());
    device->setName("Harmonizer Smart Hub");
    device->setManufacturer("soaresbrun0");
    device->setModel("Harmonizer");
    device->setConfigurationUrl(Portal::getUrl());
    device->enableSharedAvailability();
    device->enableLastWill();

    // Get the list of all buttons to create triggers for
    const SmartHub::Button *buttons = SmartHub::Button::all;
    const size_t buttonCount = SmartHub::Button::count;

    // We must setup MQTT before creating any triggers
    mqtt = new HAMqtt(wifiClient, *device, 255);
    mqtt->onStateChanged(&mqttStateChangedCallback);

    // Create triggers and register the callback
    buttonShortPressTriggers = new HADeviceTriggerRegistry(
        HADeviceTrigger::TriggerType::ButtonShortPressType,
        buttons,
        buttonCount
    );
    buttonShortReleaseTriggers = new HADeviceTriggerRegistry(
        HADeviceTrigger::TriggerType::ButtonShortReleaseType,
        buttons,
        buttonCount
    );
    SmartHub::addButtonPressedCallback(&smartHubButtonPressedCallback);

    Serial.print("Starting Home Assistant bridge at mqtt://");
    Serial.print(config.ipAddress);
    Serial.print(":");
    Serial.print(config.port);
    Serial.print("... ");

    bool success;
    if (config.username[0] == '\0') {
        success = mqtt->begin(config.ipAddress, config.port);
    } else {
        success = mqtt->begin(config.ipAddress, config.port, config.username, config.password);
    }
    if (success) {
        mqtt->setBufferSize(512);
    }

    Serial.println(success ? "Done!" : "Failed!");
    return success;
}

void HomeAssistant::loop() {
    if (mqtt == nullptr) {
        return;
    }
    mqtt->loop();
    if (!mqtt->isConnected()) {
        static unsigned long lastNoticeMillis = 0;
        unsigned long now = millis();
        if (now - lastNoticeMillis >= 30000UL) {
            lastNoticeMillis = now;
            Serial.print("Home Assistant MQTT not connected, retrying (");
            Serial.print(now);
            Serial.println(" ms).");
        }
    }
}

#pragma mark - HADeviceTriggerRegistry

static HADeviceTriggerRegistry::Entry *newEntriesForButtons(
    HADeviceTrigger::TriggerType type,
    const SmartHub::Button *buttons,
    size_t count
) {
    HADeviceTriggerRegistry::Entry *entries = new HADeviceTriggerRegistry::Entry[count];
    for (size_t i = 0; i < count; i++) {
        const SmartHub::Button &button = buttons[i];
        HADeviceTriggerRegistry::Entry &entry = entries[i];
        entry.buttonGroupCode = button.groupCode;
        entry.buttonCode = button.code;
        entry.trigger = new HADeviceTrigger(type, button.name);
    }
    return entries;
}

HADeviceTriggerRegistry::HADeviceTriggerRegistry(
    HADeviceTrigger::TriggerType type,
    const SmartHub::Button *buttons,
    size_t count
) : type(type),
    entries(newEntriesForButtons(type, buttons, count)),
    count(count) {}

HADeviceTriggerRegistry::~HADeviceTriggerRegistry() {
    for (size_t i = 0; i < count; i++) {
        delete entries[i].trigger;
    }
    delete[] entries;
}

HADeviceTrigger *HADeviceTriggerRegistry::getTriggerForButton(const SmartHub::Button &button) {
    for (size_t i = 0; i < count; i++) {
        const HADeviceTriggerRegistry::Entry &entry = entries[i];
        if (entry.buttonGroupCode == button.groupCode && entry.buttonCode == button.code) {
            return entry.trigger;
        }
    }
    return nullptr;
}

#pragma mark - MQTT connection callbacks

static void printMqttConnectionState(HAMqtt::ConnectionState state) {
    switch (state) {
        case HAMqtt::ConnectionState::StateConnecting:
            Serial.print("connecting");
            break;
        case HAMqtt::ConnectionState::StateConnectionTimeout:
            Serial.print("connection-timeout");
            break;
        case HAMqtt::ConnectionState::StateConnectionLost:
            Serial.print("connection-lost");
            break;
        case HAMqtt::ConnectionState::StateConnectionFailed:
            Serial.print("connection-failed");
            break;
        case HAMqtt::ConnectionState::StateDisconnected:
            Serial.print("disconnected");
            break;
        case HAMqtt::ConnectionState::StateConnected:
            Serial.print("connected");
            break;
        case HAMqtt::ConnectionState::StateBadProtocol:
            Serial.print("bad-protocol");
            break;
        case HAMqtt::ConnectionState::StateBadClientId:
            Serial.print("bad-client-id");
            break;
        case HAMqtt::ConnectionState::StateUnavailable:
            Serial.print("unavailable");
            break;
        case HAMqtt::ConnectionState::StateBadCredentials:
            Serial.print("bad-credentials");
            break;
        case HAMqtt::ConnectionState::StateUnauthorized:
            Serial.print("unauthorized");
            break;
    }
}

static void mqttStateChangedCallback(HAMqtt::ConnectionState state) {
    static HAMqtt::ConnectionState lastState = HAMqtt::ConnectionState::StateUnavailable;

    Serial.print("Home Assistant bridge state changed from '");
    printMqttConnectionState(lastState);
    Serial.print("' to '");
    printMqttConnectionState(state);
    Serial.println("'.");

    lastState = state;
}

#pragma mark - Smart Hub callbacks

static void smartHubButtonPressedCallback(const SmartHub::Endpoint endpoint, const SmartHub::Button *buttons, uint8_t count) {
    // Copy of the previously pressed set. The incoming `buttons` array is
    // owned by the SmartHub stack frame, so only values may be retained.
    static SmartHub::Button lastButtons[MAX_PRESSED_BUTTON_COUNT];
    static uint8_t lastCount = 0;

    // Publish "button_short_press" trigger for all pressed buttons that
    // were not also pressed in the last callback.
    for (uint8_t i = 0; i < count; i++) {
        const SmartHub::Button &button = buttons[i];
        bool alreadyPressed = false;
        for (uint8_t j = 0; j < lastCount; j++) {
            if (button == lastButtons[j]) {
                alreadyPressed = true;
                break;
            }
        }
        HADeviceTrigger *trigger = alreadyPressed
            ? nullptr
            : buttonShortPressTriggers->getTriggerForButton(button);
        if (trigger != nullptr) {
            trigger->trigger();
        }
    }

    // Publish "button_short_release" trigger for all buttons that were
    // pressed in the last callback, but are no longer pressed.
    for (uint8_t i = 0; i < lastCount; i++) {
        const SmartHub::Button &lastButton = lastButtons[i];
        bool released = true;
        for (uint8_t j = 0; j < count; j++) {
            if (lastButton == buttons[j]) {
                released = false;
                break;
            }
        }
        HADeviceTrigger *trigger = released
            ? buttonShortReleaseTriggers->getTriggerForButton(lastButton)
            : nullptr;
        if (trigger != nullptr) {
            trigger->trigger();
        }
    }

    // Store pressed buttons to compute the diff in the next callback.
    lastCount = min(count, (uint8_t)MAX_PRESSED_BUTTON_COUNT);
    for (uint8_t i = 0; i < lastCount; i++) {
        lastButtons[i] = buttons[i];
    }
}