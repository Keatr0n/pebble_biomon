#pragma once

#include "common.h"

#define HR_HIST_LEN 32
#define STRESS_ELEVATED 50

// What the HUD's one line of status text should be about, most urgent last.
typedef enum {
  STATUS_OK = 0,
  STATUS_NO_DATA,     // Nothing is being measured, so there's nothing to call normal
  STATUS_CALIBRATING,
  STATUS_WORN_DOWN,   // HRV has been sinking for weeks, whatever today's score says
  STATUS_DRAINED,
  STATUS_STRESS,
  STATUS_FOOD,
  STATUS_WATER
} Status;

// Everything the themes draw. A value of -1 means "nothing to show" for that field.
typedef struct {
  char time[6];
  char date[12];
  int hr;
  int hrv;          // RMSSD in ms
  int stress;       // 0-100
  bool stress_high; // High for long enough to be worth an alert
  bool worn_down;
  int energy;       // 0-100, drawn as the HUD's health bar
  int calib_pct;    // How far the HRV baseline is from being usable
  bool calibrating;
  int water;        // 0-100
  int food;
  int steps;
  int sleep_min;
  int battery;
  bool charging;
  Need panel;       // Which meter the log panel is open for
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

// hrv.c
void hrv_refresh(void);
void hrv_reset_baseline(void);

// needs.c
void needs_init(void);
void needs_deinit(void);
// own_reminders: check for reminders here, because no worker is running to do it.
void needs_tick(time_t now, bool own_reminders);
void needs_show_pending_alert(bool exit_when_done);
void needs_tap(GPoint point);
// Returns false if the press wasn't used, so Back can fall through to leaving the app.
bool needs_button(ButtonId button);
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
