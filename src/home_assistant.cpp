#include "home_assistant.h"

#include <Arduino.h>
#include <ArduinoHA.h>
#include <WiFi.h>

#include "defaults.h"
#include "metrics.h"
#include "network.h"
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

HABinarySensor *deviceHealthSensor = nullptr;
HASensor *pressedButtonsSensor = nullptr;
HASensor *heapSensor = nullptr;
HASensor *cpuSensor = nullptr;
HASensor *psramSensor = nullptr;
HASensor *flashSensor = nullptr;
HASensor *uptimeSensor = nullptr;
HASensor *otherAttributesSensor = nullptr;
HASensor *wifiSensor = nullptr;
HAButton *rebootButton = nullptr;

// Metrics report cadence. HASensor has no change detection (every setValue
// publishes), so this also bounds MQTT traffic to ~12 messages per interval.
// Heap or PSRAM below their HA_MIN_HEALTHY_*_PERCENT free thresholds is
// reported as a device-health problem.
static unsigned long rebootAtMillis = 0;
static unsigned long lastMetricsMillis = 0;
static unsigned long lastButtonPressMillis = 0;

static void mqttStateChangedCallback(HAMqtt::ConnectionState state);
static void smartHubButtonPressedCallback(const SmartHub::Endpoint endpoint, const SmartHub::Button *buttons, uint8_t count);
static void rebootCommandCallback(HAButton *sender);
static void reportMetrics();

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
    // Prefix every entity unique_id with the per-unit MAC so two
    // harmonizers on one network register distinct HA entities.
    device->enableExtendedUniqueIds();
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

    deviceHealthSensor = new HABinarySensor("device_health");
    deviceHealthSensor->setName("Device Health");
    deviceHealthSensor->setDeviceClass("problem");
    // Silence means trouble: HA marks the entity unknown if no state
    // arrives within HA_SENSOR_STALE_AFTER_S. This only means anything
    // because the health state is force-published every refresh below.
    deviceHealthSensor->setExpireAfter(HA_SENSOR_STALE_AFTER_S);

    // Live held-button set for remote verification (and chord watching).
    // Event-driven from the button callback below, not the metrics tick.
    pressedButtonsSensor = new HASensor("pressed_buttons");
    pressedButtonsSensor->setName("Pressed Buttons");
    pressedButtonsSensor->setIcon("mdi:remote");
    pressedButtonsSensor->setValue("");

    // One sensor per trendable signal so HA can graph each over time.
    // Static chip facts live on other_device_attributes instead.
    heapSensor = new HASensor("heap", HASensor::JsonAttributesFeature);
    heapSensor->setName("Heap");
    heapSensor->setIcon("mdi:memory");
    heapSensor->setDeviceClass("data_size");
    heapSensor->setUnitOfMeasurement("kB");

    cpuSensor = new HASensor("cpu", HASensor::JsonAttributesFeature);
    cpuSensor->setName("CPU");
    cpuSensor->setIcon("mdi:cpu-32-bit");
    cpuSensor->setDeviceClass("frequency");
    cpuSensor->setUnitOfMeasurement("Hz");

    // PSRAM only exists on some boards; no sensor without it.
    if (Metrics::hasPsram()) {
        psramSensor = new HASensor("psram", HASensor::JsonAttributesFeature);
        psramSensor->setName("PSRAM");
        psramSensor->setIcon("mdi:memory");
        psramSensor->setDeviceClass("data_size");
        psramSensor->setUnitOfMeasurement("kB");
    }

    flashSensor = new HASensor("flash", HASensor::JsonAttributesFeature);
    flashSensor->setName("Flash");
    flashSensor->setIcon("mdi:memory");
    flashSensor->setDeviceClass("data_size");
    flashSensor->setUnitOfMeasurement("kB");

    uptimeSensor = new HASensor("uptime", HASensor::JsonAttributesFeature);
    uptimeSensor->setName("Uptime");
    uptimeSensor->setDeviceClass("duration");
    uptimeSensor->setUnitOfMeasurement("s");

    otherAttributesSensor = new HASensor("other_attrs", HASensor::JsonAttributesFeature);
    otherAttributesSensor->setName("Other Attributes");
    otherAttributesSensor->setIcon("mdi:shape-outline");

    wifiSensor = new HASensor("wifi", HASensor::JsonAttributesFeature);
    wifiSensor->setName("Wi-Fi");
    wifiSensor->setDeviceClass("signal_strength");
    wifiSensor->setUnitOfMeasurement("dBm");
    // Silence means trouble: HA marks the entity unknown if no state
    // arrives within HA_SENSOR_STALE_AFTER_S. A template binary sensor
    // (see README) maps unknown/unavailable to a problem state.
    wifiSensor->setExpireAfter(HA_SENSOR_STALE_AFTER_S);

    rebootButton = new HAButton("reboot");
    rebootButton->setName("Restart");
    rebootButton->setIcon("mdi:restart");
    rebootButton->onCommand(&rebootCommandCallback);

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

    if (rebootAtMillis != 0 && (int32_t)(millis() - rebootAtMillis) >= 0) {
        rebootAtMillis = 0;
        Serial.println("Rebooting...");
        Serial.flush();
        ESP.restart();
    }

    // Gated on MQTT: nothing here can publish while disconnected, and the
    // forced first refresh is only spent once it can actually go out.
    // Reporting also pauses while the remote is in use (any button packet
    // resets the quiet timer) and resumes after HA_METRICS_REPORT_RESUME_DELAY_MS
    // of silence, so bursts never stall radio polling mid-navigation.
    unsigned long now = millis();
    unsigned long elapsedMillis = now - lastMetricsMillis;
    bool needsMetricsRefresh = lastMetricsMillis == 0 || elapsedMillis >= HA_METRICS_REPORT_INTERVAL_MS;
    bool idle = lastButtonPressMillis == 0 || now - lastButtonPressMillis >= HA_METRICS_REPORT_RESUME_DELAY_MS;
    if (mqtt->isConnected() && needsMetricsRefresh && idle) {
        lastMetricsMillis = now;
        reportMetrics();
    }
}

static void rebootCommandCallback(HAButton *sender) {
    (void)sender;
    // Deferred: this runs inside mqtt->loop() while it processes the
    // incoming command, so restarting here would cut the connection
    // mid-handler. loop() performs the restart after the delay instead.
    Serial.println("Reboot requested from Home Assistant.");
    rebootAtMillis = millis() + HA_REBOOT_DELAY_MS;
}

static void reportMetrics() {
    auto device = Metrics::takeDeviceSnapshot();

    // Health is force-published every refresh (not just on change) so the
    // signal doubles as a heartbeat: a device quiet for HA_SENSOR_STALE_AFTER_S
    // expires to unknown via the expire_after above. Failed publishes are
    // not cached by the library and retry on the next refresh by
    // themselves.
    bool critical = device.heap.freePercent() < HA_MIN_HEALTHY_HEAP_PERCENT
        || device.psram.freePercent() < HA_MIN_HEALTHY_PSRAM_PERCENT;
    deviceHealthSensor->setState(critical, true);

    if (!mqtt->isConnected()) {
        return;
    }

    char heapStr[12];
    snprintf(heapStr, sizeof(heapStr), "%u", (unsigned)(device.heap.freeHeap / 1024UL));
    heapSensor->setValue(heapStr);

    char heapAttrs[112];
    device.heap.toJson(heapAttrs, sizeof(heapAttrs));
    heapSensor->setJsonAttributes(heapAttrs);

    char loopStr[12];
    snprintf(loopStr, sizeof(loopStr), "%lu", device.cpu.loopHertz);
    cpuSensor->setValue(loopStr);

    char cpuAttrs[80];
    device.cpu.toJson(cpuAttrs, sizeof(cpuAttrs));
    cpuSensor->setJsonAttributes(cpuAttrs);

    char uptimeStr[12];
    snprintf(uptimeStr, sizeof(uptimeStr), "%lu", device.uptime.seconds);
    uptimeSensor->setValue(uptimeStr);

    char uptimeAttrs[32];
    device.uptime.toJson(uptimeAttrs, sizeof(uptimeAttrs));
    uptimeSensor->setJsonAttributes(uptimeAttrs);

    char flashStr[12];
    snprintf(flashStr, sizeof(flashStr), "%u", (unsigned)(device.flash.sketchSize / 1024UL));
    flashSensor->setValue(flashStr);

    char flashAttrs[80];
    device.flash.toJson(flashAttrs, sizeof(flashAttrs));
    flashSensor->setJsonAttributes(flashAttrs);

    otherAttributesSensor->setValue(device.otherAttributes.chipModel);

    char staticAttrs[224];
    device.otherAttributes.toJson(staticAttrs, sizeof(staticAttrs));
    otherAttributesSensor->setJsonAttributes(staticAttrs);

    if (psramSensor != nullptr) {
        char psramStr[12];
        snprintf(psramStr, sizeof(psramStr), "%u", (unsigned)(device.psram.freePsram / 1024UL));
        psramSensor->setValue(psramStr);

        char psramAttrs[64];
        device.psram.toJson(psramAttrs, sizeof(psramAttrs));
        psramSensor->setJsonAttributes(psramAttrs);
    }

    auto wifi = Metrics::takeWiFiSnapshot();
    if (!wifi.connected) {
        return;
    }

    char rssiStr[8];
    snprintf(rssiStr, sizeof(rssiStr), "%ld", wifi.rssi);
    wifiSensor->setValue(rssiStr);

    char wifiAttrs[192];
    wifi.toJson(wifiAttrs, sizeof(wifiAttrs));
    wifiSensor->setJsonAttributes(wifiAttrs);
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

static int compareButtonNames(const void * const lhs, const void * const rhs) {
    const char *lhsButtonName = *static_cast<const char * const *>(lhs);
    const char *rhsButtonName = *static_cast<const char * const *>(rhs);
    return strcmp(lhsButtonName, rhsButtonName);
}

static void smartHubButtonPressedCallback(const SmartHub::Endpoint endpoint, const SmartHub::Button *buttons, uint8_t count) {
    // Any button packet (press or release) counts as remote activity and
    // pauses metric reporting until the quiet period elapses.
    lastButtonPressMillis = millis();

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

    // Publish the currently held set (`+`-joined, sorted, so a chord reads
    // the same regardless of press order; empty string when idle). Names
    // are referenced, not copied: they point into SmartHub's static button
    // table. Every callback carries the full set, so a received packet
    // always replaces the state — only a total loss of all further packets
    // can leave a stale value, healed by the next interaction.
    if (count == 0) {
        pressedButtonsSensor->setValue("");
    } else if (count == 1) {
        pressedButtonsSensor->setValue(buttons[0].name);
    } else {
        const char *names[count];
        for (uint8_t i = 0; i < count; i++) {
            names[i] = buttons[i].name;
        }
        qsort(names, count, sizeof(names[0]), compareButtonNames);
        char chord[HA_PRESSED_BUTTONS_VALUE_MAX_LENGTH] = { 0 };
        Util::joinStrings(names, count, chord, sizeof(chord), "+");
        pressedButtonsSensor->setValue(chord);
    }

    // Store pressed buttons to compute the diff in the next callback.
    lastCount = min(count, (uint8_t)MAX_PRESSED_BUTTON_COUNT);
    for (uint8_t i = 0; i < lastCount; i++) {
        lastButtons[i] = buttons[i];
    }
}