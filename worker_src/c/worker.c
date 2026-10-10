#include "common.h"
#include "energy_core.h"
#include "hrv_core.h"
#include "needs_core.h"

// Keeps Biomon measuring while the app is closed: runs the HRV schedule, moves the energy
// meter, watches the water and food meters, and opens the app when one of them needs attention. A worker
// can't vibrate or draw, so anything the wearer should notice is handed to the app.

static void prv_notify(uint8_t type) {
  AppWorkerMessage message = { 0 };
  app_worker_send_message(type, &message);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  time_t now = time(NULL);
  Settings settings;
  settings_core_load(&settings);

  bool changed = hrv_core_tick(now, &settings);
  HrvStore hrv;
  hrv_core_load(&hrv);
  changed |= energy_core_tick(now, &settings, hrv_core_stress(&hrv, &settings, now));
  if (changed) prv_notify(WORKER_MSG_BODY);

  NeedsStore needs;
  needs_core_load(&needs);
  needs_core_advance(&needs, &settings, now);
  if (needs_core_check_reminders(&needs, &settings, now) != NEED_NONE) {
    // Launching does nothing if the app is already in front, hence the message as well.
    worker_launch_app();
    prv_notify(WORKER_MSG_ALERT);
  }
}

#if defined(BIOMON_HAS_HRV)
static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHRVUpdate && hrv_core_on_event()) prv_notify(WORKER_MSG_BODY);
}
#endif

int main(void) {
#if defined(BIOMON_HAS_HRV)
  health_service_events_subscribe(prv_health_handler, NULL);
#endif
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  worker_event_loop();
  hrv_core_stop();
}
