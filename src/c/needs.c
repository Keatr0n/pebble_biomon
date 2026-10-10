#include "biomon.h"
#include "needs_core.h"

// The app's side of the water and food meters: showing them, the panel for logging a
// drink or a meal, and presenting reminders. The meter maths is in shared/needs_core.c.
//
// The log panel opens from a tap on a meter or from Up (water), Down (food) or Select
// (whichever is emptier). Inside it Up and Down change the amount, Select logs it and
// Back cancels; the on-screen buttons do the same for fingers.
#define CUP_ML 250
#define KJ_STEP_DKJ 5000       // 500 kJ
#define KCAL_STEP_DKJ 4184     // 100 kcal
#define KJ_DEFAULT_STEPS 4
#define KCAL_DEFAULT_STEPS 5
#define WATER_MAX_STEPS 9
#define FOOD_MAX_STEPS 20
#define PANEL_TIMEOUT_MS 15000
#define ALERT_TIMEOUT_MS 30000  // A reminder waits longer before giving up on you
#define TAP_SLOP 8              // Meters are thin; forgive a near miss

typedef struct {
  GColor bg;
  GColor line;
  GColor text;
  GColor accent;
} PanelColors;

static NeedsStore s_store;
static AppTimer *s_panel_timer = NULL;
static bool s_panel_is_alert = false;
// Set when the worker opened the app just to deliver a reminder: once that's dealt with
// the app closes again, putting the wearer back on their watchface.
static bool s_exit_after_panel = false;

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

static void prv_publish(void) {
  g_bio.water = needs_core_pct(&s_store, &g_settings, NEED_WATER);
  g_bio.food = needs_core_pct(&s_store, &g_settings, NEED_FOOD);
}

static bool prv_tracked(Need need) {
  return need == NEED_WATER ? g_settings.track_water : g_settings.track_food;
}

static int32_t prv_food_step_dkj(void) {
  return g_settings.energy_kcal ? KCAL_STEP_DKJ : KJ_STEP_DKJ;
}

static void prv_close_panel(void) {
  if (s_panel_timer) app_timer_cancel(s_panel_timer);
  s_panel_timer = NULL;
  g_bio.panel = NEED_NONE;
  s_panel_is_alert = false;

  if (s_exit_after_panel) {
    window_stack_pop_all(true);
    return;
  }
  ui_redraw();
}

static void prv_panel_timeout(void *data) {
  s_panel_timer = NULL;
  prv_close_panel();
}

static void prv_keep_panel_open(void) {
  uint32_t timeout_ms = s_panel_is_alert ? ALERT_TIMEOUT_MS : PANEL_TIMEOUT_MS;
  if (s_panel_timer) {
    app_timer_reschedule(s_panel_timer, timeout_ms);
  } else {
    s_panel_timer = app_timer_register(timeout_ms, prv_panel_timeout, NULL);
  }
}

static void prv_open_panel(Need need, bool is_alert) {
  if (!prv_tracked(need)) return;
  g_bio.panel = need;
  s_panel_is_alert = is_alert;
  if (need == NEED_WATER) {
    g_bio.panel_count = 1;
  } else {
    g_bio.panel_count = g_settings.energy_kcal ? KCAL_DEFAULT_STEPS : KJ_DEFAULT_STEPS;
  }
  prv_keep_panel_open();
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
  needs_core_advance(&s_store, &g_settings, time(NULL));
  int32_t unit = g_bio.panel == NEED_WATER ? CUP_ML : prv_food_step_dkj();
  needs_core_log(&s_store, &g_settings, g_bio.panel, g_bio.panel_count * unit);
  needs_core_save(&s_store);
  prv_publish();

  if (g_settings.vibrate && !quiet_time_is_active()) vibes_short_pulse();
  prv_close_panel();
}

static void prv_alert(Need need) {
  light_enable_interaction();
  if (g_settings.vibrate && !quiet_time_is_active()) {
    if (need == NEED_WATER) {
      vibes_double_pulse();
    } else {
      vibes_enqueue_custom_pattern((VibePattern) {
        .durations = FOOD_VIBE_SEGMENTS,
        .num_segments = ARRAY_LENGTH(FOOD_VIBE_SEGMENTS)
      });
    }
  }
  // Don't yank away a panel someone is already halfway through using.
  if (g_bio.panel == NEED_NONE) prv_open_panel(need, true);
}

void needs_show_pending_alert(bool exit_when_done) {
  Need pending = needs_core_take_pending();
  if (pending == NEED_NONE || !prv_tracked(pending)) return;
  s_exit_after_panel = exit_when_done;
  prv_alert(pending);
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
    if (!prv_tracked(needs[i])) continue;

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
  if (g_bio.panel == NEED_NONE) {
    Need need = prv_need_at(point);
    if (need != NEED_NONE) prv_open_panel(need, false);
    return;
  }

  prv_keep_panel_open();
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

bool needs_button(ButtonId button) {
  if (g_bio.panel == NEED_NONE) {
    switch (button) {
      case BUTTON_ID_UP: prv_open_panel(NEED_WATER, false); return true;
      case BUTTON_ID_DOWN: prv_open_panel(NEED_FOOD, false); return true;
      case BUTTON_ID_SELECT: {
        bool food_first = !g_settings.track_water ||
                          (g_settings.track_food && g_bio.food < g_bio.water);
        prv_open_panel(food_first ? NEED_FOOD : NEED_WATER, false);
        return true;
      }
      default: return false;
    }
  }

  prv_keep_panel_open();
  switch (button) {
    case BUTTON_ID_UP: prv_step_panel(1); break;
    case BUTTON_ID_DOWN: prv_step_panel(-1); break;
    case BUTTON_ID_SELECT: prv_confirm_panel(); break;
    default: prv_close_panel(); break;
  }
  return true;
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
  if (g_bio.panel == NEED_NONE) return;

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

  // The requirement is an estimate from the wearer's body details, so it's shown as
  // one - and not at all while those are still the defaults.
  char amount[20];
  char detail[20];
  char today[48];
  char target[20] = "";
  if (water) {
    snprintf(amount, sizeof(amount), "%s%d CUP%s", count < 0 ? "-" : "", magnitude,
             magnitude == 1 ? "" : "S");
    prv_fmt_volume(detail, sizeof(detail), magnitude * CUP_ML, true);
    char taken[12];
    char need[12];
    prv_fmt_volume(taken, sizeof(taken), s_store.water_ml, false);
    prv_fmt_volume(need, sizeof(need), needs_core_water_need_ml(&g_settings), false);
    if (g_settings.profile_set) snprintf(target, sizeof(target), " OF ~%s", need);
    snprintf(today, sizeof(today), "TODAY %s%s", taken, target);
  } else {
    const char *unit = g_settings.energy_kcal ? "KCAL" : "KJ";
    snprintf(amount, sizeof(amount), "%s%d %s", count < 0 ? "-" : "",
             (int)prv_energy_display(magnitude * prv_food_step_dkj()), unit);
    detail[0] = '\0';
    if (g_settings.profile_set) {
      snprintf(target, sizeof(target), " OF ~%d",
               (int)prv_energy_display(needs_core_food_need_dkj(&g_settings)));
    }
    snprintf(today, sizeof(today), "TODAY %d%s %s", (int)prv_energy_display(s_store.food_dkj),
             target, unit);
  }

  const char *title;
  if (count < 0) {
    title = water ? "REMOVE WATER" : "REMOVE FOOD";
  } else if (s_panel_is_alert) {
    title = water ? "TIME TO DRINK" : "TIME TO EAT";
  } else {
    title = water ? "LOG WATER" : "LOG FOOD";
  }
  draw_text(ctx, title, medium, GRect(x, y, w, 22), GTextAlignmentCenter,
            s_panel_is_alert && count > 0 ? colors.accent : colors.line);
  draw_text(ctx, amount, large, GRect(x, y + 16, w, 32), GTextAlignmentCenter, colors.text);
  draw_text(ctx, detail, small, GRect(x, y + 48, w, 16), GTextAlignmentCenter, colors.text);
  draw_text(ctx, today, small, GRect(x, y + 66, w, 16), GTextAlignmentCenter, colors.line);

  prv_button(ctx, BUTTON_MINUS, "-", large, -2, colors, false);
  prv_button(ctx, BUTTON_PLUS, "+", large, -2, colors, false);
  prv_button(ctx, BUTTON_CANCEL, "CANCEL", medium, 5, colors, false);
  prv_button(ctx, BUTTON_CONFIRM, "CONFIRM", medium, 5, colors, true);
}

void needs_init(void) {
  // Saving straight away gives the worker something to read on a first run.
  needs_core_load(&s_store);
  needs_core_advance(&s_store, &g_settings, time(NULL));
  needs_core_save(&s_store);
  prv_publish();
}

void needs_deinit(void) {
  needs_core_save(&s_store);
}

void needs_tick(time_t now, bool own_reminders) {
  needs_core_advance(&s_store, &g_settings, now);
  prv_publish();
  if (!own_reminders) return;

  Need due = needs_core_check_reminders(&s_store, &g_settings, now);
  if (due != NEED_NONE) needs_show_pending_alert(false);
}
