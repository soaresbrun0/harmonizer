#include <Arduino.h>

namespace Util {
    void joinStrings(const char *strings[], size_t count, char *buffer, size_t maxLength, const char *separator = "");
}