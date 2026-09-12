#include <Arduino.h>
#include "config/network.h"

#ifndef NETWORK_H
#define NETWORK_H

namespace Network {
    bool connect(Config::Network config);
    bool isConnected();
    IPAddress getIPAddress();
}

#endif