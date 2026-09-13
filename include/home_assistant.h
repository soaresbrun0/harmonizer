#include "config/mqtt.h"

#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

namespace HomeAssistant {
    bool setup(const Config::Mqtt &config);
    void loop();
}

#endif
