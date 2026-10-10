#pragma once

// Everything the app and its background worker both need to agree on: settings, what's
// kept in persistent storage (which the two share), and the messages between them.
#if defined(BIOMON_WORKER)
#include <pebble_worker.h>
#else
#include <pebble.h>
#endif

// The HRV API landed in SDK 4.32; older SDKs still build, they just never report stress.
// Testing the symbol pebble_sdk_version.h generates, rather than going through the
// PBL_API_EXISTS() wrapper, because the wrapper expands to defined() inside an #if.
#if defined(PBL_HEALTH) && defined(_PBL_API_EXISTS_health_service_set_hrv_sample_period)
#define BIOMON_HAS_HRV 1
#endif
#if defined(_PBL_API_EXISTS_touch_service_subscribe)
#define BIOMON_HAS_TOUCH 1
#endif

#define PERSIST_KEY_SETTINGS 1
#define PERSIST_KEY_HRV 3
#define PERSIST_KEY_NEEDS 6
#define PERSIST_KEY_NAG 7
#define PERSIST_KEY_ENERGY 8

#define NEED_PROMPT_PCT 50    // Below this a meter asks to be topped up
#define NEED_CRITICAL_PCT 25  // Below this it's drawn as an emergency

// Worker -> app message types.
#define WORKER_MSG_BODY 1     // A new HRV reading or energy level has been stored
#define WORKER_MSG_ALERT 2    // A reminder is waiting in the nag store

typedef enum {
  THEME_SUBNAUTICA = 0,
  THEME_RUST,
  THEME_CYBERPUNK,
  THEME_COUNT
} Theme;

typedef enum {
  NEED_NONE = 0,
  NEED_WATER,
  NEED_FOOD
} Need;

typedef enum {
  SEX_MALE = 0,
  SEX_FEMALE,
  SEX_OTHER
} Sex;

typedef struct {
  uint8_t theme;
  bool track_water;
  bool track_food;
  bool vibrate;
  uint8_t quiet_start;       // Hour of day; reminders and meter drain pause until quiet_end
  uint8_t quiet_end;
  uint8_t hrv_interval_min;
  uint8_t nag_min;           // 0 = remind once only
  uint8_t sex;
  uint8_t age;
  uint16_t weight_kg;
  uint16_t height_cm;
  bool volume_floz;          // Show fl oz rather than ml
  bool energy_kcal;          // Show kcal rather than kJ
  bool profile_set;          // The body details came from the wearer, not the defaults
} Settings;

// Fills in defaults, then overlays whatever has been saved.
void settings_core_load(Settings *settings);
