#pragma once

#include "common.h"

#define LEVEL_FULL 1000000     // Meter level in ten-thousandths of a percent
#define LEVEL_PER_PCT (LEVEL_FULL / 100)
// A big drink or meal carries on past full, out of sight, so it lasts longer than a
// small one. Food banks a whole day's worth; water much less, since the body doesn't
// store it.
#define WATER_LEVEL_MAX (LEVEL_FULL + LEVEL_FULL / 2)
#define FOOD_LEVEL_MAX (2 * LEVEL_FULL)

// Meter levels and the day's running totals. Only the app writes this.
typedef struct {
  time_t updated_at;     // When the levels were last drained up to
  int32_t water_level;   // Can run past LEVEL_FULL; see above
  int32_t food_level;
  time_t day_start;      // The waking day the totals below belong to
  int32_t water_ml;
  int32_t food_dkj;      // Tenths of a kJ, so that 100 kcal is a whole number
} NeedsStore;

// Reminder bookkeeping, written by whichever process is watching the meters.
typedef struct {
  time_t water_nag_at;
  time_t food_nag_at;
  bool water_low;
  bool food_low;
  uint8_t pending;       // A Need whose reminder the app hasn't shown yet
} NagStore;

// Reads the stored meters, or full ones if nothing has been saved yet.
void needs_core_load(NeedsStore *store);
void needs_core_save(const NeedsStore *store);

// Drains both meters up to now and rolls the daily totals over if a new day has begun.
void needs_core_advance(NeedsStore *store, const Settings *settings, time_t now);

// The day's estimated requirement: water to drink, and food energy.
int32_t needs_core_water_need_ml(const Settings *settings);
int32_t needs_core_food_need_dkj(const Settings *settings);

// Adds (or with a negative amount, takes back) ml of water or tenths of a kJ of food.
void needs_core_log(NeedsStore *store, const Settings *settings, Need need, int32_t amount);

bool needs_core_in_quiet_hours(const Settings *settings, time_t now);

// Percentage for a need, or -1 if it isn't being tracked.
int needs_core_pct(const NeedsStore *store, const Settings *settings, Need need);

// Decides whether a reminder is due for meters that have already been advanced to now.
// Returns the need to remind about, having recorded it as pending, or NEED_NONE.
Need needs_core_check_reminders(const NeedsStore *store, const Settings *settings, time_t now);

// Returns the pending reminder, if any, and clears it.
Need needs_core_take_pending(void);
