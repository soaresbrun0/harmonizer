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

#ifndef MQTT_DEFAULT_PORT
#define MQTT_DEFAULT_PORT 1883
#endif

#ifndef MQTT_USERNAME_MAX_LENGTH
#define MQTT_USERNAME_MAX_LENGTH 64
#endif

#ifndef MQTT_PASSWORD_MAX_LENGTH
#define MQTT_PASSWORD_MAX_LENGTH 128
#endif

#ifndef DNS_PORT
#define DNS_PORT 53
#endif

#ifndef WEB_SERVER_PORT
#define WEB_SERVER_PORT 80
#endif

#ifndef HA_ENTITY_VALUE_MAX_LENGTH
#define HA_ENTITY_VALUE_MAX_LENGTH 128
#endif

// 6 buttons x 24 chars + commas, with room to spare.
#ifndef HA_PRESSED_BUTTONS_VALUE_MAX_LENGTH
#define HA_PRESSED_BUTTONS_VALUE_MAX_LENGTH 160
#endif

#ifndef HA_METRICS_REPORT_INTERVAL_MS
#define HA_METRICS_REPORT_INTERVAL_MS 1000UL
#endif

// Fixed expiry for expiring sensors, deliberately decoupled from the
// report interval: reporting pauses while the remote is in use, so an
// interval-derived timeout would false-trip mid-navigation.
#ifndef HA_SENSOR_STALE_AFTER_S
#define HA_SENSOR_STALE_AFTER_S 30
#endif

#ifndef HA_MIN_HEALTHY_HEAP_PERCENT
#define HA_MIN_HEALTHY_HEAP_PERCENT 10
#endif

// Quiet period with no button activity before metric reporting resumes.
// Reports pause while the remote is in use so publish bursts never stall
// radio polling on a weak link.
#ifndef HA_METRICS_REPORT_RESUME_DELAY_MS
#define HA_METRICS_REPORT_RESUME_DELAY_MS 3000UL
#endif

#ifndef HA_MIN_HEALTHY_PSRAM_PERCENT
#define HA_MIN_HEALTHY_PSRAM_PERCENT 10
#endif

#ifndef HA_REBOOT_DELAY_MS
#define HA_REBOOT_DELAY_MS 1000UL
#endif
