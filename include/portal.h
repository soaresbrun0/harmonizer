#include <Arduino.h>

#ifndef PORTAL_H
#define PORTAL_H

namespace Portal {
    bool setup();
    void loop();
    
    const char *getUrl();
}

#endif