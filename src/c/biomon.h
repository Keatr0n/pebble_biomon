#pragma once

#include <pebble.h>

#define HR_HIST_LEN 32
#define NEED_PROMPT_PCT 50    // Below this a meter asks to be topped up
#define NEED_CRITICAL_PCT 25  // Below this it's drawn as an emergency
#define STRESS_HIGH 75
#define STRESS_ELEVATED 50
#define STRESS_NORMAL 25

// The HRV API landed in SDK 4.32; older SDKs still build, they just never report stress.
// Testing the symbol pebble_sdk_version.h generates, rather than going through the
// PBL_API_EXISTS() wrapper, because the wrapper expands to defined() inside an #if.
#if defined(PBL_HEALTH) && defined(_PBL_API_EXISTS_health_service_set_hrv_sample_period)
#define BIOMON_HAS_HRV 1
#endif
#if defined(_PBL_API_EXISTS_touch_service_subscribe)
#define BIOMON_HAS_TOUCH 1
#endif

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

// What the face's one line of status text should be about, most urgent last.
typedef enum {
  STATUS_OK = 0,
  STATUS_CALIBRATING,
  STATUS_STRESS,
  STATUS_FOOD,
  STATUS_WATER
} Status;

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
} Settings;

// Everything the themes draw. A value of -1 means "nothing to show" for that field.
typedef struct {
  char time[6];
  char date[12];
  int hr;
  int hrv;          // RMSSD in ms
  int stress;       // 0-100
  int calib_pct;    // How far the HRV baseline is from being usable
  bool calibrating;
  int water;        // 0-100
  int food;
  int steps;
  int sleep_min;
  int battery;
  bool charging;
  Need panel;       // Which meter the log panel is open for
  bool panel_shake; // The panel is being driven by wrist shakes rather than taps
  int panel_count;  // Cups or food portions it's about to log; negative takes them back
  uint8_t hr_hist[HR_HIST_LEN];
  int hr_hist_len;
} Bio;

extern Settings g_settings;
extern Bio g_bio;
extern GFont g_font_chakra_time;
extern GFont g_font_chakra_label;

// main.c
void ui_redraw(void);

// settings.c
void settings_load(void);
bool settings_handle_message(DictionaryIterator *iter);

// vitals.c
void vitals_init(void);
void vitals_deinit(void);
void vitals_tick(void);
bool vitals_is_asleep(void);
bool vitals_is_moving(void);

// hrv.c
void hrv_init(void);
void hrv_deinit(void);
void hrv_tick(time_t now);
void hrv_on_event(void);
void hrv_reset_baseline(void);

// needs.c
void needs_init(void);
void needs_deinit(void);
void needs_tick(time_t now);
void needs_tap(GPoint point);
void needs_shake(void);
void needs_panel_draw(GContext *ctx, GRect bounds);

// draw.c
#define ICON_SIZE 11
typedef const char *const Icon[ICON_SIZE];
extern Icon ICON_HEART;
extern Icon ICON_DROP;
extern Icon ICON_FOOD;
extern Icon ICON_APPLE;
extern Icon ICON_CROSS;

Status bio_status(void);
int bio_vitality(void);
void draw_text(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align,
               GColor color);
void draw_bar(GContext *ctx, GRect rect, int pct, GColor fill, GColor track);
void draw_segments(GContext *ctx, GRect rect, int pct, int seg_w, int gap, GColor fill,
                   GColor track);
void draw_ring(GContext *ctx, GPoint center, int radius, int thickness, int pct, GColor fill,
               GColor track);
void draw_icon(GContext *ctx, Icon icon, GPoint origin, GColor color);
void draw_cut_rect(GContext *ctx, GRect rect, int cut, GColor color, bool filled);
void draw_sparkline(GContext *ctx, GRect rect, const uint8_t *values, int count, GColor color);
void fmt_value(char *buf, size_t size, int value);
void fmt_count(char *buf, size_t size, int value);
void fmt_hours(char *buf, size_t size, int minutes);

// theme_*.c
void theme_subnautica_draw(GContext *ctx, GRect bounds);
void theme_rust_draw(GContext *ctx, GRect bounds);
void theme_cyberpunk_draw(GContext *ctx, GRect bounds);
// Where each theme draws a meter, so a tap can be matched to it.
GRect theme_subnautica_need_rect(Need need);
GRect theme_rust_need_rect(Need need);
GRect theme_cyberpunk_need_rect(Need need);
