#include "biomon.h"
#include "energy_core.h"
#include "hrv_core.h"

Bio g_bio;
GFont g_font_chakra_time;
GFont g_font_chakra_label;

#define TAP_MAX_TRAVEL 16  // Further than this between finger down and up is a swipe

static Window *s_window;
static Layer *s_canvas;

#if defined(BIOMON_DEMO)
// Canned readings for emulator screenshots: 1 is a good day, 2 has everything going
// wrong, 3 and 4 are a good day with the water or food log panel open.
static void prv_apply_demo(void) {
  bool bad = BIOMON_DEMO == 2;
  if (BIOMON_DEMO == 3) g_bio.panel = NEED_WATER;
  if (BIOMON_DEMO == 4) g_bio.panel = NEED_FOOD;
  if (BIOMON_DEMO >= 3 && g_bio.panel_count == 0) g_bio.panel_count = 2;
  g_bio.hr = bad ? 104 : 64;
  g_bio.hrv = bad ? 19 : 52;
  g_bio.stress = bad ? 84 : 18;
  g_bio.stress_high = bad;
  g_bio.worn_down = false;
  g_bio.energy = bad ? 14 : 78;
  g_bio.calibrating = false;
  g_bio.water = bad ? 8 : 72;
  g_bio.food = bad ? 31 : 61;
  g_bio.steps = bad ? 912 : 6240;
  g_bio.sleep_min = bad ? 310 : 452;
  g_bio.hr_hist_len = HR_HIST_LEN;
  for (int i = 0; i < HR_HIST_LEN; i++) {
    g_bio.hr_hist[i] = (uint8_t)(g_bio.hr - 8 + (i * 7) % 13 + (i % 5 == 0 ? 9 : 0));
  }
}
#endif

void ui_redraw(void) {
#if defined(BIOMON_DEMO)
  prv_apply_demo();
#endif
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  switch (g_settings.theme) {
    case THEME_RUST: theme_rust_draw(ctx, bounds); break;
    case THEME_CYBERPUNK: theme_cyberpunk_draw(ctx, bounds); break;
    case THEME_SUBNAUTICA:
    default: theme_subnautica_draw(ctx, bounds); break;
  }
  needs_panel_draw(ctx, bounds);
}

static void prv_update_clock(struct tm *tick_time) {
  strftime(g_bio.time, sizeof(g_bio.time), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  if (!clock_is_24h_style() && g_bio.time[0] == '0') {
    memmove(g_bio.time, g_bio.time + 1, sizeof(g_bio.time) - 1);
  }

  strftime(g_bio.date, sizeof(g_bio.date), "%a %d %b", tick_time);
  for (char *c = g_bio.date; *c; c++) {
    if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
  }
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  time_t now = time(NULL);
  prv_update_clock(tick_time);
  vitals_tick();

  // The worker normally does the measuring and the reminding. If it isn't running -
  // another app's worker has the slot, say - the app covers for it while it's open.
  bool worker_running = app_worker_is_running();
  if (!worker_running) {
    hrv_core_tick(now, &g_settings);
    HrvStore hrv;
    hrv_core_load(&hrv);
    energy_core_tick(now, &g_settings, hrv_core_stress(&hrv, &g_settings, now));
  }
  hrv_refresh();
  needs_tick(now, !worker_running);
  ui_redraw();
}

static void prv_worker_message_handler(uint16_t type, AppWorkerMessage *data) {
  if (type == WORKER_MSG_BODY) {
    hrv_refresh();
    ui_redraw();
  } else if (type == WORKER_MSG_ALERT) {
    needs_show_pending_alert(false);
  }
}

static void prv_battery_handler(BatteryChargeState state) {
  g_bio.battery = state.charge_percent;
  g_bio.charging = state.is_charging;
  ui_redraw();
}

static void prv_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (!needs_button(click_recognizer_get_button_id(recognizer))) window_stack_pop(true);
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_click_handler);
}

#if defined(BIOMON_HAS_TOUCH)
static void prv_touch_handler(const TouchEvent *event, void *context) {
  static GPoint s_down;

  if (event->type == TouchEvent_Touchdown) {
    s_down = GPoint(event->x, event->y);
  } else if (event->type == TouchEvent_Liftoff) {
    int dx = event->x - s_down.x;
    int dy = event->y - s_down.y;
    if (dx * dx + dy * dy <= TAP_MAX_TRAVEL * TAP_MAX_TRAVEL) needs_tap(s_down);
  }
}
#endif

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  if (!settings_handle_message(iter)) return;
  // Tracking switches and body details may have changed, so recompute before repainting.
  hrv_refresh();
  needs_tick(time(NULL), false);
  ui_redraw();
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  g_font_chakra_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CHAKRA_TIME_52));
  g_font_chakra_label =
      fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CHAKRA_LABEL_16));

  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, prv_canvas_update);
  layer_add_child(root, s_canvas);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
  fonts_unload_custom_font(g_font_chakra_time);
  fonts_unload_custom_font(g_font_chakra_label);
}

static void prv_init(void) {
  settings_load();

  time_t now = time(NULL);
  prv_update_clock(localtime(&now));
  prv_battery_handler(battery_state_service_peek());
  vitals_init();
  hrv_refresh();
  needs_init();
  // Asking again when it's already running is harmless.
  app_worker_launch();
  app_worker_message_subscribe(prv_worker_message_handler);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  battery_state_service_subscribe(prv_battery_handler);
#if defined(BIOMON_HAS_TOUCH)
  touch_service_subscribe(prv_touch_handler, NULL);
#endif

  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(256, 64);

  // If the worker opened the app to deliver a reminder, show it and leave again after.
  needs_show_pending_alert(launch_reason() == APP_LAUNCH_WORKER);
  ui_redraw();
}

static void prv_deinit(void) {
  needs_deinit();
  hrv_core_stop();
  app_worker_message_unsubscribe();
  vitals_deinit();
#if defined(BIOMON_HAS_TOUCH)
  touch_service_unsubscribe();
#endif
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
