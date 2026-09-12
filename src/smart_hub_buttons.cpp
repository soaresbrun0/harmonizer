#include "smart_hub_buttons.h"
#include <Arduino.h>

const SmartHub::Button SmartHub::Button::all[] = {
    // ==========================================
    // Keypad (Group: 0xC1)
    // ==========================================
    // Navigation
    { .groupCode = 0xC1, .code = 0x0052, .name = "up" },
    { .groupCode = 0xC1, .code = 0x0051, .name = "down" },
    { .groupCode = 0xC1, .code = 0x0050, .name = "left" },
    { .groupCode = 0xC1, .code = 0x004F, .name = "right" },
    { .groupCode = 0xC1, .code = 0x0058, .name = "select" },
    { .groupCode = 0xC1, .code = 0x0065, .name = "menu" },

    // Numeric Keypad
    { .groupCode = 0xC1, .code = 0x001E, .name = "number_1" },
    { .groupCode = 0xC1, .code = 0x001F, .name = "number_2" },
    { .groupCode = 0xC1, .code = 0x0020, .name = "number_3" },
    { .groupCode = 0xC1, .code = 0x0021, .name = "number_4" },
    { .groupCode = 0xC1, .code = 0x0022, .name = "number_5" },
    { .groupCode = 0xC1, .code = 0x0023, .name = "number_6" },
    { .groupCode = 0xC1, .code = 0x0024, .name = "number_7" },
    { .groupCode = 0xC1, .code = 0x0025, .name = "number_8" },
    { .groupCode = 0xC1, .code = 0x0026, .name = "number_9" },
    { .groupCode = 0xC1, .code = 0x0027, .name = "number_0" },
    { .groupCode = 0xC1, .code = 0x0056, .name = "number_dot_dash" },
    { .groupCode = 0xC1, .code = 0x0028, .name = "number_enter" },

    // ==========================================
    // MediaHome (Group: 0xC3)
    // ==========================================
    // Power & Activities
    { .groupCode = 0xC3, .code = 0x01EC, .name = "off" },
    { .groupCode = 0xC3, .code = 0x01E8, .name = "music" },
    { .groupCode = 0xC3, .code = 0x01ED, .name = "tv" },
    { .groupCode = 0xC3, .code = 0x01E9, .name = "movie" },

    // Home Automation
    { .groupCode = 0xC3, .code = 0x0FF2, .name = "light_1" },
    { .groupCode = 0xC3, .code = 0x0FF3, .name = "light_2" },
    { .groupCode = 0xC3, .code = 0x0FF0, .name = "plus" },
    { .groupCode = 0xC3, .code = 0x0FF1, .name = "minus" },
    { .groupCode = 0xC3, .code = 0x0FF4, .name = "switch_1" },
    { .groupCode = 0xC3, .code = 0x0FF5, .name = "switch_2" },

    // Fastext Color Keys
    { .groupCode = 0xC3, .code = 0x01F7, .name = "red" },
    { .groupCode = 0xC3, .code = 0x01F6, .name = "green" },
    { .groupCode = 0xC3, .code = 0x01F5, .name = "yellow" },
    { .groupCode = 0xC3, .code = 0x01F4, .name = "blue" },

    // Guide & UI Utility
    { .groupCode = 0xC3, .code = 0x009A, .name = "dvr" },
    { .groupCode = 0xC3, .code = 0x008D, .name = "guide" },
    { .groupCode = 0xC3, .code = 0x01FF, .name = "info" },
    { .groupCode = 0xC3, .code = 0x0224, .name = "back" },
    { .groupCode = 0xC3, .code = 0x0094, .name = "exit" },

    // Volume
    { .groupCode = 0xC3, .code = 0x00E9, .name = "volume_up" },
    { .groupCode = 0xC3, .code = 0x00EA, .name = "volume_down" },
    { .groupCode = 0xC3, .code = 0x00E2, .name = "mute" },

    // Channels
    { .groupCode = 0xC3, .code = 0x009C, .name = "channel_up" },
    { .groupCode = 0xC3, .code = 0x009D, .name = "channel_down" },

    // Media Playback
    { .groupCode = 0xC3, .code = 0x00B4, .name = "skip_backward" },
    { .groupCode = 0xC3, .code = 0x00B3, .name = "skip_forward" },
    { .groupCode = 0xC3, .code = 0x00B0, .name = "play" },
    { .groupCode = 0xC3, .code = 0x00B1, .name = "pause" },
    { .groupCode = 0xC3, .code = 0x00B2, .name = "record" },
    { .groupCode = 0xC3, .code = 0x00B7, .name = "stop" }
};

const size_t SmartHub::Button::count = sizeof(all) / sizeof(all[0]);
static SmartHub::Button notFound = { .groupCode = 0x00, .code = 0x00, .name = nullptr };

SmartHub::Button SmartHub::Button::find(uint8_t groupCode, uint16_t code) {
    if (groupCode == 0x00 || code == 0x0000) {
        return notFound; // there's no point in looking the button up
    }
    for (size_t i = 0, n = count; i < n; i++) {
        Button button = all[i];
        if (button.groupCode == groupCode && button.code == code) {
            return button; 
        }
    }
    return notFound;
}

// +-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
// |   0   |   1   |   2   |   3   |   4   |   5   |   6   |   7   |   8   |   9   |
// +-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
// |   ID  |   C1  |   00  |   B1  |   B2  |   B3  |   B4  |   B5  |   B6  |  CRC  |
// +-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
// |   ID  |   C3  | B1_HI | B1_LO | B2_HI | B2_LO |   00  |   00  |   00  |  CRC  |
// +-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
SmartHub::Button::ParseResult SmartHub::Button::parse(const uint8_t *bytes, size_t length, Button *outButtons, uint8_t *inOutButtonCount) {
    // 1. Basic arg validation and capacity guard
    if (bytes == nullptr || outButtons == nullptr || inOutButtonCount == nullptr) {
        return ParseResult::InvalidArgs;
    }

    // 2. Validate the payload
    if (length < 10) {
        return ParseResult::InvalidPayload;
    }
    uint8_t checksum = 0;
    for (size_t i = 0; i < 9; i++) {
        checksum += bytes[i];
    }
    checksum = 0x00 - checksum; // Generate expected checksum
    if (bytes[9] != checksum) {
        return ParseResult::InvalidPayload; // Checksum mismatch
    }

    // 3. Decode the payload based on the Group Code layout
    uint8_t groupCode = bytes[1];
    uint16_t codes[6];
    uint8_t codeCount = 0;

    if (groupCode == 0xC1) {
        // C1 Layout contains up to 6 single-byte button slots (Bytes 3 to 8)
        // High byte is 0x00, Low byte is the transmitted byte value
        for (size_t i = 3; i <= 8; i++) {
            codes[codeCount++] = bytes[i]; // High byte is implicitly 0x00
        }
    } else if (groupCode == 0xC3) {
        // C3 Layout contains up to 2 multi-byte slots (little-endian)
        // Slot 1: Bytes 2 (Low) and 3 (High)
        codes[codeCount++] = (static_cast<uint16_t>(bytes[3]) << 8) | bytes[2];
        codes[codeCount++] = (static_cast<uint16_t>(bytes[5]) << 8) | bytes[4];
    } else {
        // Not a button payload
        return ParseResult::InvalidPayload;
    }

    // 4. Scan the button registry for matching buttons
    uint8_t maxButtonCount = *inOutButtonCount;
    uint8_t actualButtonCount = 0;

    for (uint8_t i = 0; i < codeCount; i++) {
        Button button = find(groupCode, codes[i]);
        if (button.name) {
            outButtons[actualButtonCount++] = button;
            if (actualButtonCount >= maxButtonCount) {
                break;   
            }
        }
    }
    
    *inOutButtonCount = actualButtonCount;
    return ParseResult::Success;
}

void SmartHub::Button::printDetails(void) const {
    Serial.print("Group Code      = 0x");
    Serial.println(groupCode, HEX);
    Serial.print("Code            = 0x");
    Serial.println(code, HEX);
    Serial.print("Name            = ");
    Serial.println(name);
}