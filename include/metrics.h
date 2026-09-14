#ifndef METRICS_H
#define METRICS_H

#include <stddef.h>
#include <stdint.h>

// Platform metric collection and JSON formatting. Deliberately free of any
// Home Assistant dependency: this unit fetches numbers and assembles payloads,
// while home_assistant.cpp owns entities and publishing.
namespace Metrics {
    // One snapshot per takeDeviceSnapshot call, grouping one nested struct
    // per reporting sensor. Nested structs with attributes expose them via
    // toJson; state-only signals (loop rate, uptime) need no serialization.
    // DeviceSnapshot itself has no toJson: it only groups the pieces.
    struct DeviceSnapshot {
        struct Heap {
            uint32_t freeHeap;
            uint32_t minHeap;
            uint32_t maxAllocHeap;
            uint32_t heapSize;

            size_t toJson(char *out, size_t outSize) const;
            // Free heap as a percentage of total. Returns 100 when the
            // pool size is unknown so emptiness never reads as critical.
            uint8_t freePercent() const;
        };

        struct CPU {
            // Main-loop iterations/second, computed by takeDeviceSnapshot
            // from the passes counted by loop(): a relative CPU-load proxy
            // (true CPU % needs FreeRTOS run-time stats, which the Arduino
            // core doesn't enable).
            unsigned long loopHertz;
            uint8_t chipCores;
            unsigned long cpuFreqMHz;

            size_t toJson(char *out, size_t outSize) const;
        };

        struct Flash {
            uint32_t flashSize;
            uint32_t sketchSize;
            uint32_t freeSketchSpace;

            size_t toJson(char *out, size_t outSize) const;
            // Free sketch space as a percentage of total flash.
            uint8_t freePercent() const;
        };

        struct Psram {
            uint32_t psramSize;
            uint32_t freePsram;

            size_t toJson(char *out, size_t outSize) const;
            // Free PSRAM as a percentage of total. Returns 100 when the
            // board has no PSRAM so absence never reads as critical.
            uint8_t freePercent() const;
        };

        struct Uptime {
            unsigned long seconds;

            size_t toJson(char *out, size_t outSize) const;
        };

        struct OtherAttributes {
            uint8_t chipRevision;
            char chipModel[24];
            char sdkVersion[24];

            size_t toJson(char *out, size_t outSize) const;
        };

        Heap heap;
        CPU cpu;
        Psram psram;
        Flash flash;
        Uptime uptime;
        OtherAttributes otherAttributes;
    };

    struct WiFiSnapshot {
        bool connected;
        long rssi;
        char ssid[40];
        char bssid[24];
        char ip[16];

        size_t toJson(char *out, size_t outSize) const;
    };

    bool setup();
    void loop();
    
    DeviceSnapshot takeDeviceSnapshot();
    WiFiSnapshot takeWiFiSnapshot();
    bool hasPsram();
}

#endif
