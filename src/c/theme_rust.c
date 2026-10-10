#include "biomon.h"

// Rust: the three stacked vitals bars from the bottom-right of the HUD, a status effect
// box above them and the belt bar underneath holding the remaining readouts.
#define RUST_BG GColorBlack
#define RUST_PANEL GColorDarkGray
#define RUST_TRACK GColorBlack
#define RUST_TEXT GColorWhite
#define RUST_DIM GColorLightGray
#define RUST_HEALTH GColorKellyGreen
#define RUST_WATER GColorVividCerulean
#define RUST_FOOD GColorChromeYellow
#define RUST_GOOD GColorDarkGreen
#define RUST_BAD GColorDarkCandyAppleRed
#define RUST_PROMPT GColorWindsorTan

#define BELT_SLOTS 5
#define ICON_COLUMN_W 22

typedef struct {
  int16_t date_y;
  int16_t date_rule_inset;
  int16_t time_y;
  GRect status;
  GRect bars;      // Height is one row
  int16_t bar_gap;
  GRect slot;      // The first belt slot
  int16_t slot_gap;
} RustLayout;

#if defined(PBL_ROUND)
static const RustLayout LAYOUT = {
  .date_y = 16, .date_rule_inset = 66, .time_y = 28,
  .status = { { 40, 92 }, { 180, 20 } },
  .bars = { { 34, 116 }, { 192, 20 } }, .bar_gap = 3,
  .slot = { { 44, 188 }, { 33, 36 } }, .slot_gap = 2
};
#else
static const RustLayout LAYOUT = {
  .date_y = 0, .date_rule_inset = 8, .time_y = 14,
  .status = { { 6, 76 }, { 188, 20 } },
  .bars = { { 6, 100 }, { 188, 22 } }, .bar_gap = 3,
  .slot = { { 6, 178 }, { 36, 44 } }, .slot_gap = 2
};
#endif

static void prv_status(GContext *ctx) {
  static char s_value[12];
  const char *label = "COMFORT";
  GColor color = RUST_GOOD;
  s_value[0] = '\0';

  switch (bio_status()) {
    case STATUS_WATER:
      label = g_bio.water < NEED_CRITICAL_PCT ? "DEHYDRATED" : "THIRSTY";
      color = RUST_BAD;
      snprintf(s_value, sizeof(s_value), "%d%%", g_bio.water);
      break;
    case STATUS_FOOD:
      label = g_bio.food < NEED_CRITICAL_PCT ? "STARVING" : "HUNGRY";
      color = RUST_BAD;
      snprintf(s_value, sizeof(s_value), "%d%%", g_bio.food);
      break;
    case STATUS_STRESS:
      label = "STRESSED";
      color = RUST_BAD;
      snprintf(s_value, sizeof(s_value), "%d", g_bio.stress);
      break;
    case STATUS_DRAINED:
      label = "EXHAUSTED";
      color = RUST_BAD;
      snprintf(s_value, sizeof(s_value), "%d%%", g_bio.energy);
      break;
    case STATUS_WORN_DOWN:
      label = "WORN DOWN";
      color = RUST_PROMPT;
      if (g_bio.stress >= 0) snprintf(s_value, sizeof(s_value), "%d", g_bio.stress);
      break;
    case STATUS_NO_DATA:
      label = "NO SIGNAL";
      color = RUST_PANEL;
      break;
    case STATUS_CALIBRATING:
      label = "CALIBRATING";
      color = RUST_PANEL;
      snprintf(s_value, sizeof(s_value), "%d%%", g_bio.calib_pct);
      break;
    case STATUS_OK:
      if (g_bio.stress >= STRESS_ELEVATED) {
        label = "TENSE";
        color = RUST_PROMPT;
      } else if (g_bio.stress < 0) {
        label = "STABLE";
        color = RUST_PANEL;
      }
      if (g_bio.stress >= 0) snprintf(s_value, sizeof(s_value), "%d", g_bio.stress);
      break;
  }

  GRect box = LAYOUT.status;
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, box, 0, GCornerNone);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GRect text = GRect(box.origin.x + 6, box.origin.y, box.size.w - 12, box.size.h);
  draw_text(ctx, label, font, text, GTextAlignmentLeft, RUST_TEXT);
  draw_text(ctx, s_value, font, text, GTextAlignmentRight, RUST_TEXT);
}

static GRect prv_row_rect(int index) {
  GRect row = LAYOUT.bars;
  row.origin.y += index * (row.size.h + LAYOUT.bar_gap);
  return row;
}

static void prv_vital_row(GContext *ctx, int index, Icon icon, int pct, GColor fill) {
  GRect row = prv_row_rect(index);

  graphics_context_set_fill_color(ctx, RUST_PANEL);
  graphics_fill_rect(ctx, row, 0, GCornerNone);
  draw_icon(ctx, icon, GPoint(row.origin.x + (ICON_COLUMN_W - ICON_SIZE) / 2,
                              row.origin.y + (row.size.h - ICON_SIZE) / 2), RUST_DIM);

  GRect bar = GRect(row.origin.x + ICON_COLUMN_W, row.origin.y + 2,
                    row.size.w - ICON_COLUMN_W - 2, row.size.h - 4);
  draw_bar(ctx, bar, pct, fill, RUST_TRACK);

  char value[8];
  fmt_value(value, sizeof(value), pct);
  draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GRect(bar.origin.x + 5, bar.origin.y + (bar.size.h - 18) / 2 - 4, 60, 22),
            GTextAlignmentLeft, RUST_TEXT);
}

static void prv_belt_slot(GContext *ctx, int index, const char *label, const char *value) {
  GRect slot = LAYOUT.slot;
  slot.origin.x += index * (slot.size.w + LAYOUT.slot_gap);

  graphics_context_set_fill_color(ctx, RUST_PANEL);
  graphics_fill_rect(ctx, slot, 0, GCornerNone);
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GRect(slot.origin.x, slot.origin.y - 1, slot.size.w, 16), GTextAlignmentCenter,
            RUST_DIM);
  draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GRect(slot.origin.x - 2, slot.origin.y + slot.size.h - 26, slot.size.w + 4, 22),
            GTextAlignmentCenter, RUST_TEXT);
}

GRect theme_rust_need_rect(Need need) {
  return prv_row_rect(need == NEED_WATER ? 1 : 2);
}

void theme_rust_draw(GContext *ctx, GRect bounds) {
  graphics_context_set_fill_color(ctx, RUST_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int rule_y = LAYOUT.date_y + 14;
  int rule_w = bounds.size.w / 2 - LAYOUT.date_rule_inset - 50;
  graphics_context_set_stroke_color(ctx, RUST_PANEL);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(LAYOUT.date_rule_inset, rule_y),
                     GPoint(LAYOUT.date_rule_inset + rule_w, rule_y));
  graphics_draw_line(ctx, GPoint(bounds.size.w - LAYOUT.date_rule_inset - rule_w, rule_y),
                     GPoint(bounds.size.w - LAYOUT.date_rule_inset, rule_y));
  draw_text(ctx, g_bio.date, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GRect(0, LAYOUT.date_y, bounds.size.w, 22), GTextAlignmentCenter, RUST_DIM);
  draw_text(ctx, g_bio.time, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49),
            GRect(0, LAYOUT.time_y, bounds.size.w, 56), GTextAlignmentCenter, RUST_TEXT);

  prv_status(ctx);
  prv_vital_row(ctx, 0, ICON_CROSS, bio_vitality(), RUST_HEALTH);
  prv_vital_row(ctx, 1, ICON_DROP, g_bio.water, RUST_WATER);
  prv_vital_row(ctx, 2, ICON_FOOD, g_bio.food, RUST_FOOD);

  char value[8];
  fmt_value(value, sizeof(value), g_bio.hr);
  prv_belt_slot(ctx, 0, "BPM", value);
  fmt_value(value, sizeof(value), g_bio.hrv);
  prv_belt_slot(ctx, 1, "HRV", value);
  fmt_count(value, sizeof(value), g_bio.steps);
  prv_belt_slot(ctx, 2, "STEP", value);
  fmt_hours(value, sizeof(value), g_bio.sleep_min);
  prv_belt_slot(ctx, 3, "ZZZ", value);
  fmt_value(value, sizeof(value), g_bio.battery);
  prv_belt_slot(ctx, 4, "PWR", value);
}
