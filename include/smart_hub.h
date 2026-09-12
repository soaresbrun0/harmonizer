#include <Arduino.h>
#include "smart_hub_buttons.h"

#ifndef SMART_HUB_H
#define SMART_HUB_H

namespace SmartHub {
    enum class State {
        NotReady,
        Idle,
        Scanning,
        Listening
    };

    struct Endpoint {
        uint8_t channel;
        uint64_t address;
        
        bool isValid() const;
        void printDetails() const;
    };
    
    typedef void (*ScannerCallback)(const Endpoint endpoint);
    typedef void (*ButtonPressedCallback)(const Endpoint endpoint, const Button *buttons, uint8_t count);
    
    bool setup();
    void loop();

    const char *getUniqueId();
    State getState();
    Endpoint getActiveEndpoint();

    int8_t addScannerCallback(ScannerCallback callback);
    bool removeScannerCallback(int8_t id);

    int8_t addButtonPressedCallback(ButtonPressedCallback callback);
    bool removeButtonPressedCallback(int8_t id);

    bool startScanning();
    void stopScanning();

    bool startListening(Endpoint endpoint);
    void stopListening();
}

#endif