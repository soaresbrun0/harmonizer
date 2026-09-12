#include "smart_hub.h"
#include "smart_hub_buttons.h"
#include "defaults.h"
#include <RF24.h>

// Smart Hub RF24 network and radio parameters
static const uint64_t pairingAddress = 0xBB0ADCA575;
static const uint8_t potentialChannels[12] = { 5, 8, 14, 17, 32, 35, 41, 44, 62, 65, 71, 74 };
static int currentChannelIndex = 0;

// Messages to send to the Smart Hub to get regular remote RF24 address
const uint8_t pairMessage[22] = { 242, 95, 1, 225, 154, 157, 218, 83, 40, 64, 30, 4, 2, 7, 12, 0, 0, 0, 0, 0, 102, 100 };
const uint8_t pingMessage[5] = { 242, 64, 1, 225, 236 };
static int pingAttemptsRemaining = 0;

static RF24 radio(CE_PIN, CSN_PIN);

static char uniqueId[13] = { 0 };
static SmartHub::State state = SmartHub::State::NotReady;
static SmartHub::Endpoint activeEndpoint = {};

static constexpr uint8_t maxCallbackCount = 16;
static SmartHub::ScannerCallback scannerCallbacks[maxCallbackCount] = {};
static SmartHub::ButtonPressedCallback buttonPressedCallbacks[maxCallbackCount] = {};

namespace SmartHub {
    enum class StopScanningReason {
        None,
        FoundSmartHub,
        TimedOut
    };

    void stopScanning(StopScanningReason reason);
    void continueScanning(void);
    void continueListening(void);
    void processPayload(const uint8_t *bytes, size_t length);
    void processButtonsPressed(const SmartHub::Button *buttons, size_t count);
}

#pragma mark - Lifecycle

bool SmartHub::setup() {
    if (state != SmartHub::State::NotReady) {
        Serial.println("Smart Hub is already setup.");
        return false;
    }

    // Radio setup
    SPI.begin();
    if (!radio.begin(&SPI)) {
      Serial.println("nRF24L01+ radio not responding.");
      return false;
    }
    Serial.println("nRF24L01+ radio started.");

    radio.setDataRate(RF24_2MBPS);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setCRCLength (RF24_CRC_16);

    Serial.println("nRF24L01+ radio configured.");
#if VERBOSE
    radio.printDetails();
#endif

    state = SmartHub::State::Idle;
    return true;
}

void SmartHub::loop(void) {
    switch (state) {
        case SmartHub::State::NotReady:
        case SmartHub::State::Idle:
            break;
        case SmartHub::State::Scanning:
            continueScanning();
            break;
        case SmartHub::State::Listening:
            continueListening();
            break;
    }
}

const char *SmartHub::getUniqueId() {
    if (uniqueId[0] == '\0') {
        snprintf(uniqueId, sizeof(uniqueId), "%012llX", ESP.getEfuseMac());
    }
    return uniqueId;
}

SmartHub::State SmartHub::getState() {
    return state;
}

SmartHub::Endpoint SmartHub::getActiveEndpoint() {
    return activeEndpoint;
}

#pragma mark - Callbacks

template <typename T>
int8_t addCallback(T callback, T callbacks[]) {
    for (int8_t i = 0; i < maxCallbackCount; i++) {
        if (callbacks[i] == nullptr) {
            callbacks[i] = callback;
            return i;
        }
    }
    Serial.println("Failed to add callback!");
    return -1;
}

template <typename T>
bool removeCallback(int8_t i, T callbacks[]) {
    if (i < 0 || i >= maxCallbackCount || callbacks[i] == nullptr) {
        Serial.println("Failed to remove callback!");
        return false;
    }
    callbacks[i] = nullptr;
    return true;
}

template <typename T, typename... Args>
void triggerCallbacks(T callbacks[], Args... args) {
    for (int8_t i = 0; i < maxCallbackCount; i++) {
        if (callbacks[i] != nullptr) {
            callbacks[i](args...); 
        }
    }
}

int8_t SmartHub::addScannerCallback(ScannerCallback callback) {
    return addCallback(callback, scannerCallbacks);
}

bool SmartHub::removeScannerCallback(int8_t i) {
    return removeCallback(i, scannerCallbacks);
}

int8_t SmartHub::addButtonPressedCallback(ButtonPressedCallback callback) {
    return addCallback(callback, buttonPressedCallbacks);
}

bool SmartHub::removeButtonPressedCallback(int8_t i) {
    return removeCallback(i, buttonPressedCallbacks);
}

#pragma mark - Endpoint

bool SmartHub::Endpoint::isValid(void) const {
    // 1. Validate the hardware RF channel range
    if (channel > 124) {
        return false;
    }

    // 2. Mask the address to 5 bytes (40 bits) to clear any uninitialized high bits
    uint64_t maskedAddress = address & 0xFFFFFFFFFFULL;

    // 3. Reject illegal completely blank or completely high hardware states
    if (maskedAddress == 0x0000000000ULL || maskedAddress == 0xFFFFFFFFFFULL) {
        return false;
    }

    // 4. Reject illegal alternating bit preambles on the first transmitted byte.
    // The RF24 library transmits the lowest byte of your uint64_t first (LSB).
    uint8_t firstByte = (uint8_t)(maskedAddress & 0xFF);
    if (firstByte == 0x55 || firstByte == 0xAA) {
        return false;
    }

    return true;
}

void SmartHub::Endpoint::printDetails(void) const {
    Serial.print("Channel         = ");
    Serial.println(channel);
    Serial.print("Address         = 0x");
    Serial.println(address, HEX);
    Serial.print("Status          = ");
    Serial.println(isValid() ? "VALID" : "INVALID");
}

#pragma mark - Scanning

bool SmartHub::startScanning() {
    if (state != SmartHub::State::Idle) {
        Serial.println("Smart Hub is busy or not ready.");
        return false;
    }

    radio.stopListening();
    radio.flush_tx();
    radio.openWritingPipe(pairingAddress);
    state = SmartHub::State::Scanning;

    Serial.println("Scanning for Smart Hub...");
#if VERBOSE
    Serial.println("Power on the Smart Hub and press the pair/reset button.");
#endif
    return true;
}

void SmartHub::stopScanning(void) {
    stopScanning(StopScanningReason::None);
}

void SmartHub::stopScanning(StopScanningReason reason) {
    if (state != SmartHub::State::Scanning) {
        Serial.println("Smart Hub is not scanning.");
        return;
    }

    state = SmartHub::State::Idle;

    switch (reason) {
        case StopScanningReason::None:
            Serial.println("Stopped scanning for Smart Hub.");
            break;
        case StopScanningReason::FoundSmartHub:
            break; // already logged
        case StopScanningReason::TimedOut:
            Serial.println("No Smart Hub found.");
            break;
    }
}

void SmartHub::continueScanning(void) {
    if (state != SmartHub::State::Scanning) {
        Serial.println("Smart Hub is not scanning.");
        return;
    }

    // Send out data to trigger the Hub
    if (pingAttemptsRemaining == 0) {
        radio.setChannel(potentialChannels[currentChannelIndex]);
        if (radio.write(&pairMessage, sizeof(pairMessage))) {
            pingAttemptsRemaining = 10;
        } else {
            currentChannelIndex++;
            if (currentChannelIndex > 11) {
                currentChannelIndex = 0;
            }
        }
    } else {
        radio.write(&pingMessage, sizeof(pingMessage));
        pingAttemptsRemaining--;
    }

    delay(100);

    // Look for and interpret ACK payloads
    if (radio.isAckPayloadAvailable()) {
        uint8_t dataReceived[32];
        int payloadSize = radio.getDynamicPayloadSize();
        radio.read(&dataReceived, payloadSize);

        if (payloadSize == 22) {
            SmartHub::Endpoint endpoint;
            endpoint.channel = potentialChannels[currentChannelIndex];
            endpoint.address =
                (uint64_t)dataReceived[3] << 32 |
                (uint64_t)dataReceived[4] << 24 |
                (uint64_t)dataReceived[5] << 16 |
                (uint64_t)dataReceived[6] << 8 |
                (uint64_t)(dataReceived[7] - 1);
            
            Serial.println("Found Smart Hub!");
#if VERBOSE
            endpoint.printDetails();
#endif
            
            stopScanning(StopScanningReason::FoundSmartHub);
            triggerCallbacks(scannerCallbacks, endpoint);
        }
    }
}

#pragma mark - Listening

bool SmartHub::startListening(Endpoint endpoint) {
    if (state != SmartHub::State::Idle) {
        Serial.println("Radio is busy or not ready.");
        return false;
    }

    // Configure the radio to listen for remote commands
    radio.setChannel(endpoint.channel);
    radio.setDataRate(RF24_2MBPS);
    radio.enableDynamicPayloads();
    radio.setCRCLength (RF24_CRC_16);
    radio.openReadingPipe(1, endpoint.address & 0xFFFFFFFF00);
    radio.openReadingPipe(2, endpoint.address & 0xFFFFFFFFFF);
    radio.startListening();
    
    state = SmartHub::State::Listening;
    activeEndpoint = endpoint;

    Serial.println("Listening for remote commands...");
#if VERBOSE
    endpoint.printDetails();
#endif
    return true;
}

void SmartHub::stopListening(void) {
    if (state != SmartHub::State::Listening) {
        Serial.println("Smart Hub is not listening.");
        return;
    }

    state = SmartHub::State::Idle;
    activeEndpoint = {};
    radio.stopListening();
    radio.flush_rx();

    Serial.println("Stopped listening for remote commands.");
}

void SmartHub::continueListening(void) {
    if (state != SmartHub::State::Listening) {
        Serial.println("Smart Hub is not listening.");
        return;
    }

    uint8_t pipeNum;
    while (radio.available(&pipeNum)) {
        // Read packet
        uint8_t dataReceived[32];
        int payloadSize = radio.getDynamicPayloadSize();
        if (payloadSize > 32) {
            Serial.println("Payload size exceeds buffer size.");
            return;
        }
        radio.read(&dataReceived, payloadSize);

#if VERBOSE
        Serial.print("Received message: 0x");
        char tmp[3];
        for (int i = 0; i < payloadSize; i++) {
            if (i > 0) {
                Serial.print(".");
            }
            sprintf(tmp, "%.2X", dataReceived[i]);
            Serial.print(tmp);
        }
        Serial.print(" (");
        Serial.print(payloadSize);
        Serial.print(" bytes on pipe ");
        Serial.print(pipeNum);
        Serial.println(")");
#endif

        processPayload(dataReceived, payloadSize);
    }
}

void SmartHub::processPayload(const uint8_t *bytes, size_t length) {
    Button buttons[MAX_PRESSED_BUTTON_COUNT];
    uint8_t buttonCount = MAX_PRESSED_BUTTON_COUNT;
    auto result = Button::parse(bytes, length, buttons, &buttonCount);
    
    switch (result) {
        case Button::ParseResult::Success:
            processButtonsPressed(buttons, buttonCount);
            break;
        case Button::ParseResult::InvalidPayload:
            break;
        case Button::ParseResult::InvalidArgs:
            Serial.println("Unexpected invalid args failure parsing command.");
            break;
    }
}

void SmartHub::processButtonsPressed(const SmartHub::Button *buttons, size_t count) {
#if VERBOSE
    Serial.println("Button(s) pressed: ");
    for (size_t i = 0; i < count; i++) {
        buttons[i].printDetails();
    }
#else
    Serial.print("Button(s) pressed: [");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            Serial.print(", ");
        }
        Serial.print(buttons[i].name);
    }
    Serial.println("]");
#endif

    triggerCallbacks(buttonPressedCallbacks, activeEndpoint, buttons, count);
}