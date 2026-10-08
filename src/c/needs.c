#include "biomon.h"

// Water and food are game-style meters that drain while you're awake and are topped up
// by logging a drink or a meal, which is done by tapping the meter.
//
// The drain is deliberately uneven. The top half goes quickly, so a meter reaches the
// "top me up" line a couple of hours after a drink; below that it slows right down,
// because being a bit short is uncomfortable for a long time before it's dangerous.
// Filling is even: a cup or a meal is always worth the same slice of bar, sized from the
// wearer's daily requirement so that eating and drinking a normal day's worth keeps the
// meters cycling through their top half.
//
// Logging is done by shaking the wrist: the first shake opens a picker, each further
// shake steps to the next amount, and holding still for a few seconds logs whatever is
// showing. Shakes happen by accident all day, which is why the first one selects nothing.
#define PERSIST_KEY_NEEDS 5
#define LEVEL_FULL 1000000     // Meter level in ten-thousandths of a percent
#define LEVEL_PER_PCT (LEVEL_FULL / 100)
#define CUP_ML 250
#define KJ_STEP_DKJ 5000       // 500 kJ; energy is stored in tenths of a kJ so that
#define KCAL_STEP_DKJ 4184     // 100 kcal lands on a whole number too
#define KJ_DEFAULT_STEPS 4
#define KCAL_DEFAULT_STEPS 5
#define WATER_MAX_STEPS 9
#define FOOD_MAX_STEPS 20
#define DEFAULT_WAKING_SEC (16 * SECONDS_PER_HOUR)
#define QUIET_LOOKBACK_DAYS 4
#define DAY_RESET_GAP_SEC (20 * SECONDS_PER_HOUR)
#define PANEL_TIMEOUT_MS 15000
#define SHAKE_CONFIRM_MS 4000   // Holding still this long accepts the picker's choice
#define SHAKE_DEBOUNCE_MS 700   // One shake can fire several tap events
#define TAP_SLOP 8             // Meters are thin; forgive a near miss

// ml of water per kg of body weight, and the constant term of the Mifflin-St Jeor
// resting energy equation, indexed by Sex. "Other" sits halfway between the two.
static const int WATER_ML_PER_KG[] = { 35, 31, 33 };
static const int MIFFLIN_CONSTANT[] = { 5, -161, -78 };
#define ACTIVITY_FACTOR_X10 14  // Lightly active

// One stretch of a drain curve: how long the meter takes to fall from the previous
// stretch's floor (or from full) to this one's, in eighths of a waking day.
typedef struct {
  int32_t floor;
  int32_t eighths;
} DrainStretch;

#define DRAIN_STRETCHES 4
// With a 16 hour day: 100-50% in 2 h, 50-25% in 6 h, 25-10% in a day, 10-0% in two more.
static const DrainStretch WATER_DRAIN[DRAIN_STRETCHES] = {
  { 50 * LEVEL_PER_PCT, 1 }, { 25 * LEVEL_PER_PCT, 3 }, { 10 * LEVEL_PER_PCT, 8 }, { 0, 16 }
};
// 100-50% in 4 h, 50-25% in 8 h, 25-10% in two days, 10-0% in six more.
static const DrainStretch FOOD_DRAIN[DRAIN_STRETCHES] = {
  { 50 * LEVEL_PER_PCT, 2 }, { 25 * LEVEL_PER_PCT, 4 }, { 10 * LEVEL_PER_PCT, 16 }, { 0, 48 }
};
// The top half of each bar is worth the requirement for the time it takes to drain, so
// a whole bar holds this fraction of a day's requirement.
#define WATER_BAR_DAY_FRACTION 4
#define FOOD_BAR_DAY_FRACTION 2

typedef struct {
  time_t updated_at;     // When the levels were last drained up to
  int32_t water_level;
  int32_t food_level;
  time_t day_start;      // The waking day the totals below belong to
  int32_t water_ml;
  int32_t food_dkj;
  time_t water_nag_at;
  time_t food_nag_at;
  bool water_low;
  bool food_low;
} NeedsStore;

typedef struct {
  GColor bg;
  GColor line;
  GColor text;
  GColor accent;
} PanelColors;

// One amount the shake picker can land on, in cups or in food steps for each energy unit.
typedef struct {
  Need need;
  int8_t steps;
  int8_t steps_kcal;
} ShakeOption;

static const ShakeOption SHAKE_OPTIONS[] = {
  { NEED_WATER, 1, 1 }, { NEED_WATER, 2, 2 },
  { NEED_FOOD, 2, 3 }, { NEED_FOOD, 4, 5 }, { NEED_FOOD, 6, 7 }
};
#define SHAKE_NOTHING (-1)

static NeedsStore s_store;
static AppTimer *s_panel_timer = NULL;
static int s_shake_option = SHAKE_NOTHING;
static uint64_t s_last_shake_ms = 0;

static const uint32_t FOOD_VIBE_SEGMENTS[] = { 120, 100, 120, 100, 300 };

#if defined(PBL_ROUND)
static const GRect PANEL = { { 40, 40 }, { 180, 180 } };
#else
static const GRect PANEL = { { 10, 24 }, { 180, 180 } };
#endif
// Button frames, relative to the panel.
static const GRect BUTTON_MINUS = { { 8, 92 }, { 78, 38 } };
static const GRect BUTTON_PLUS = { { 94, 92 }, { 78, 38 } };
static const GRect BUTTON_CANCEL = { { 8, 136 }, { 78, 38 } };
static const GRect BUTTON_CONFIRM = { { 94, 136 }, { 78, 38 } };

static uint64_t prv_now_ms(void) {
  time_t seconds;
  uint16_t ms;
  time_ms(&seconds, &ms);
  return (uint64_t)seconds * 1000 + ms;
}

static void prv_save(void) {
  persist_write_data(PERSIST_KEY_NEEDS, &s_store, sizeof(s_store));
}

static int32_t prv_water_need_ml(void) {
  return (int32_t)g_settings.weight_kg * WATER_ML_PER_KG[g_settings.sex];
}

static int32_t prv_food_need_dkj(void) {
  // Mifflin-St Jeor: 10w + 6.25h - 5a + c, kept as four times itself to stay in integers.
  int32_t bmr_x4 = 40 * g_settings.weight_kg + 25 * g_settings.height_cm - 20 * g_settings.age +
                   4 * MIFFLIN_CONSTANT[g_settings.sex];
  int32_t kcal = (bmr_x4 * ACTIVITY_FACTOR_X10) / 40;
  return (kcal * 4184) / 100;
}

static bool prv_quiet_enabled(void) {
  return g_settings.quiet_start != g_settings.quiet_end;
}

static bool prv_in_quiet_hours(void) {
  if (!prv_quiet_enabled()) return false;
  int start = g_settings.quiet_start;
  int end = g_settings.quiet_end;

  time_t now = time(NULL);
  int hour = localtime(&now)->tm_hour;
  return start < end ? (hour >= start && hour < end) : (hour >= start || hour < end);
}

static int32_t prv_waking_seconds(void) {
  if (!prv_quiet_enabled()) return DEFAULT_WAKING_SEC;
  int hours = (g_settings.quiet_start - g_settings.quiet_end + 24) % 24;
  return hours * SECONDS_PER_HOUR;
}

// Seconds between two times, not counting any that fell inside quiet hours. Nobody eats
// or drinks in their sleep, so the meters shouldn't be on the floor every morning.
static int32_t prv_awake_seconds(time_t from, time_t to) {
  if (to <= from) return 0;
  int32_t total = (int32_t)(to - from);
  if (!prv_quiet_enabled() || total > QUIET_LOOKBACK_DAYS * SECONDS_PER_DAY) return total;

  int start = g_settings.quiet_start;
  int end = g_settings.quiet_end;
  int32_t quiet = 0;
  time_t midnight = time_start_of_today();
  for (int day = 0; day <= QUIET_LOOKBACK_DAYS; day++) {
    time_t day_start = midnight - day * SECONDS_PER_DAY;
    time_t quiet_from = day_start + start * SECONDS_PER_HOUR;
    time_t quiet_to = day_start + end * SECONDS_PER_HOUR + (end < start ? SECONDS_PER_DAY : 0);

    time_t overlap_from = quiet_from > from ? quiet_from : from;
    time_t overlap_to = quiet_to < to ? quiet_to : to;
    if (overlap_to > overlap_from) quiet += (int32_t)(overlap_to - overlap_from);
  }
  return total - quiet;
}

static int32_t prv_drain(int32_t level, int32_t seconds, const DrainStretch *curve) {
  int32_t eighth = prv_waking_seconds() / 8;
  int32_t top = LEVEL_FULL;

  for (int i = 0; i < DRAIN_STRETCHES && seconds > 0; i++) {
    int32_t floor = curve[i].floor;
    if (level > floor) {
      int32_t span = top - floor;
      int32_t duration = curve[i].eighths * eighth;
      int32_t to_floor = (int32_t)(((int64_t)(level - floor) * duration) / span);
      if (seconds < to_floor) return level - (int32_t)(((int64_t)seconds * span) / duration);
      seconds -= to_floor;
      level = floor;
    }
    top = floor;
  }
  return level;
}

// How much bar an amount is worth, given the day's requirement and what fraction of a
// day a full bar represents.
static int32_t prv_fill(int32_t amount, int32_t day_need, int bar_day_fraction) {
  return (int32_t)(((int64_t)amount * bar_day_fraction * LEVEL_FULL) / day_need);
}

static int32_t prv_clamp_level(int32_t level) {
  if (level < 0) return 0;
  if (level > LEVEL_FULL) return LEVEL_FULL;
  return level;
}

// The running totals shown in the log panel belong to a waking day, which starts when
// quiet hours end so that someone up past midnight doesn't lose theirs mid-evening.
static void prv_roll_day(time_t now) {
  int wake_hour = prv_quiet_enabled() ? g_settings.quiet_end : 0;
  time_t day_start = time_start_of_today() + wake_hour * SECONDS_PER_HOUR;
  if (day_start > now) day_start -= SECONDS_PER_DAY;

  // Changing the quiet hours shifts the boundary by a few hours; only a real new day,
  // or a clock that has jumped backwards, clears the totals.
  if (day_start - s_store.day_start < DAY_RESET_GAP_SEC && s_store.day_start <= now) return;
  s_store.day_start = day_start;
  s_store.water_ml = 0;
  s_store.food_dkj = 0;
}

static void prv_publish(void) {
  g_bio.water = g_settings.track_water ? s_store.water_level / LEVEL_PER_PCT : -1;
  g_bio.food = g_settings.track_food ? s_store.food_level / LEVEL_PER_PCT : -1;
}

static void prv_update_levels(time_t now) {
  // A clock change can leave the last update in the future; start again from now.
  if (s_store.updated_at > now) s_store.updated_at = now;

  int32_t awake = prv_awake_seconds(s_store.updated_at, now);
  s_store.water_level = prv_drain(s_store.water_level, awake, WATER_DRAIN);
  s_store.food_level = prv_drain(s_store.food_level, awake, FOOD_DRAIN);
  s_store.updated_at = now;
  prv_roll_day(now);
  prv_publish();
}

static void prv_remind(Need need, time_t now) {
  int pct = need == NEED_WATER ? g_bio.water : g_bio.food;
  bool low = pct >= 0 && pct < NEED_PROMPT_PCT;
  bool *was_low = need == NEED_WATER ? &s_store.water_low : &s_store.food_low;
  time_t *nag_at = need == NEED_WATER ? &s_store.water_nag_at : &s_store.food_nag_at;

  if (!low) {
    *was_low = false;
    return;
  }
  if (prv_in_quiet_hours() || vitals_is_asleep()) return;

  // Buzz on first dropping under the line, then on a timer for as long as it stays there.
  bool repeat = g_settings.nag_min > 0 &&
                (now - *nag_at) >= g_settings.nag_min * SECONDS_PER_MINUTE;
  if (*was_low && !repeat) return;

  *was_low = true;
  *nag_at = now;
  light_enable_interaction();
  if (!g_settings.vibrate || quiet_time_is_active()) return;

  if (need == NEED_WATER) {
    vibes_double_pulse();
  } else {
    vibes_enqueue_custom_pattern((VibePattern) {
      .durations = FOOD_VIBE_SEGMENTS,
      .num_segments = ARRAY_LENGTH(FOOD_VIBE_SEGMENTS)
    });
  }
}

// --- Log panel ---

static int32_t prv_food_step_dkj(void) {
  return g_settings.energy_kcal ? KCAL_STEP_DKJ : KJ_STEP_DKJ;
}

static void prv_confirm_panel(void);

static void prv_close_panel(void) {
  if (s_panel_timer) app_timer_cancel(s_panel_timer);
  s_panel_timer = NULL;
  g_bio.panel = NEED_NONE;
  g_bio.panel_shake = false;
  ui_redraw();
}

static void prv_panel_timeout(void *data) {
  s_panel_timer = NULL;
  if (g_bio.panel_shake && g_bio.panel != NEED_NONE) {
    prv_confirm_panel();
  } else {
    prv_close_panel();
  }
}

static void prv_keep_panel_open(uint32_t timeout_ms) {
  if (s_panel_timer) {
    app_timer_reschedule(s_panel_timer, timeout_ms);
  } else {
    s_panel_timer = app_timer_register(timeout_ms, prv_panel_timeout, NULL);
  }
}

static void prv_open_panel(Need need) {
  g_bio.panel = need;
  g_bio.panel_shake = false;
  if (need == NEED_WATER) {
    g_bio.panel_count = 1;
  } else {
    g_bio.panel_count = g_settings.energy_kcal ? KCAL_DEFAULT_STEPS : KJ_DEFAULT_STEPS;
  }
  prv_keep_panel_open(PANEL_TIMEOUT_MS);
  ui_redraw();
}

static void prv_step_panel(int delta) {
  int limit = g_bio.panel == NEED_WATER ? WATER_MAX_STEPS : FOOD_MAX_STEPS;
  int count = g_bio.panel_count + delta;
  // Zero would log nothing, so step straight over it into taking an entry back.
  if (count == 0) count += delta;
  if (count < -limit || count > limit) return;
  g_bio.panel_count = count;
  ui_redraw();
}

static void prv_confirm_panel(void) {
  // Drain up to this moment first, so the top-up lands on the level as it is now.
  prv_update_levels(time(NULL));
  if (g_bio.panel == NEED_WATER) {
    int32_t ml = g_bio.panel_count * CUP_ML;
    s_store.water_ml += ml;
    if (s_store.water_ml < 0) s_store.water_ml = 0;
    s_store.water_level = prv_clamp_level(
        s_store.water_level + prv_fill(ml, prv_water_need_ml(), WATER_BAR_DAY_FRACTION));
  } else {
    int32_t dkj = g_bio.panel_count * prv_food_step_dkj();
    s_store.food_dkj += dkj;
    if (s_store.food_dkj < 0) s_store.food_dkj = 0;
    s_store.food_level = prv_clamp_level(
        s_store.food_level + prv_fill(dkj, prv_food_need_dkj(), FOOD_BAR_DAY_FRACTION));
  }
  prv_save();
  prv_publish();

  if (g_settings.vibrate && !quiet_time_is_active()) vibes_short_pulse();
  prv_close_panel();
}

static GRect prv_need_rect(Need need) {
  switch (g_settings.theme) {
    case THEME_RUST: return theme_rust_need_rect(need);
    case THEME_CYBERPUNK: return theme_cyberpunk_need_rect(need);
    case THEME_SUBNAUTICA:
    default: return theme_subnautica_need_rect(need);
  }
}

static int32_t prv_distance_sq(GPoint point, GRect rect) {
  GPoint center = grect_center_point(&rect);
  int32_t dx = point.x - center.x;
  int32_t dy = point.y - center.y;
  return dx * dx + dy * dy;
}

// The meter a tap landed on. The padded hit areas of neighbouring meters overlap, so
// when a tap is inside both it goes to whichever centre is closer.
static Need prv_need_at(GPoint point) {
  Need hit = NEED_NONE;
  int32_t hit_distance = 0;
  const Need needs[] = { NEED_WATER, NEED_FOOD };

  for (unsigned i = 0; i < ARRAY_LENGTH(needs); i++) {
    bool tracked = needs[i] == NEED_WATER ? g_settings.track_water : g_settings.track_food;
    if (!tracked) continue;

    GRect rect = prv_need_rect(needs[i]);
    GRect padded = grect_inset(rect, GEdgeInsets(-TAP_SLOP));
    if (!grect_contains_point(&padded, &point)) continue;

    int32_t distance = prv_distance_sq(point, rect);
    if (hit == NEED_NONE || distance < hit_distance) {
      hit = needs[i];
      hit_distance = distance;
    }
  }
  return hit;
}

static bool prv_button_hit(GRect button, GPoint point) {
  button.origin.x += PANEL.origin.x;
  button.origin.y += PANEL.origin.y;
  return grect_contains_point(&button, &point);
}

void needs_tap(GPoint point) {
  if (g_bio.panel == NEED_NONE && !g_bio.panel_shake) {
    Need need = prv_need_at(point);
    if (need != NEED_NONE) prv_open_panel(need);
    return;
  }

  if (g_bio.panel_shake) return;
  prv_keep_panel_open(PANEL_TIMEOUT_MS);
  if (prv_button_hit(BUTTON_MINUS, point)) {
    prv_step_panel(-1);
  } else if (prv_button_hit(BUTTON_PLUS, point)) {
    prv_step_panel(1);
  } else if (prv_button_hit(BUTTON_CONFIRM, point)) {
    prv_confirm_panel();
  } else if (prv_button_hit(BUTTON_CANCEL, point)) {
    prv_close_panel();
  }
}

static bool prv_tracked(Need need) {
  return need == NEED_WATER ? g_settings.track_water : g_settings.track_food;
}

// The picker offers the emptier meter's amounts first, so the common case - topping up
// whatever just asked - is the fewest shakes away.
static Need prv_first_need(void) {
  if (!g_settings.track_water) return NEED_FOOD;
  if (!g_settings.track_food) return NEED_WATER;
  return g_bio.food < g_bio.water ? NEED_FOOD : NEED_WATER;
}

// Steps the picker on to its next amount, or back to nothing once it has shown them all.
static void prv_next_shake_option(void) {
  Need first = prv_first_need();
  int total = ARRAY_LENGTH(SHAKE_OPTIONS);
  int visited = 0;

  // Walk the table as two passes, the first need's entries and then the other's.
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < total; i++) {
      const ShakeOption *option = &SHAKE_OPTIONS[i];
      if ((option->need == first) != (pass == 0) || !prv_tracked(option->need)) continue;
      if (visited++ <= s_shake_option) continue;

      s_shake_option = visited - 1;
      g_bio.panel = option->need;
      g_bio.panel_count = g_settings.energy_kcal ? option->steps_kcal : option->steps;
      return;
    }
  }
  s_shake_option = SHAKE_NOTHING;
  g_bio.panel = NEED_NONE;
}

void needs_shake(void) {
  uint64_t now_ms = prv_now_ms();
  if ((now_ms - s_last_shake_ms) < SHAKE_DEBOUNCE_MS) return;
  s_last_shake_ms = now_ms;

  if (!g_bio.panel_shake) {
    // Leave a panel opened by tapping alone.
    if (g_bio.panel != NEED_NONE) return;
    if (!g_settings.track_water && !g_settings.track_food) return;
    g_bio.panel_shake = true;
    s_shake_option = SHAKE_NOTHING;
  } else {
    prv_next_shake_option();
  }
  prv_keep_panel_open(SHAKE_CONFIRM_MS);
  ui_redraw();
}

static void prv_fmt_volume(char *buf, size_t size, int32_t ml, bool precise) {
  if (!g_settings.volume_floz) {
    snprintf(buf, size, "%d ML", (int)ml);
    return;
  }
  int32_t tenths = (ml * 10000 + 14787) / 29574;
  if (precise) {
    snprintf(buf, size, "%d.%d FL OZ", (int)(tenths / 10), (int)(tenths % 10));
  } else {
    snprintf(buf, size, "%d FL OZ", (int)((tenths + 5) / 10));
  }
}

static int32_t prv_energy_display(int32_t dkj) {
  return g_settings.energy_kcal ? (dkj * 100 + 2092) / 4184 : dkj / 10;
}

static void prv_button(GContext *ctx, GRect frame, const char *label, GFont font, int text_dy,
                       PanelColors colors, bool primary) {
  frame.origin.x += PANEL.origin.x;
  frame.origin.y += PANEL.origin.y;
  if (primary) {
    graphics_context_set_fill_color(ctx, colors.accent);
    graphics_fill_rect(ctx, frame, 3, GCornersAll);
  } else {
    graphics_context_set_stroke_color(ctx, colors.line);
    graphics_draw_round_rect(ctx, frame, 3);
  }
  draw_text(ctx, label, font, GRect(frame.origin.x, frame.origin.y + text_dy, frame.size.w, 34),
            GTextAlignmentCenter, primary ? colors.bg : colors.text);
}

void needs_panel_draw(GContext *ctx, GRect bounds) {
  if (g_bio.panel == NEED_NONE && !g_bio.panel_shake) return;

  static const PanelColors COLORS[THEME_COUNT] = {
    [THEME_SUBNAUTICA] = { { GColorOxfordBlueARGB8 }, { GColorVividCeruleanARGB8 },
                           { GColorCelesteARGB8 }, { GColorChromeYellowARGB8 } },
    [THEME_RUST] = { { GColorBlackARGB8 }, { GColorLightGrayARGB8 }, { GColorWhiteARGB8 },
                     { GColorKellyGreenARGB8 } },
    [THEME_CYBERPUNK] = { { GColorBlackARGB8 }, { GColorCyanARGB8 }, { GColorYellowARGB8 },
                          { GColorFollyARGB8 } }
  };
  PanelColors colors = COLORS[g_settings.theme];
  bool water = g_bio.panel == NEED_WATER;
  int count = g_bio.panel_count;
  int magnitude = count < 0 ? -count : count;

  graphics_context_set_fill_color(ctx, colors.bg);
  graphics_fill_rect(ctx, PANEL, 4, GCornersAll);
  graphics_context_set_stroke_color(ctx, colors.line);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, PANEL, 4);

  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont medium = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont large = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  int x = PANEL.origin.x;
  int y = PANEL.origin.y;
  int w = PANEL.size.w;

  if (g_bio.panel == NEED_NONE) {
    draw_text(ctx, "LOG INTAKE", medium, GRect(x, y, w, 22), GTextAlignmentCenter, colors.line);
    draw_text(ctx, "SHAKE", large, GRect(x, y + 40, w, 32), GTextAlignmentCenter, colors.text);
    draw_text(ctx, "TO PICK AN AMOUNT", medium, GRect(x, y + 74, w, 22), GTextAlignmentCenter,
              colors.text);
    draw_text(ctx, "HOLD STILL TO CANCEL", small, GRect(x, y + 140, w, 16),
              GTextAlignmentCenter, colors.line);
    return;
  }

  char amount[20];
  char detail[20];
  char today[40];
  if (water) {
    snprintf(amount, sizeof(amount), "%s%d CUP%s", count < 0 ? "-" : "", magnitude,
             magnitude == 1 ? "" : "S");
    prv_fmt_volume(detail, sizeof(detail), magnitude * CUP_ML, true);
    char taken[12];
    char need[12];
    prv_fmt_volume(taken, sizeof(taken), s_store.water_ml, false);
    prv_fmt_volume(need, sizeof(need), prv_water_need_ml(), false);
    snprintf(today, sizeof(today), "TODAY %s OF %s", taken, need);
  } else {
    const char *unit = g_settings.energy_kcal ? "KCAL" : "KJ";
    snprintf(amount, sizeof(amount), "%s%d %s", count < 0 ? "-" : "",
             (int)prv_energy_display(magnitude * prv_food_step_dkj()), unit);
    detail[0] = '\0';
    snprintf(today, sizeof(today), "TODAY %d OF %d %s", (int)prv_energy_display(s_store.food_dkj),
             (int)prv_energy_display(prv_food_need_dkj()), unit);
  }

  const char *title = water ? (count < 0 ? "REMOVE WATER" : "LOG WATER")
                            : (count < 0 ? "REMOVE FOOD" : "LOG FOOD");
  draw_text(ctx, title, medium, GRect(x, y, w, 22), GTextAlignmentCenter, colors.line);
  draw_text(ctx, amount, large, GRect(x, y + 16, w, 32), GTextAlignmentCenter, colors.text);
  draw_text(ctx, detail, small, GRect(x, y + 48, w, 16), GTextAlignmentCenter, colors.text);
  draw_text(ctx, today, small, GRect(x, y + 66, w, 16), GTextAlignmentCenter, colors.line);

  if (g_bio.panel_shake) {
    draw_text(ctx, "SHAKE: NEXT", medium, GRect(x, y + 100, w, 22), GTextAlignmentCenter,
              colors.text);
    draw_text(ctx, "HOLD STILL: LOG IT", medium, GRect(x, y + 126, w, 22),
              GTextAlignmentCenter, colors.accent);
    return;
  }
  prv_button(ctx, BUTTON_MINUS, "-", large, -2, colors, false);
  prv_button(ctx, BUTTON_PLUS, "+", large, -2, colors, false);
  prv_button(ctx, BUTTON_CANCEL, "CANCEL", medium, 5, colors, false);
  prv_button(ctx, BUTTON_CONFIRM, "CONFIRM", medium, 5, colors, true);
}

void needs_init(void) {
  NeedsStore stored;
  if (persist_read_data(PERSIST_KEY_NEEDS, &stored, sizeof(stored)) == (int)sizeof(stored)) {
    s_store = stored;
  } else {
    s_store = (NeedsStore) {
      .updated_at = time(NULL), .water_level = LEVEL_FULL, .food_level = LEVEL_FULL
    };
  }
  prv_update_levels(time(NULL));
}

void needs_deinit(void) {
  prv_save();
}

void needs_tick(time_t now) {
  prv_update_levels(now);
  prv_remind(NEED_WATER, now);
  prv_remind(NEED_FOOD, now);
}
