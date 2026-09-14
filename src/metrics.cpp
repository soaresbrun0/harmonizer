#include "metrics.h"

#include <Arduino.h>
#include <WiFi.h>
#include <limits.h>

#include "network.h"

static size_t copyJsonEscaped(char *dst, size_t dstSize, const char *src) {
    size_t n = 0;
    for (; *src != '\0' && n + 1 < dstSize; src++) {
        if ((*src == '"' || *src == '\\') && n + 2 < dstSize) {
            dst[n++] = '\\';
        }
        dst[n++] = *src;
    }
    dst[n] = '\0';
    return n;
}

static void copyString(char *dst, size_t dstSize, const char *src) {
    if (dstSize == 0) {
        return;
    }
    snprintf(dst, dstSize, "%s", src);
}

static unsigned long loopIterationsSinceLastSnapshot = 0;
static unsigned long lastSnapshotMillis = 0;

bool Metrics::setup() {
    loopIterationsSinceLastSnapshot = 0;
    lastSnapshotMillis = millis();
    return true;
}

void Metrics::loop() {
    loopIterationsSinceLastSnapshot++;
}

Metrics::DeviceSnapshot Metrics::takeDeviceSnapshot() {
    DeviceSnapshot out;
    // Iterations have been accumulating since the previous snapshot; the
    // rate uses actual elapsed time because blocking reconnects can stretch
    // an interval. Zero elapsed with pending iterations means the loop is
    // faster than the millisecond clock can resolve, so saturate instead of
    // reporting 0 (which would wrongly read as "loop is dead").
    unsigned long now = millis();
    unsigned long elapsedMillis = now - lastSnapshotMillis;
    if (elapsedMillis == 0) {
        out.cpu.loopHertz = loopIterationsSinceLastSnapshot > 0 ? ULONG_MAX : 0;
    } else {
        out.cpu.loopHertz = loopIterationsSinceLastSnapshot * 1000UL / elapsedMillis;
    }
    loopIterationsSinceLastSnapshot = 0;
    lastSnapshotMillis = now;

    out.heap.freeHeap = ESP.getFreeHeap();
    out.heap.minHeap = ESP.getMinFreeHeap();
    out.heap.maxAllocHeap = ESP.getMaxAllocHeap();
    out.heap.heapSize = ESP.getHeapSize();
    out.cpu.chipCores = ESP.getChipCores();
    out.cpu.cpuFreqMHz = ESP.getCpuFreqMHz();
    out.psram.psramSize = ESP.getPsramSize();
    out.psram.freePsram = ESP.getFreePsram();
    out.flash.flashSize = ESP.getFlashChipSize();
    out.flash.sketchSize = ESP.getSketchSize();
    out.flash.freeSketchSpace = ESP.getFreeSketchSpace();
    out.uptime.seconds = millis() / 1000UL;
    out.otherAttributes.chipRevision = ESP.getChipRevision();
    copyString(out.otherAttributes.chipModel, sizeof(out.otherAttributes.chipModel), ESP.getChipModel());
    copyString(out.otherAttributes.sdkVersion, sizeof(out.otherAttributes.sdkVersion), ESP.getSdkVersion());
    return out;
}

Metrics::WiFiSnapshot Metrics::takeWiFiSnapshot() {
    WiFiSnapshot out;
    out.connected = Network::isConnected();
    if (!out.connected) {
        out.rssi = 0;
        out.ssid[0] = '\0';
        out.bssid[0] = '\0';
        out.ip[0] = '\0';
        return out;
    }
    out.rssi = WiFi.RSSI();
    copyString(out.ssid, sizeof(out.ssid), WiFi.SSID().c_str());
    copyString(out.bssid, sizeof(out.bssid), WiFi.BSSIDstr().c_str());
    copyString(out.ip, sizeof(out.ip), Network::getIPAddress().toString().c_str());
    return out;
}

uint8_t Metrics::DeviceSnapshot::Heap::freePercent() const {
    return heapSize > 0 ? (uint8_t)(freeHeap * 100UL / heapSize) : 100;
}

uint8_t Metrics::DeviceSnapshot::Psram::freePercent() const {
    return psramSize > 0 ? (uint8_t)(freePsram * 100UL / psramSize) : 100;
}

uint8_t Metrics::DeviceSnapshot::Flash::freePercent() const {
    return flashSize > 0 ? (uint8_t)(freeSketchSpace * 100UL / flashSize) : 100;
}

bool Metrics::hasPsram() {
    return ESP.getPsramSize() > 0;
}

size_t Metrics::DeviceSnapshot::Heap::toJson(char *out, size_t outSize) const {
    // Values in KB to match the sensor units; free_percent still derives
    // from the raw byte fields above.
    int written = snprintf(out, outSize,
        "{\"heap_size\":%u,\"min_heap\":%u,\"max_alloc_heap\":%u,\"free_percent\":%u}",
        (unsigned)(heapSize / 1024UL), (unsigned)(minHeap / 1024UL),
        (unsigned)(maxAllocHeap / 1024UL), freePercent());
    return written > 0 ? (size_t)written : 0;
}

size_t Metrics::DeviceSnapshot::CPU::toJson(char *out, size_t outSize) const {
    int written = snprintf(out, outSize,
        "{\"chip_cores\":%u,\"cpu_freq_mhz\":%lu,\"loop_hz\":%lu}",
        chipCores, cpuFreqMHz, loopHertz);
    return written > 0 ? (size_t)written : 0;
}

size_t Metrics::DeviceSnapshot::Flash::toJson(char *out, size_t outSize) const {
    int written = snprintf(out, outSize,
        "{\"flash_size\":%u,\"free_sketch\":%u,\"free_percent\":%u}",
        (unsigned)(flashSize / 1024UL), (unsigned)(freeSketchSpace / 1024UL), freePercent());
    return written > 0 ? (size_t)written : 0;
}

size_t Metrics::DeviceSnapshot::Uptime::toJson(char *out, size_t outSize) const {
    int written = snprintf(out, outSize, "{\"uptime_s\":%lu}", seconds);
    return written > 0 ? (size_t)written : 0;
}

size_t Metrics::DeviceSnapshot::Psram::toJson(char *out, size_t outSize) const {
    int written = snprintf(out, outSize, "{\"psram_size\":%u,\"free_percent\":%u}",
        (unsigned)(psramSize / 1024UL), freePercent());
    return written > 0 ? (size_t)written : 0;
}

size_t Metrics::DeviceSnapshot::OtherAttributes::toJson(char *out, size_t outSize) const {
    char chipModel[sizeof(this->chipModel) * 2];
    copyJsonEscaped(chipModel, sizeof(chipModel), this->chipModel);
    char sdkVersion[sizeof(this->sdkVersion) * 2];
    copyJsonEscaped(sdkVersion, sizeof(sdkVersion), this->sdkVersion);

    int written = snprintf(out, outSize,
        "{\"chip_model\":\"%s\",\"chip_revision\":%u,\"sdk_version\":\"%s\"}",
        chipModel, chipRevision, sdkVersion);
    return written > 0 ? (size_t)written : 0;
}

size_t Metrics::WiFiSnapshot::toJson(char *out, size_t outSize) const {
    char ssid[sizeof(this->ssid) * 2];
    copyJsonEscaped(ssid, sizeof(ssid), this->ssid);
    int written = snprintf(out, outSize, "{\"ssid\":\"%s\",\"bssid\":\"%s\",\"ip\":\"%s\"}",
        ssid, bssid, ip);
    return written > 0 ? (size_t)written : 0;
}
