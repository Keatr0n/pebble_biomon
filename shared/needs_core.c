#include "needs_core.h"

// Water and food are game-style meters that drain while you're awake and are topped up
// by logging a drink or a meal.
//
// The drain is deliberately uneven. The top half goes quickly, so a meter reaches the
// "top me up" line a couple of hours after a drink; below that it slows right down,
// because being a bit short is uncomfortable for a long time before it's dangerous.
// Filling is even: a cup or a meal is always worth the same slice of bar, sized from the
// wearer's daily requirement so that eating and drinking a normal day's worth keeps the
// meters cycling through their top half.
#define DEFAULT_WAKING_SEC (16 * SECONDS_PER_HOUR)
#define QUIET_LOOKBACK_DAYS 4
#define DAY_RESET_GAP_SEC (20 * SECONDS_PER_HOUR)

// ml of water per kg of body weight, and the constant term of the Mifflin-St Jeor
// resting energy equation, indexed by Sex. "Other" sits halfway between the two.
static const int WATER_ML_PER_KG[] = { 35, 31, 33 };
static const int MIFFLIN_CONSTANT[] = { 5, -161, -78 };
// The per-kg figure is all the water a body takes in. About a fifth of that arrives in
// food, and it stops scaling sensibly at the extremes of body weight.
#define WATER_DRUNK_PCT 80
#define WATER_NEED_MIN_ML 1500
#define WATER_NEED_MAX_ML 3500
#define SEDENTARY_FACTOR_X10 12  // Resting energy plus digestion and pottering about
#define ACTIVITY_FACTOR_X10 14   // Lightly active, for when the watch has no history to go on

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

void needs_core_load(NeedsStore *store) {
  if (persist_read_data(PERSIST_KEY_NEEDS, store, sizeof(*store)) == (int)sizeof(*store)) return;
  *store = (NeedsStore) {
    .updated_at = time(NULL), .water_level = LEVEL_FULL, .food_level = LEVEL_FULL
  };
}

void needs_core_save(const NeedsStore *store) {
  persist_write_data(PERSIST_KEY_NEEDS, store, sizeof(*store));
}

int32_t needs_core_water_need_ml(const Settings *settings) {
  int32_t ml = ((int32_t)settings->weight_kg * WATER_ML_PER_KG[settings->sex] * WATER_DRUNK_PCT) /
               100;
  if (ml < WATER_NEED_MIN_ML) return WATER_NEED_MIN_ML;
  return ml > WATER_NEED_MAX_ML ? WATER_NEED_MAX_ML : ml;
}

// kcal the wearer burns moving about on a typical day, by the watch's own count, or 0
// if it hasn't been keeping one.
static int32_t prv_usual_active_kcal(void) {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = start + SECONDS_PER_DAY;
  HealthServiceAccessibilityMask access = health_service_metric_averaged_accessible(
      HealthMetricActiveKCalories, start, end, HealthServiceTimeScopeDaily);
  if (!(access & HealthServiceAccessibilityMaskAvailable)) return 0;
  return (int32_t)health_service_sum_averaged(HealthMetricActiveKCalories, start, end,
                                              HealthServiceTimeScopeDaily);
#else
  return 0;
#endif
}

int32_t needs_core_food_need_dkj(const Settings *settings) {
  // Mifflin-St Jeor: 10w + 6.25h - 5a + c, kept as four times itself to stay in integers.
  int32_t bmr_x4 = 40 * settings->weight_kg + 25 * settings->height_cm - 20 * settings->age +
                   4 * MIFFLIN_CONSTANT[settings->sex];
  int32_t active = prv_usual_active_kcal();
  int32_t kcal;
  if (active > 0) {
    // Never more than doubles the resting figure, whatever the watch has counted.
    if (active > bmr_x4 / 4) active = bmr_x4 / 4;
    kcal = (bmr_x4 * SEDENTARY_FACTOR_X10) / 40 + active;
  } else {
    kcal = (bmr_x4 * ACTIVITY_FACTOR_X10) / 40;
  }
  return (kcal * 4184) / 100;
}

static bool prv_quiet_enabled(const Settings *settings) {
  return settings->quiet_start != settings->quiet_end;
}

bool needs_core_in_quiet_hours(const Settings *settings, time_t now) {
  if (!prv_quiet_enabled(settings)) return false;
  int start = settings->quiet_start;
  int end = settings->quiet_end;
  int hour = localtime(&now)->tm_hour;
  return start < end ? (hour >= start && hour < end) : (hour >= start || hour < end);
}

static int32_t prv_waking_seconds(const Settings *settings) {
  if (!prv_quiet_enabled(settings)) return DEFAULT_WAKING_SEC;
  int hours = (settings->quiet_start - settings->quiet_end + 24) % 24;
  return hours * SECONDS_PER_HOUR;
}

// Seconds between two times, not counting any that fell inside quiet hours. Nobody eats
// or drinks in their sleep, so the meters shouldn't be on the floor every morning.
static int32_t prv_awake_seconds(const Settings *settings, time_t from, time_t to) {
  if (to <= from) return 0;
  int32_t total = (int32_t)(to - from);
  if (!prv_quiet_enabled(settings) || total > QUIET_LOOKBACK_DAYS * SECONDS_PER_DAY) {
    return total;
  }

  int start = settings->quiet_start;
  int end = settings->quiet_end;
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

static int32_t prv_drain(int32_t level, int32_t seconds, const DrainStretch *curve,
                         int32_t waking_seconds) {
  int32_t eighth = waking_seconds / 8;
  int32_t top = LEVEL_FULL;

  // What's banked past full goes at the same rate as the top stretch.
  if (level > LEVEL_FULL) {
    int32_t span = LEVEL_FULL - curve[0].floor;
    int32_t duration = curve[0].eighths * eighth;
    int32_t to_full = (int32_t)(((int64_t)(level - LEVEL_FULL) * duration) / span);
    if (seconds < to_full) return level - (int32_t)(((int64_t)seconds * span) / duration);
    seconds -= to_full;
    level = LEVEL_FULL;
  }

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

// The running totals belong to a waking day, which starts when quiet hours end so that
// someone up past midnight doesn't lose theirs mid-evening.
static void prv_roll_day(NeedsStore *store, const Settings *settings, time_t now) {
  int wake_hour = prv_quiet_enabled(settings) ? settings->quiet_end : 0;
  time_t day_start = time_start_of_today() + wake_hour * SECONDS_PER_HOUR;
  if (day_start > now) day_start -= SECONDS_PER_DAY;

  // Changing the quiet hours shifts the boundary by a few hours; only a real new day,
  // or a clock that has jumped backwards, clears the totals.
  if (day_start - store->day_start < DAY_RESET_GAP_SEC && store->day_start <= now) return;
  store->day_start = day_start;
  store->water_ml = 0;
  store->food_dkj = 0;
}

void needs_core_advance(NeedsStore *store, const Settings *settings, time_t now) {
  // A clock change can leave the last update in the future; start again from now.
  if (store->updated_at > now) store->updated_at = now;

  int32_t awake = prv_awake_seconds(settings, store->updated_at, now);
  int32_t waking = prv_waking_seconds(settings);
  store->water_level = prv_drain(store->water_level, awake, WATER_DRAIN, waking);
  store->food_level = prv_drain(store->food_level, awake, FOOD_DRAIN, waking);
  store->updated_at = now;
  prv_roll_day(store, settings, now);
}

static int32_t prv_clamp(int32_t value, int32_t high) {
  if (value < 0) return 0;
  return value > high ? high : value;
}

void needs_core_log(NeedsStore *store, const Settings *settings, Need need, int32_t amount) {
  bool water = need == NEED_WATER;
  int32_t day_need = water ? needs_core_water_need_ml(settings) : needs_core_food_need_dkj(settings);
  int bar_fraction = water ? WATER_BAR_DAY_FRACTION : FOOD_BAR_DAY_FRACTION;
  int32_t fill = (int32_t)(((int64_t)amount * bar_fraction * LEVEL_FULL) / day_need);

  if (water) {
    store->water_ml = prv_clamp(store->water_ml + amount, INT32_MAX);
    store->water_level = prv_clamp(store->water_level + fill, WATER_LEVEL_MAX);
  } else {
    store->food_dkj = prv_clamp(store->food_dkj + amount, INT32_MAX);
    store->food_level = prv_clamp(store->food_level + fill, FOOD_LEVEL_MAX);
  }
}

int needs_core_pct(const NeedsStore *store, const Settings *settings, Need need) {
  bool water = need == NEED_WATER;
  if (!(water ? settings->track_water : settings->track_food)) return -1;
  int32_t level = water ? store->water_level : store->food_level;
  return level > LEVEL_FULL ? 100 : level / LEVEL_PER_PCT;
}

// Whether a need wants a reminder: its meter is low, and the day's requirement hasn't
// already been met - past that point a buzz would only be pushing more than is needed.
static bool prv_wanting(const NeedsStore *store, const Settings *settings, Need need) {
  int pct = needs_core_pct(store, settings, need);
  if (pct < 0 || pct >= NEED_PROMPT_PCT) return false;
  if (need == NEED_WATER) return store->water_ml < needs_core_water_need_ml(settings);
  return store->food_dkj < needs_core_food_need_dkj(settings);
}

static void prv_load_nag(NagStore *nag) {
  if (persist_read_data(PERSIST_KEY_NAG, nag, sizeof(*nag)) != (int)sizeof(*nag)) {
    memset(nag, 0, sizeof(*nag));
  }
}

// Returns true if this need should buzz now, updating its bookkeeping either way.
static bool prv_reminder_due(bool low, bool *was_low, time_t *nag_at, const Settings *settings,
                             time_t now, bool resting, bool *changed) {
  if (!low) {
    if (*was_low) *changed = true;
    *was_low = false;
    return false;
  }
  if (resting) return false;

  // Buzz on first dropping under the line, then on a timer for as long as it stays there.
  bool repeat = settings->nag_min > 0 &&
                (now - *nag_at) >= settings->nag_min * SECONDS_PER_MINUTE;
  if (*was_low && !repeat) return false;

  *was_low = true;
  *nag_at = now;
  *changed = true;
  return true;
}

Need needs_core_check_reminders(const NeedsStore *store, const Settings *settings, time_t now) {
  NagStore nag;
  prv_load_nag(&nag);

  bool asleep = health_service_peek_current_activities() &
                (HealthActivitySleep | HealthActivityRestfulSleep);
  bool resting = asleep || needs_core_in_quiet_hours(settings, now);
  bool changed = false;
  Need due = NEED_NONE;

  // One reminder at a time: if both are due, food waits for the next check.
  if (prv_reminder_due(prv_wanting(store, settings, NEED_WATER), &nag.water_low,
                       &nag.water_nag_at, settings, now, resting, &changed)) {
    due = NEED_WATER;
  } else if (prv_reminder_due(prv_wanting(store, settings, NEED_FOOD), &nag.food_low,
                              &nag.food_nag_at, settings, now, resting, &changed)) {
    due = NEED_FOOD;
  }

  if (due != NEED_NONE) nag.pending = due;
  if (changed) persist_write_data(PERSIST_KEY_NAG, &nag, sizeof(nag));
  return due;
}

Need needs_core_take_pending(void) {
  NagStore nag;
  prv_load_nag(&nag);
  Need pending = (Need)nag.pending;
  if (pending != NEED_NONE) {
    nag.pending = NEED_NONE;
    persist_write_data(PERSIST_KEY_NAG, &nag, sizeof(nag));
  }
  return pending;
}
