#include "biomon.h"

// Cyberpunk 2077: a biomonitor readout in the game's yellow, red and cyan - cut-corner
// panels, segmented bars, a chromatic-aberration clock and a pulse trace.
#define CP_BG GColorBlack
#define CP_YELLOW GColorYellow
#define CP_RED GColorFolly
#define CP_CYAN GColorCyan
#define CP_TRACK GColorBulgarianRose
#define CP_DIM GColorDarkGray

#define FOOTER_CELLS 4
#define GLITCH_OFFSET 2

typedef struct {
  GRect header;
  int16_t time_y;
  GRect bars;        // Height is one row
  int16_t bar_gap;
  int16_t label_w;
  int16_t value_w;
  GRect trace;
  GRect status;
  GRect cell;        // The first footer cell
} CyberLayout;

#if defined(PBL_ROUND)
static const CyberLayout LAYOUT = {
  .header = { { 66, 16 }, { 128, 18 } },
  .time_y = 26,
  .bars = { { 32, 94 }, { 196, 14 } }, .bar_gap = 5, .label_w = 36, .value_w = 34,
  .trace = { { 28, 150 }, { 204, 36 } },
  .status = { { 28, 190 }, { 204, 16 } },
  .cell = { { 56, 207 }, { 37, 28 } }
};
#else
static const CyberLayout LAYOUT = {
  .header = { { 4, 4 }, { 192, 18 } },
  .time_y = 14,
  .bars = { { 6, 82 }, { 188, 14 } }, .bar_gap = 5, .label_w = 36, .value_w = 34,
  .trace = { { 4, 140 }, { 192, 36 } },
  .status = { { 4, 180 }, { 192, 16 } },
  .cell = { { 4, 197 }, { 48, 28 } }
};
#endif

static void prv_label(GContext *ctx, const char *text, GRect box, GTextAlignment align,
                      GColor color) {
  // Chakra's glyphs sit a few pixels below the top of their box at this size.
  box.origin.y -= 4;
  box.size.h += 6;
  draw_text(ctx, text, g_font_chakra_label, box, align, color);
}

static GRect prv_row_rect(int index) {
  GRect row = LAYOUT.bars;
  row.origin.y += index * (row.size.h + LAYOUT.bar_gap);
  return row;
}

static void prv_bar_row(GContext *ctx, int index, const char *label, int pct, GColor color) {
  GRect row = prv_row_rect(index);
  bool low = pct >= 0 && pct < NEED_CRITICAL_PCT;

  prv_label(ctx, label, GRect(row.origin.x, row.origin.y, LAYOUT.label_w, row.size.h),
            GTextAlignmentLeft, color);
  GRect bar = GRect(row.origin.x + LAYOUT.label_w, row.origin.y + 2,
                    row.size.w - LAYOUT.label_w - LAYOUT.value_w, row.size.h - 4);
  draw_segments(ctx, bar, pct, 5, 2, low ? CP_RED : color, CP_DIM);

  char value[8];
  fmt_value(value, sizeof(value), pct);
  prv_label(ctx, value, GRect(row.origin.x + row.size.w - LAYOUT.value_w, row.origin.y,
                              LAYOUT.value_w, row.size.h), GTextAlignmentRight,
            low ? CP_RED : CP_YELLOW);
}

static void prv_status(GContext *ctx) {
  static char s_calibrating[24];
  const char *text = "ALL SYSTEMS NOMINAL";
  bool alarm = true;

  switch (bio_status()) {
    case STATUS_WATER:
      text = g_bio.water < NEED_CRITICAL_PCT ? "HYDRATION CRITICAL" : "HYDRATION LOW";
      break;
    case STATUS_FOOD:
      text = g_bio.food < NEED_CRITICAL_PCT ? "NUTRIENTS DEPLETED" : "NUTRIENTS LOW";
      break;
    case STATUS_STRESS: text = "STRESS SPIKE"; break;
    case STATUS_DRAINED: text = "RESERVES DEPLETED"; break;
    case STATUS_WORN_DOWN: text = "CHRONIC OVERLOAD"; break;
    case STATUS_NO_DATA:
      text = "BIOSENSOR OFFLINE";
      alarm = false;
      break;
    case STATUS_CALIBRATING:
      snprintf(s_calibrating, sizeof(s_calibrating), "SYNCING BASELINE %d%%", g_bio.calib_pct);
      text = s_calibrating;
      alarm = false;
      break;
    case STATUS_OK:
      alarm = false;
      break;
  }

  if (alarm) {
    draw_cut_rect(ctx, LAYOUT.status, 6, CP_RED, true);
    prv_label(ctx, text, LAYOUT.status, GTextAlignmentCenter, CP_BG);
  } else {
    prv_label(ctx, text, LAYOUT.status, GTextAlignmentCenter, CP_CYAN);
  }
}

static void prv_footer_cell(GContext *ctx, int index, const char *label, const char *value) {
  GRect cell = LAYOUT.cell;
  cell.origin.x += index * cell.size.w;
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GRect(cell.origin.x, cell.origin.y - 3, cell.size.w, 16), GTextAlignmentCenter,
            CP_CYAN);
  draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(cell.origin.x, cell.origin.y + 9, cell.size.w, 16), GTextAlignmentCenter,
            CP_YELLOW);
}

GRect theme_cyberpunk_need_rect(Need need) {
  return prv_row_rect(need == NEED_WATER ? 1 : 2);
}

void theme_cyberpunk_draw(GContext *ctx, GRect bounds) {
  int width = bounds.size.w;

  graphics_context_set_fill_color(ctx, CP_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  draw_cut_rect(ctx, LAYOUT.header, 8, CP_YELLOW, true);
  GRect header_text = GRect(LAYOUT.header.origin.x + 6, LAYOUT.header.origin.y,
                            LAYOUT.header.size.w - 18, LAYOUT.header.size.h);
#if defined(PBL_ROUND)
  prv_label(ctx, g_bio.date, header_text, GTextAlignmentCenter, CP_BG);
#else
  prv_label(ctx, "BIOMON", header_text, GTextAlignmentLeft, CP_BG);
  prv_label(ctx, g_bio.date, header_text, GTextAlignmentRight, CP_BG);
#endif

  // The clock is drawn three times, the red and cyan copies pulled apart sideways.
  GRect time_box = GRect(0, LAYOUT.time_y, width, 64);
  time_box.origin.x = -GLITCH_OFFSET;
  draw_text(ctx, g_bio.time, g_font_chakra_time, time_box, GTextAlignmentCenter, CP_CYAN);
  time_box.origin.x = GLITCH_OFFSET;
  draw_text(ctx, g_bio.time, g_font_chakra_time, time_box, GTextAlignmentCenter, CP_RED);
  time_box.origin.x = 0;
  draw_text(ctx, g_bio.time, g_font_chakra_time, time_box, GTextAlignmentCenter, CP_YELLOW);

  prv_bar_row(ctx, 0, "VIT", bio_vitality(), CP_RED);
  prv_bar_row(ctx, 1, "H2O", g_bio.water, CP_CYAN);
  prv_bar_row(ctx, 2, "NUT", g_bio.food, CP_YELLOW);

  GRect trace = LAYOUT.trace;
  draw_cut_rect(ctx, trace, 8, CP_CYAN, false);
  int readout_w = 62;
  draw_sparkline(ctx, GRect(trace.origin.x + 6, trace.origin.y + 6,
                            trace.size.w - readout_w - 12, trace.size.h - 12),
                 g_bio.hr_hist, g_bio.hr_hist_len, CP_RED);
  char value[8];
  fmt_value(value, sizeof(value), g_bio.hr);
  int readout_x = trace.origin.x + trace.size.w - readout_w;
  draw_text(ctx, value, fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS),
            GRect(readout_x, trace.origin.y + 1, readout_w - 10, 22), GTextAlignmentCenter,
            CP_YELLOW);
  draw_text(ctx, "BPM", fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GRect(readout_x, trace.origin.y + trace.size.h - 18, readout_w - 10, 16),
            GTextAlignmentCenter, CP_CYAN);

  prv_status(ctx);

  fmt_value(value, sizeof(value), g_bio.hrv);
  prv_footer_cell(ctx, 0, "HRV", value);
  fmt_count(value, sizeof(value), g_bio.steps);
  prv_footer_cell(ctx, 1, "STEPS", value);
  fmt_hours(value, sizeof(value), g_bio.sleep_min);
  prv_footer_cell(ctx, 2, "SLEEP", value);
  snprintf(value, sizeof(value), "%d%%", g_bio.battery);
  prv_footer_cell(ctx, 3, "PWR", value);
}
