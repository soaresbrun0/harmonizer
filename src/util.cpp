#include "util.h"

void Util::joinStrings(const char *strings[], size_t count, char *buffer, size_t maxLength, const char *separator) {
    if (buffer == nullptr || maxLength == 0) {
        return;
    }
    char *currentBuffer = buffer;
    size_t remainingCapacity = maxLength - 1;
    auto write = [&] (const char *string) {
        size_t length = string == nullptr ? 0 : strlen(string);
        if (length > 0) {
            length = min(length, remainingCapacity);
            memcpy(currentBuffer, string, length);
            currentBuffer += length;
            remainingCapacity -= length;
        }
        return remainingCapacity > 0;
    };
    for (size_t i = 0; i < count; i++) {
        if (!(currentBuffer == buffer || write(separator)) || !write(strings[i])) {
            break;
        }
    }
    *currentBuffer = '\0';
}
