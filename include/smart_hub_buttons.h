#include <Arduino.h>

#ifndef SMART_HUB_BUTTON_H
#define SMART_HUB_BUTTON_H

namespace SmartHub {
    struct Button {
        enum class ParseResult : int8_t {
            Success = 0,
            InvalidArgs = -1,
            InvalidPayload = -2,
        };

        uint8_t groupCode;
        uint16_t code;
        const char *name;

        static const Button all[];
        static const size_t count;
        static Button find(uint8_t groupCode, uint16_t code);
        static ParseResult parse(const uint8_t *bytes, size_t length, Button *outButtons, uint8_t *inOutButtonCount);
        void printDetails(void) const;
    };
}

#endif