#include "biomon.h"

// Subnautica: the survival gauge cluster from the corner of the HUD - one large gauge
// with three small ones curling round it on a dark backing - with the depth-meter pill
// and a PDA voice line above.
#define SUB_BG GColorCobaltBlue
#define SUB_BACKING GColorOxfordBlue
#define SUB_TEXT GColorWhite
#define SUB_LABEL GColorCeleste
#define SUB_RIM GColorMediumAquamarine
#define SUB_CORE GColorDarkGray
#define SUB_ACCENT GColorChromeYellow
#define SUB_WARN GColorSunsetOrange
#define SUB_PULSE GColorSpringBud
#define SUB_PULSE_TRACK GColorDarkGreen
#define SUB_HEALTH GColorRed
#define SUB_HEALTH_TRACK GColorBulgarianRose
#define SUB_FOOD GColorChromeYellow
#define SUB_FOOD_TRACK GColorWindsorTan
#define SUB_WATER GColorPictonBlue
#define SUB_WATER_TRACK GColorDukeBlue

#define PULSE_MIN_BPM 40
#define PULSE_MAX_BPM 180
#define PILL_W 100
#define PILL_H 18
#define PULSE_R 38
#define GAUGE_R 21
#define BACKING_PAD 5
#define READOUTS 4

typedef struct {
  int16_t pill_y;
  int16_t time_y;
  int16_t status_y;
  GPoint pulse;
  GRect readout;     // The first of the stacked readouts
  int16_t readout_step;
} SubLayout;

#if defined(PBL_ROUND)
static const SubLayout LAYOUT = {
  .pill_y = 18, .time_y = 30, .status_y = 80,
  .pulse = { 119, 146 },
  .readout = { { 166, 108 }, { 58, 26 } }, .readout_step = 26
};
#else
static const SubLayout LAYOUT = {
  .pill_y = 4, .time_y = 16, .status_y = 67,
  .pulse = { 100, 130 },
  .readout = { { 146, 92 }, { 52, 28 } }, .readout_step = 28
};
#endif

typedef enum {
  GAUGE_HEALTH = 0,
  GAUGE_FOOD,
  GAUGE_WATER,
  GAUGE_COUNT
} Gauge;

// Where each small gauge sits relative to the large one, tracing the same arc the game's
// cluster does: level with it, below-left, and underneath.
static const GPoint GAUGE_OFFSET[GAUGE_COUNT] = { { -71, 1 }, { -51, 49 }, { -5, 70 } };

static GPoint prv_gauge_center(Gauge gauge) {
  return GPoint(LAYOUT.pulse.x + GAUGE_OFFSET[gauge].x, LAYOUT.pulse.y + GAUGE_OFFSET[gauge].y);
}

static const char *prv_status_text(GColor *color) {
  static char s_calibrating[28];
  *color = SUB_WARN;

  switch (bio_status()) {
    case STATUS_WATER: return "SEEK FLUID INTAKE";
    case STATUS_FOOD: return "SEEK CALORIC INTAKE";
    case STATUS_STRESS: return "ELEVATED STRESS DETECTED";
    case STATUS_DRAINED: return "REST REQUIRED";
    case STATUS_WORN_DOWN: return "CHRONIC STRESS DETECTED";
    case STATUS_NO_DATA:
      *color = SUB_LABEL;
      return "NO BIOSIGNAL DETECTED";
    case STATUS_CALIBRATING:
      *color = SUB_ACCENT;
      snprintf(s_calibrating, sizeof(s_calibrating), "LEARNING BASELINE %d%%", g_bio.calib_pct);
      return s_calibrating;
    case STATUS_OK:
    default:
      *color = SUB_LABEL;
      return "VITAL SIGNS NORMAL";
  }
}

static void prv_disc(GContext *ctx, GPoint center, int radius, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, center, radius);
}

static void prv_gauge(GContext *ctx, Gauge gauge, Icon icon, int pct, GColor color,
                      GColor track) {
  GPoint center = prv_gauge_center(gauge);
  bool critical = pct >= 0 && pct < NEED_CRITICAL_PCT;

  // A gauge in trouble gets a warning halo; otherwise the same thin rim as the big one.
  prv_disc(ctx, center, GAUGE_R, critical ? SUB_WARN : SUB_RIM);
  draw_ring(ctx, center, GAUGE_R - 2, 7, pct, color, track);
  prv_disc(ctx, center, GAUGE_R - 9, SUB_BACKING);
  draw_icon(ctx, icon, GPoint(center.x - ICON_SIZE / 2, center.y - ICON_SIZE / 2), SUB_TEXT);
}

static void prv_readout(GContext *ctx, int index, const char *label, const char *value) {
  GRect box = LAYOUT.readout;
  box.origin.y += index * LAYOUT.readout_step;
  draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GRect(box.origin.x, box.origin.y - 4, box.size.w, 16), GTextAlignmentCenter,
            SUB_LABEL);
  draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GRect(box.origin.x, box.origin.y + 5, box.size.w, 22), GTextAlignmentCenter,
            SUB_TEXT);
}

GRect theme_subnautica_need_rect(Need need) {
  GPoint center = prv_gauge_center(need == NEED_WATER ? GAUGE_WATER : GAUGE_FOOD);
  return GRect(center.x - GAUGE_R, center.y - GAUGE_R, GAUGE_R * 2, GAUGE_R * 2);
}

void theme_subnautica_draw(GContext *ctx, GRect bounds) {
  int width = bounds.size.w;
  int center_x = width / 2;

  graphics_context_set_fill_color(ctx, SUB_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  GRect pill = GRect(center_x - PILL_W / 2, LAYOUT.pill_y, PILL_W, PILL_H);
  graphics_context_set_fill_color(ctx, SUB_BACKING);
  graphics_fill_rect(ctx, pill, PILL_H / 2, GCornersAll);
  graphics_context_set_stroke_color(ctx, SUB_RIM);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, pill, PILL_H / 2);
  draw_text(ctx, g_bio.date, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(pill.origin.x, pill.origin.y - 1, PILL_W, 16), GTextAlignmentCenter, SUB_TEXT);

  draw_text(ctx, g_bio.time, fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS),
            GRect(0, LAYOUT.time_y, width, 46), GTextAlignmentCenter, SUB_TEXT);

  GColor status_color;
  const char *status = prv_status_text(&status_color);
  graphics_context_set_fill_color(ctx, SUB_BACKING);
  graphics_fill_rect(ctx, GRect(0, LAYOUT.status_y + 1, width, 17), 0, GCornerNone);
  draw_text(ctx, status, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(0, LAYOUT.status_y, width, 16), GTextAlignmentCenter, status_color);

  // The backing is just a larger dark disc behind every gauge; they're spaced so the
  // discs touch and read as a single blob.
  prv_disc(ctx, LAYOUT.pulse, PULSE_R + BACKING_PAD + 1, SUB_BACKING);
  for (int i = 0; i < GAUGE_COUNT; i++) {
    prv_disc(ctx, prv_gauge_center(i), GAUGE_R + BACKING_PAD, SUB_BACKING);
  }

  int pulse_pct = g_bio.hr < 0 ? 0
      : ((g_bio.hr - PULSE_MIN_BPM) * 100) / (PULSE_MAX_BPM - PULSE_MIN_BPM);
  prv_disc(ctx, LAYOUT.pulse, PULSE_R, SUB_RIM);
  draw_ring(ctx, LAYOUT.pulse, PULSE_R - 2, 10, pulse_pct, SUB_PULSE, SUB_PULSE_TRACK);
  prv_disc(ctx, LAYOUT.pulse, PULSE_R - 12, SUB_RIM);
  prv_disc(ctx, LAYOUT.pulse, PULSE_R - 13, SUB_CORE);
  char value[16];
  fmt_value(value, sizeof(value), g_bio.hr);
  draw_text(ctx, "BPM", fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GRect(LAYOUT.pulse.x - PULSE_R, LAYOUT.pulse.y - 24, PULSE_R * 2, 16),
            GTextAlignmentCenter, SUB_TEXT);
  draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(LAYOUT.pulse.x - PULSE_R, LAYOUT.pulse.y - 13, PULSE_R * 2, 28),
            GTextAlignmentCenter, SUB_TEXT);

  prv_gauge(ctx, GAUGE_HEALTH, ICON_HEART, bio_vitality(), SUB_HEALTH, SUB_HEALTH_TRACK);
  prv_gauge(ctx, GAUGE_FOOD, ICON_APPLE, g_bio.food, SUB_FOOD, SUB_FOOD_TRACK);
  prv_gauge(ctx, GAUGE_WATER, ICON_DROP, g_bio.water, SUB_WATER, SUB_WATER_TRACK);

  if (g_bio.hrv < 0) {
    snprintf(value, sizeof(value), "--");
  } else {
    snprintf(value, sizeof(value), "%dMS", g_bio.hrv);
  }
  prv_readout(ctx, 0, "HRV", value);
  fmt_count(value, sizeof(value), g_bio.steps);
  prv_readout(ctx, 1, "STEPS", value);
  fmt_hours(value, sizeof(value), g_bio.sleep_min);
  prv_readout(ctx, 2, "SLEEP", value);
  snprintf(value, sizeof(value), "%d%%", g_bio.battery);
  prv_readout(ctx, 3, "POWER", value);
}
