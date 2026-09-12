#ifndef VERBOSE
#define VERBOSE (CORE_DEBUG_LEVEL >= 5 ? 1 : 0)
#endif

#ifndef CE_PIN
#define CE_PIN 4
#endif

#ifndef CSN_PIN
#define CSN_PIN 5
#endif

#ifndef USE_WIRED
#define USE_WIRED 0
#endif

#ifndef MAX_PRESSED_BUTTON_COUNT
#define MAX_PRESSED_BUTTON_COUNT 6
#endif

#ifndef WL_SSID_MAX_LENGTH
#define WL_SSID_MAX_LENGTH 32
#endif

#ifndef WL_WPA_KEY_MAX_LENGTH
#define WL_WPA_KEY_MAX_LENGTH 63
#endif

#ifndef DNS_PORT
#define DNS_PORT 53
#endif

#ifndef WEB_SERVER_PORT
#define WEB_SERVER_PORT 80
#endif