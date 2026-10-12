#include "biomon.h"
#include "needs_core.h"

// The app's side of the water and food meters: showing them, opening the log menu from a
// tap on one, applying what gets picked there, and buzzing for reminders. The meter maths
// is in shared/needs_core.c and the menu is log_menu.c.
#define TAP_SLOP 8  // Meters are thin; forgive a near miss

// The last thing logged, kept so that it can be taken back.
typedef struct {
  LogEntry entry;
  time_t at;
} LastLog;

static NeedsStore s_store;
static LastLog s_last;

static const uint32_t FOOD_VIBE_SEGMENTS[] = { 120, 100, 120, 100, 300 };

static void prv_publish(void) {
  g_bio.water = needs_core_pct(&s_store, &g_settings, NEED_WATER);
  g_bio.food = needs_core_pct(&s_store, &g_settings, NEED_FOOD);
}

static bool prv_tracked(Need need) {
  return need == NEED_WATER ? g_settings.track_water : g_settings.track_food;
}

static void prv_apply(LogEntry entry) {
  // Drain up to this moment first, so the top-up lands on the level as it is now.
  needs_core_advance(&s_store, &g_settings, time(NULL));
  needs_core_log(&s_store, &g_settings, NEED_WATER, entry.water_ml);
  needs_core_log(&s_store, &g_settings, NEED_FOOD, entry.food_dkj);
  needs_core_save(&s_store);
  prv_publish();
  ui_redraw();

  if (g_settings.vibrate && !quiet_time_is_active()) vibes_short_pulse();
}

void needs_log(LogEntry entry) {
  prv_apply(entry);
  s_last = (LastLog) { .entry = entry, .at = time(NULL) };
  persist_write_data(PERSIST_KEY_LAST_LOG, &s_last, sizeof(s_last));
}

bool needs_last_entry(LogEntry *entry) {
  // Yesterday's entry would come off today's totals, so by then it's too late.
  if (s_last.at == 0 || s_last.at < s_store.day_start) return false;
  *entry = s_last.entry;
  return true;
}

void needs_undo(void) {
  LogEntry entry;
  if (!needs_last_entry(&entry)) return;
  prv_apply((LogEntry) { .water_ml = -entry.water_ml, .food_dkj = -entry.food_dkj });
  s_last = (LastLog) { .at = 0 };
  persist_delete(PERSIST_KEY_LAST_LOG);
}

int32_t needs_today(Need need) {
  return need == NEED_WATER ? s_store.water_ml : s_store.food_dkj;
}

// A reminder only buzzes and lights the screen. What it's about is already on the HUD:
// the low meter and the status line.
void needs_show_pending_alert(void) {
  Need pending = needs_core_take_pending();
  if (pending == NEED_NONE || !prv_tracked(pending)) return;

  light_enable_interaction();
  if (!g_settings.vibrate || quiet_time_is_active()) return;
  if (pending == NEED_WATER) {
    vibes_double_pulse();
  } else {
    vibes_enqueue_custom_pattern((VibePattern) {
      .durations = FOOD_VIBE_SEGMENTS,
      .num_segments = ARRAY_LENGTH(FOOD_VIBE_SEGMENTS)
    });
  }
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

void needs_tap(GPoint point) {
  Need need = prv_need_at(point);
  if (need != NEED_NONE) log_menu_show(need);
}

void needs_init(void) {
  // Saving straight away gives the worker something to read on a first run.
  needs_core_load(&s_store);
  needs_core_advance(&s_store, &g_settings, time(NULL));
  needs_core_save(&s_store);
  prv_publish();

  if (persist_read_data(PERSIST_KEY_LAST_LOG, &s_last, sizeof(s_last)) != (int)sizeof(s_last)) {
    s_last = (LastLog) { .at = 0 };
  }
}

void needs_deinit(void) {
  needs_core_save(&s_store);
}

void needs_tick(time_t now, bool own_reminders) {
  needs_core_advance(&s_store, &g_settings, now);
  prv_publish();
  if (!own_reminders) return;

  Need due = needs_core_check_reminders(&s_store, &g_settings, now);
  if (due != NEED_NONE) needs_show_pending_alert();
}
