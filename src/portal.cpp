#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config/smart_hub.h"
#include "config/mqtt.h"
#include "config/network.h"
#include "defaults.h"
#include "defaults.h"
#include "smart_hub.h"
#include "network.h"
#include "portal_pages.h"
#include "util.h"

static char url[30] = { 0 }; // http://AAA.BBB.CCC.DDD/review\0
static IPAddress ipAddress;
static DNSServer dnsServer;
static bool dnsServerRunning = false;
static WebServer webServer(WEB_SERVER_PORT);
static bool running = false;
static unsigned long scheduledRebootTime = 0;

// Setup progress. Initialized from NVS validity in setup() so a config that
// was saved in a previous session counts as done; a successful save during
// this session also marks its step done. Reboot is gated on both.
static bool networkReady = false;
// Set when a network save actually changes the stored config. Only a
// network change needs a reboot; smart-hub changes apply live.
static bool networkChanged = false;
static bool smartHubReady = false;
static bool mqttReady = false;

// Most recently discovered (scanning) endpoint. Saved to NVS only via
// POST /smart-hub/save so the user can review before committing.
static SmartHub::Endpoint pendingEndpoint = {0, 0};
static bool hasPendingEndpoint = false;
static int8_t scannerCallbackId = -1;
static int8_t buttonCallbackId = -1;

// Currently pressed buttons for the Smart Hub confirm page. Updated on every
// button event; an event with zero buttons means all released (cleared).
static char pressedNames[MAX_PRESSED_BUTTON_COUNT][24];
static uint8_t pressedCount = 0;
static unsigned long pressedMs = 0;
// Release hold: keeps the last pressed state visible briefly after release
// so a tap shorter than the UI poll interval still shows up.
static const unsigned long PRESSED_HOLD_MS = 500;
static bool pressedCleared = false;
static unsigned long pressedClearedMs = 0;

struct WebServerRoute {
    typedef void (*Handler)();

    const char *path;
    const HTTPMethod method;
    Handler handler;
    static WebServerRoute all[];
};

static void redirectToNetworkPage();
static void showReviewPage();
static void showNetworkPage();
static void handleNetworkScan();
static void handleNetworkSave();
static void showSmartHubPage();
static void handleSmartHubStatus();
static void handleSmartHubEvents();
static void handleSmartHubScanStart();
static void handleSmartHubScanStop();
static void handleSmartHubSave();
static void handleSmartHubListen();
static void showMqttPage();
static void handleMqttSave();
static void handleApply();
static void servePortalCss();
static void showOverviewPage();

static void smartHubScannerCallback(const SmartHub::Endpoint endpoint);
static void smartHubButtonCallback(const SmartHub::Endpoint endpoint, const SmartHub::Button *buttons, uint8_t count);
static bool bothConfigsValid();
static bool canReboot();
static String formatAddressHex(uint64_t address);
static bool parseAddress(const String &value, uint64_t &outAddress);
static const char *smartHubStateName();
static String networkLabel();
static String smartHubLabel();

WebServerRoute WebServerRoute::all[] = {
    { .path = "/", .method = HTTP_ANY, .handler = redirectToNetworkPage },
    { .path = "/hotspot-detect.html", .method = HTTP_ANY, .handler = redirectToNetworkPage },
    { .path = "/generate_204", .method = HTTP_ANY, .handler = redirectToNetworkPage },
    { .path = "/portal.css", .method = HTTP_GET, .handler = servePortalCss },
    { .path = "/overview", .method = HTTP_GET, .handler = showOverviewPage },
    { .path = "/network", .method = HTTP_GET, .handler = showNetworkPage },
    { .path = "/scan", .method = HTTP_GET, .handler = handleNetworkScan },
    { .path = "/save", .method = HTTP_POST, .handler = handleNetworkSave },
    { .path = "/network/save", .method = HTTP_POST, .handler = handleNetworkSave },
    { .path = "/smart-hub", .method = HTTP_GET, .handler = showSmartHubPage },
    { .path = "/smart-hub/status", .method = HTTP_GET, .handler = handleSmartHubStatus },
    { .path = "/smart-hub/events", .method = HTTP_GET, .handler = handleSmartHubEvents },
    { .path = "/smart-hub/scan/start", .method = HTTP_POST, .handler = handleSmartHubScanStart },
    { .path = "/smart-hub/scan/stop", .method = HTTP_POST, .handler = handleSmartHubScanStop },
    { .path = "/smart-hub/save", .method = HTTP_POST, .handler = handleSmartHubSave },
    { .path = "/smart-hub/listen", .method = HTTP_POST, .handler = handleSmartHubListen },
    { .path = "/mqtt", .method = HTTP_GET, .handler = showMqttPage },
    { .path = "/mqtt/save", .method = HTTP_POST, .handler = handleMqttSave },
    { .path = "/review", .method = HTTP_GET, .handler = showReviewPage },
    { .path = "/apply", .method = HTTP_POST, .handler = handleApply },
};
static const size_t routeCount = sizeof(WebServerRoute::all) / sizeof(WebServerRoute::all[0]);

static bool setupAP() {
    // Setup WiFi AP mode
    char ssid[WL_SSID_MAX_LENGTH + 1] = { 0 };
    snprintf(ssid, sizeof(ssid), "SmartHub_%s", SmartHub::getUniqueId());

    Serial.print("Starting AP with SSID ");
    Serial.print(ssid);
    Serial.print("...");
    if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP(ssid)) {
        Serial.println(" Failed!");
        return false;
    }
    ipAddress = WiFi.softAPIP();
    Serial.println("Done!");

    // Setup DNS server
    Serial.print("Starting DNS server at ");
    Serial.print(ipAddress);
    Serial.print(":");
    Serial.print(DNS_PORT);
    Serial.print("... ");
    if (!dnsServer.start(DNS_PORT, "*", ipAddress)) {
        Serial.println("Failed!");
        return false;
    }
    dnsServerRunning = true;
    Serial.println(" Done!");
    return true;
}

bool Portal::setup() {
    if (Network::isConnected()) {
        ipAddress = Network::getIPAddress();
    } else if (!setupAP()) {
        return false;
    }

    // Setup Web server
    Serial.print("Starting Web server at ");
    Serial.print(ipAddress);
    Serial.print(":");
    Serial.print(WEB_SERVER_PORT);
    Serial.print("... ");
    for (size_t i = 0; i < routeCount; i++) {
        auto route = WebServerRoute::all[i];
        webServer.on(route.path, route.method, route.handler);
    }
    webServer.onNotFound([]() {
        webServer.sendHeader("Location", String("http://") + ipAddress.toString(), true);
        webServer.send(302, "text/plain", "");
    });
    webServer.begin();
    Serial.println("Done!");

    // Init setup progress from already-saved configs
    {
        auto networkConfig = Config::Network();
        networkConfig.load();
        networkReady = networkConfig.isValid();

        auto smartHubConfig = Config::SmartHub();
        smartHubConfig.load();
        smartHubReady = smartHubConfig.isValid();

        auto mqttConfig = Config::Mqtt();
        mqttConfig.load();
        mqttReady = mqttConfig.isValid();
    }

    // Subscribe to Smart Hub events for the portal UI. Subscribing does not
    // touch the radio; other subscribers are unaffected.
    if (scannerCallbackId < 0) {
        scannerCallbackId = SmartHub::addScannerCallback(&smartHubScannerCallback);
    }
    if (buttonCallbackId < 0) {
        buttonCallbackId = SmartHub::addButtonPressedCallback(&smartHubButtonCallback);
    }

    // 5. Never touch the radio here. Scanning and listening start only
    // from explicit user action on the Smart Hub page.

    running = true;
    return true;
}

void Portal::loop() {
    if (!running) {
        return;
    }
    if (scheduledRebootTime > 0 && (millis() >= scheduledRebootTime)) {
        if (!canReboot()) {
            scheduledRebootTime = 0; // config changed under us; cancel the reboot
        } else {
            WiFi.softAPdisconnect(true);
            delay(1000);
            ESP.restart();
        }
    }
    if (dnsServerRunning) {
        dnsServer.processNextRequest();
    }
    webServer.handleClient();
}

const char *Portal::getUrl() {
    if (url[0] == '\0' && static_cast<uint32_t>(ipAddress) != 0) {
        auto ipAddressString = ipAddress.toString();
        const char *components[3];
        components[0] = "http://";
        components[1] = ipAddressString.c_str();
        components[2] = "/review";
        Util::joinStrings(components, 3, url, sizeof(url));
    }
    return url;
}

#pragma mark - Portal state

static void smartHubScannerCallback(SmartHub::Endpoint endpoint) {
    pendingEndpoint = endpoint;
    hasPendingEndpoint = true;

    Serial.println("Portal captured Smart Hub endpoint.");
    // Start listening right away so remote presses show up on the
    // Smart Hub page before the user saves.
    SmartHub::startListening(endpoint);
}

static void smartHubButtonCallback(const SmartHub::Endpoint endpoint, const SmartHub::Button *buttons, uint8_t count) {
    (void)endpoint;
    if (buttons == nullptr || count == 0) {
        // Empty event: all buttons released. Don't clear yet; the events
        // endpoint holds the state until PRESSED_HOLD_MS elapses.
        if (pressedCount > 0 && !pressedCleared) {
            pressedCleared = true;
            pressedClearedMs = millis();
        }
        return;
    }
    pressedCount = 0;
    pressedMs = millis();
    pressedCleared = false;
    for (uint8_t i = 0; i < count && pressedCount < MAX_PRESSED_BUTTON_COUNT; i++) {
        const char *name = buttons[i].name ? buttons[i].name : "Unknown";
        strncpy(pressedNames[pressedCount], name, sizeof(pressedNames[0]) - 1);
        pressedNames[pressedCount][sizeof(pressedNames[0]) - 1] = '\0';
        pressedCount++;
    }
}

static bool bothConfigsValid() {
    auto networkConfig = Config::Network();
    networkConfig.load();
    auto smartHubConfig = Config::SmartHub();
    smartHubConfig.load();
    auto mqttConfig = Config::Mqtt();
    mqttConfig.load();
    return networkConfig.isValid() && smartHubConfig.isValid() && mqttConfig.isValid();
}

static bool canReboot() {
    return networkReady && smartHubReady && mqttReady && bothConfigsValid();
}

static String formatAddressHex(uint64_t address) {
    char buf[13];
    snprintf(buf, sizeof(buf), "0x%010llX", (unsigned long long)(address & 0xFFFFFFFFFFULL));
    return String(buf);
}

static String jsonEscape(const String &value) {
    String out = "";
    for (size_t i = 0; i < value.length(); i++) {
        char c = value.charAt(i);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out += c;
        }
    }
    return out;
}

static bool parseAddress(const String &value, uint64_t &outAddress) {
    String v = value;
    v.trim();
    if (v.startsWith("0x") || v.startsWith("0X")) {
        v = v.substring(2);
    }
    if (v.length() == 0 || v.length() > 10) {
        return false;
    }
    for (size_t i = 0; i < v.length(); i++) {
        if (!isxdigit((unsigned char)v.charAt(i))) {
            return false;
        }
    }
    outAddress = strtoull(v.c_str(), nullptr, 16);
    return true;
}

static const char *smartHubStateName() {
    switch (SmartHub::getState()) {
        case SmartHub::State::NotReady:
            return "NotReady";
        case SmartHub::State::Idle:
            return "Idle";
        case SmartHub::State::Scanning:
            return "Scanning";
        case SmartHub::State::Listening:
            return "Listening";
    }
    return "Unknown";
}

static String networkLabel() {
    auto networkConfig = Config::Network();
    networkConfig.load();
    if (!networkConfig.isValid()) {
        return "Not configured";
    }
    if (networkConfig.interface == Config::Network::Interface::WiFi) {
        return "Configured (Wi-Fi: " + String(networkConfig.wifi.ssid) + ")";
    }
    return "Configured (Ethernet)";
}

static String smartHubLabel() {
    auto smartHubConfig = Config::SmartHub();
    smartHubConfig.load();
    if (smartHubConfig.endpoint.isValid()) {
        return "Configured (channel " + String(smartHubConfig.endpoint.channel) +
            ", address " + formatAddressHex(smartHubConfig.endpoint.address) + ")";
    }
    if (hasPendingEndpoint) {
        return "Hub found but not saved (channel " + String(pendingEndpoint.channel) +
            ", address " + formatAddressHex(pendingEndpoint.address) + ")";
    }
    return "Not configured";
}

#pragma mark - Static pages

// Pages are static files under portal/, embedded into flash at build time by
// scripts/embed_portal.py. All dynamic content loads via the JSON APIs below,
// so serving a page never builds a large String in heap.

void redirectToNetworkPage() {
    webServer.sendHeader("Location", "/network", true);
    webServer.send(302, "text/plain", "");
}

void showReviewPage() {
    webServer.send_P(200, "text/html", PORTAL_REVIEW_HTML);
}

void showNetworkPage() {
    webServer.send_P(200, "text/html", PORTAL_NETWORK_HTML);
}

void showSmartHubPage() {
    webServer.send_P(200, "text/html", PORTAL_SMART_HUB_HTML);
}

void showMqttPage() {
    webServer.send_P(200, "text/html", PORTAL_MQTT_HTML);
}

void servePortalCss() {
    webServer.send_P(200, "text/css", PORTAL_CSS);
}

void showOverviewPage() {
    auto networkConfig = Config::Network();
    networkConfig.load();
    auto smartHubConfig = Config::SmartHub();
    smartHubConfig.load();
    auto mqttConfig = Config::Mqtt();
    mqttConfig.load();

    String interface = "none";
    if (networkConfig.interface == Config::Network::Interface::WiFi) {
        interface = "wifi";
    } else if (networkConfig.interface == Config::Network::Interface::Ethernet) {
        interface = "ethernet";
    }

    String json = "{\"network\":{\"valid\":";
    json += (networkConfig.isValid() ? "true" : "false");
    json += ",\"label\":\"" + jsonEscape(networkLabel()) + "\"";
    json += ",\"interface\":\"" + interface + "\"";
    json += ",\"ssid\":\"" + jsonEscape(String(networkConfig.wifi.ssid)) + "\"";
    json += ",\"password\":\"" + jsonEscape(String(networkConfig.wifi.password)) + "\"}";
    json += ",\"smartHub\":{\"valid\":";
    json += (smartHubConfig.endpoint.isValid() ? "true" : "false");
    json += ",\"label\":\"" + jsonEscape(smartHubLabel()) + "\"";
    json += ",\"channel\":" + String(smartHubConfig.endpoint.channel);
    json += ",\"address\":\"" + formatAddressHex(smartHubConfig.endpoint.address) + "\"}";
    json += ",\"mqtt\":{\"valid\":";
    json += (mqttConfig.isValid() ? "true" : "false");
    json += ",\"ipAddress\":\"" + mqttConfig.ipAddress.toString() + "\"";
    json += ",\"port\":" + String(mqttConfig.port);
    json += ",\"username\":\"" + jsonEscape(String(mqttConfig.username)) + "\"";
    json += ",\"password\":\"" + jsonEscape(String(mqttConfig.password)) + "\"}";
    json += ",\"mqttReady\":" + String(mqttReady ? "true" : "false");
    json += ",\"networkReady\":" + String(networkReady ? "true" : "false");
    json += ",\"smartHubReady\":" + String(smartHubReady ? "true" : "false");
    json += ",\"canReboot\":" + String(canReboot() ? "true" : "false");
    json += ",\"rebootRequired\":" + String(networkChanged ? "true" : "false");
    json += "}";
    webServer.send(200, "application/json", json);
}

#pragma mark - Network step

void handleNetworkScan() {
    int n = WiFi.scanNetworks(false, false, false, 150);

    String jsonResponse = "[";
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            jsonResponse += "{";
            jsonResponse += "\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
            jsonResponse += "\"channel\":\"" + String(WiFi.channel(i)) + "\",";
            jsonResponse += "\"rssi\":" + String(WiFi.RSSI(i));
            jsonResponse += "}";
            if (i < n - 1) jsonResponse += ",";
        }
    }
    jsonResponse += "]";

    WiFi.scanDelete();
    webServer.send(200, "application/json", jsonResponse);
}

void handleNetworkSave() {
    auto config = Config::Network();
    auto interface = webServer.arg("interface");
    auto ssid = webServer.arg("ssid");
    auto password = webServer.arg("password");

    if (interface == "ethernet") {
        config.interface = Config::Network::Interface::Ethernet;
    } else if (
        interface == "wifi" &&
        ssid.length() < sizeof(config.wifi.ssid) &&
        password.length() < sizeof(config.wifi.password)
    ) {
        config.interface = Config::Network::Interface::WiFi;
        strcpy(config.wifi.ssid, ssid.c_str());
        strcpy(config.wifi.password, password.c_str());
    }

    if (!config.isValid()) {
        webServer.send(400, "text/plain", "Bad Request: Invalid config");
        return;
    }
    auto current = Config::Network();
    current.load();
    networkChanged =
        current.interface != config.interface ||
        strcmp(current.wifi.ssid, config.wifi.ssid) != 0 ||
        strcmp(current.wifi.password, config.wifi.password) != 0;
    config.save();
    networkReady = true;

    // Advance to the next setup step; reboot happens from Review.
    webServer.sendHeader("Location", "/smart-hub", true);
    webServer.send(303, "text/plain", "");
}

#pragma mark - Smart Hub APIs

void handleSmartHubStatus() {
    auto smartHubConfig = Config::SmartHub();
    smartHubConfig.load();

    String json = "{";
    json += "\"state\":\"" + String(smartHubStateName()) + "\",";
    json += "\"scanning\":";
    json += (SmartHub::getState() == SmartHub::State::Scanning ? "true" : "false");
    json += ",\"listening\":";
    json += (SmartHub::getState() == SmartHub::State::Listening ? "true" : "false");
    json += ",\"saved\":{\"channel\":" + String(smartHubConfig.endpoint.channel);
    json += ",\"address\":\"" + formatAddressHex(smartHubConfig.endpoint.address) + "\"";
    json += ",\"valid\":" + String(smartHubConfig.endpoint.isValid() ? "true" : "false") + "}";
    json += ",\"pending\":{\"present\":" + String(hasPendingEndpoint ? "true" : "false");
    json += ",\"channel\":" + String(pendingEndpoint.channel);
    json += ",\"address\":\"" + formatAddressHex(pendingEndpoint.address) + "\"";
    json += ",\"valid\":" + String(pendingEndpoint.isValid() ? "true" : "false") + "}";
    json += ",\"networkReady\":" + String(networkReady ? "true" : "false");
    json += ",\"smartHubReady\":" + String(smartHubReady ? "true" : "false");
    json += ",\"canReboot\":" + String(canReboot() ? "true" : "false");
    json += "}";
    webServer.send(200, "application/json", json);
}

void handleSmartHubEvents() {
    if (pressedCleared && millis() - pressedClearedMs > PRESSED_HOLD_MS) {
        pressedCount = 0;
        pressedCleared = false;
    }
    String json = "{\"pressed\":[";
    for (uint8_t i = 0; i < pressedCount; i++) {
        if (i > 0) {
            json += ",";
        }
        json += "\"" + String(pressedNames[i]) + "\"";
    }
    json += "],\"t\":" + String(pressedMs) + "}";
    webServer.send(200, "application/json", json);
}

void handleSmartHubScanStart() {
    if (SmartHub::getState() == SmartHub::State::Scanning) {
        webServer.send(200, "application/json", "{\"started\":false,\"reason\":\"already scanning\"}");
        return;
    }
    if (SmartHub::getState() == SmartHub::State::Listening) {
        SmartHub::stopListening();
    }
    if (SmartHub::getState() != SmartHub::State::Idle) {
        webServer.send(500, "application/json", "{\"started\":false,\"reason\":\"radio busy\"}");
        return;
    }
    hasPendingEndpoint = false;
    if (!SmartHub::startScanning()) {
        webServer.send(500, "application/json", "{\"started\":false,\"reason\":\"radio busy\"}");
        return;
    }
    webServer.send(200, "application/json", "{\"started\":true}");
}

void handleSmartHubScanStop() {
    if (SmartHub::getState() == SmartHub::State::Scanning) {
        SmartHub::stopScanning();
        webServer.send(200, "application/json", "{\"stopped\":true}");
    } else {
        webServer.send(200, "application/json", "{\"stopped\":false}");
    }
}

void handleSmartHubListen() {
    // Make sure the radio is listening on the saved endpoint so the Review
    // page test reflects the saved settings. Restarts the radio when it is
    // idle or listening on a different (e.g. scanned-but-unsaved) endpoint;
    // an in-progress scan is left alone.
    auto config = Config::SmartHub();
    config.load();

    bool listening = false;
    if (config.endpoint.isValid()) {
        if (SmartHub::getState() == SmartHub::State::Listening &&
            SmartHub::getActiveEndpoint().channel == config.endpoint.channel &&
            SmartHub::getActiveEndpoint().address == config.endpoint.address) {
            listening = true;
        } else if (SmartHub::getState() != SmartHub::State::Scanning) {
            SmartHub::stopListening();
            listening = SmartHub::startListening(config.endpoint);
        }
    }
    webServer.send(200, "application/json", listening ? "{\"listening\":true}" : "{\"listening\":false}");
}

void handleSmartHubSave() {
    SmartHub::Endpoint endpoint = {0, 0};
    String channelArg = webServer.arg("channel");
    String addressArg = webServer.arg("address");

    if (channelArg.length() == 0 && addressArg.length() == 0 && hasPendingEndpoint) {
        endpoint = pendingEndpoint;
    } else {
        long channel = channelArg.toInt();
        uint64_t address = 0;
        if (channelArg.length() == 0 || channel < 0 || channel > 124 || !parseAddress(addressArg, address)) {
            webServer.send(400, "text/plain", "Bad Request: Invalid channel or address");
            return;
        }
        endpoint.channel = (uint8_t)channel;
        endpoint.address = address;
    }

    if (!endpoint.isValid()) {
        webServer.send(400, "text/plain", "Bad Request: Invalid channel or address");
        return;
    }

    auto config = Config::SmartHub();
    config.load();
    config.endpoint = endpoint;
    config.save();
    smartHubReady = true;
    hasPendingEndpoint = false;

    // Listen on the saved endpoint so commands keep flowing to the page.
    if (SmartHub::getState() == SmartHub::State::Scanning) {
        SmartHub::stopScanning();
    } else if (SmartHub::getState() == SmartHub::State::Listening) {
        SmartHub::stopListening();
    }
    SmartHub::startListening(endpoint);

    webServer.sendHeader("Location", "/mqtt", true);
    webServer.send(303, "text/plain", "");
}

#pragma mark - MQTT step

void handleMqttSave() {
    auto config = Config::Mqtt();
    String ipArg = webServer.arg("ipAddress");
    String portArg = webServer.arg("port");
    String usernameArg = webServer.arg("username");
    String passwordArg = webServer.arg("password");

    IPAddress ip;
    long port = portArg.toInt();
    if (!ip.fromString(ipArg) || static_cast<uint32_t>(ip) == 0 ||
        portArg.length() == 0 || port <= 0 || port > 65535 ||
        usernameArg.length() > MQTT_USERNAME_MAX_LENGTH ||
        passwordArg.length() > MQTT_PASSWORD_MAX_LENGTH) {
        webServer.send(400, "text/plain", "Bad Request: Invalid MQTT broker settings");
        return;
    }

    config.load();
    config.ipAddress = ip;
    config.port = (uint16_t)port;
    strcpy(config.username, usernameArg.c_str());
    strcpy(config.password, passwordArg.c_str());
    config.save();
    mqttReady = true;

    // Advance to the next setup step; reboot happens from Review.
    webServer.sendHeader("Location", "/review", true);
    webServer.send(303, "text/plain", "");
}

#pragma mark - Apply

void handleApply() {
    if (!canReboot()) {
        webServer.send(400, "text/plain", "Bad Request: Save Network, Smart Hub and MQTT configs first");
        return;
    }

    // Only a network change needs a reboot; anything else is already live.
    // Same grace period as the HA reboot button: let the response flush.
    if (networkChanged) {
        webServer.send_P(200, "text/html", PORTAL_REBOOT_HTML);
        scheduledRebootTime = millis() + HA_REBOOT_DELAY_MS;
    } else {
        webServer.send_P(200, "text/html", PORTAL_APPLIED_HTML);
    }
}
