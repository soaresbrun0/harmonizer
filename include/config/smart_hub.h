#include "config/base.h"
#include "../smart_hub.h"

#ifndef NETWORK_CONFIG_SMART_HUB_H
#define NETWORK_CONFIG_SMART_HUB_H

namespace Config {
    struct SmartHub : Base {
        ::SmartHub::Endpoint endpoint;

        SmartHub();
        void load() override;
        void save() const override;
        bool isValid() const override;
        void printKeyValuePairs() const override;
    };
}

#endif