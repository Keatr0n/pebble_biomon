#include "biomon.h"

typedef enum {
  ITEM_THEME,
  ITEM_SEX,
  ITEM_AGE,
  ITEM_WEIGHT,
  ITEM_HEIGHT,
  ITEM_TRACK_WATER,
  ITEM_TRACK_FOOD,
  ITEM_NAG,
  ITEM_VIBRATE,
  ITEM_VOLUME_UNIT,
  ITEM_ENERGY_UNIT,
  ITEM_QUIET_START,
  ITEM_QUIET_END,
  ITEM_HRV_INTERVAL,
  ITEM_RESET,
  ITEM_DISCLAIMER
} Item;

typedef struct {
  const char *title;
  Item first;
  int count;
} Section;

static const Section SECTIONS[] = {
  {"LOOK", ITEM_THEME, 1},
  {"ABOUT YOU", ITEM_SEX, 4},
  {"WATER AND FOOD", ITEM_TRACK_WATER, 6},
  {"QUIET HOURS", ITEM_QUIET_START, 2},
  {"STRESS AND ENERGY", ITEM_HRV_INTERVAL, 2},
  {"ABOUT", ITEM_DISCLAIMER, 1}
};

typedef struct {
  const char *label;
  int min;
  int max;
  int step;
} NumberSpec;

typedef enum {
  RESET_IDLE,
  RESET_ARMED,
  RESET_DONE
} ResetState;

static const char *const THEME_NAMES[THEME_COUNT] = {"Subnautica", "Rust", "Cyberpunk 2077"};
static const char *const SEX_NAMES[] = {"Male", "Female", "Other"};
static const uint8_t HRV_INTERVALS[] = {5, 10, 15, 30, 60};

static Window *s_window;
static MenuLayer *s_menu;
static NumberWindow *s_number;
static Item s_editing;
static ResetState s_reset;

static const char *prv_title(Item item) {
  switch (item) {
    case ITEM_THEME: return "HUD style";
    case ITEM_SEX: return "Sex";
    case ITEM_AGE: return "Age";
    case ITEM_WEIGHT: return "Weight";
    case ITEM_HEIGHT: return "Height";
    case ITEM_TRACK_WATER: return "Track water";
    case ITEM_TRACK_FOOD: return "Track food";
    case ITEM_NAG: return "Repeat reminders";
    case ITEM_VIBRATE: return "Vibrate";
    case ITEM_VOLUME_UNIT: return "Show water in";
    case ITEM_ENERGY_UNIT: return "Show food in";
    case ITEM_QUIET_START: return "From";
    case ITEM_QUIET_END: return "Until";
    case ITEM_HRV_INTERVAL: return "Measure HRV";
    case ITEM_RESET: return "Reset baselines";
    case ITEM_DISCLAIMER: return "A toy";
  }
  return "";
}

static bool *prv_toggle(Item item) {
  switch (item) {
    case ITEM_TRACK_WATER: return &g_settings.track_water;
    case ITEM_TRACK_FOOD: return &g_settings.track_food;
    case ITEM_VIBRATE: return &g_settings.vibrate;
    default: return NULL;
  }
}

static bool prv_number_spec(Item item, NumberSpec *spec) {
  switch (item) {
    case ITEM_AGE: *spec = (NumberSpec) {"Age", 10, 110, 1}; return true;
    case ITEM_WEIGHT: *spec = (NumberSpec) {"Weight (kg)", 25, 300, 1}; return true;
    case ITEM_HEIGHT: *spec = (NumberSpec) {"Height (cm)", 100, 250, 1}; return true;
    case ITEM_NAG: *spec = (NumberSpec) {"Minutes (0 = once)", 0, 60, 5}; return true;
    case ITEM_QUIET_START: *spec = (NumberSpec) {"From (hour)", 0, 23, 1}; return true;
    case ITEM_QUIET_END: *spec = (NumberSpec) {"Until (hour)", 0, 23, 1}; return true;
    default: return false;
  }
}

static int prv_get_number(Item item) {
  switch (item) {
    case ITEM_AGE: return g_settings.age;
    case ITEM_WEIGHT: return g_settings.weight_kg;
    case ITEM_HEIGHT: return g_settings.height_cm;
    case ITEM_NAG: return g_settings.nag_min;
    case ITEM_QUIET_START: return g_settings.quiet_start;
    case ITEM_QUIET_END: return g_settings.quiet_end;
    default: return 0;
  }
}

static void prv_set_number(Item item, int value) {
  switch (item) {
    case ITEM_AGE: g_settings.age = (uint8_t)value; break;
    case ITEM_WEIGHT: g_settings.weight_kg = (uint16_t)value; break;
    case ITEM_HEIGHT: g_settings.height_cm = (uint16_t)value; break;
    case ITEM_NAG: g_settings.nag_min = (uint8_t)value; break;
    case ITEM_QUIET_START: g_settings.quiet_start = (uint8_t)value; break;
    case ITEM_QUIET_END: g_settings.quiet_end = (uint8_t)value; break;
    default: break;
  }
  if (item == ITEM_AGE || item == ITEM_WEIGHT || item == ITEM_HEIGHT) {
    g_settings.profile_set = true;
  }
}

static void prv_fmt_hour(char *buf, size_t size, int hour) {
  // The same hour at both ends is how quiet hours are switched off.
  const char *off = g_settings.quiet_start == g_settings.quiet_end ? " (off)" : "";
  snprintf(buf, size, "%02d:00%s", hour, off);
}

static void prv_fmt_value(Item item, char *buf, size_t size) {
  const char *text = NULL;
  switch (item) {
    case ITEM_THEME: text = THEME_NAMES[g_settings.theme % THEME_COUNT]; break;
    case ITEM_SEX: text = SEX_NAMES[g_settings.sex % ARRAY_LENGTH(SEX_NAMES)]; break;
    case ITEM_AGE: snprintf(buf, size, "%d", g_settings.age); return;
    case ITEM_WEIGHT: snprintf(buf, size, "%d kg", g_settings.weight_kg); return;
    case ITEM_HEIGHT: snprintf(buf, size, "%d cm", g_settings.height_cm); return;
    case ITEM_TRACK_WATER:
    case ITEM_TRACK_FOOD:
    case ITEM_VIBRATE: text = *prv_toggle(item) ? "On" : "Off"; break;
    case ITEM_NAG:
      if (g_settings.nag_min == 0) {
        text = "Remind once only";
        break;
      }
      snprintf(buf, size, "Every %d min", g_settings.nag_min);
      return;
    case ITEM_VOLUME_UNIT: text = g_settings.volume_floz ? "Fluid ounces" : "Millilitres"; break;
    case ITEM_ENERGY_UNIT: text = g_settings.energy_kcal ? "Calories" : "Kilojoules"; break;
    case ITEM_QUIET_START: prv_fmt_hour(buf, size, g_settings.quiet_start); return;
    case ITEM_QUIET_END: prv_fmt_hour(buf, size, g_settings.quiet_end); return;
    case ITEM_HRV_INTERVAL: snprintf(buf, size, "Every %d min", g_settings.hrv_interval_min); return;
    case ITEM_RESET:
      text = s_reset == RESET_ARMED ? "Press again to confirm"
           : s_reset == RESET_DONE ? "Done" : "Relearn stress and energy";
      break;
    case ITEM_DISCLAIMER: text = "Not a medical device"; break;
  }
  snprintf(buf, size, "%s", text ? text : "");
}

static void prv_edit_number(Item item, NumberSpec spec) {
  s_editing = item;
  number_window_set_label(s_number, spec.label);
  number_window_set_min(s_number, spec.min);
  number_window_set_max(s_number, spec.max);
  number_window_set_step_size(s_number, spec.step);
  number_window_set_value(s_number, prv_get_number(item));
  window_stack_push(number_window_get_window(s_number), true);
}

static void prv_number_selected(NumberWindow *number, void *context) {
  prv_set_number(s_editing, number_window_get_value(number));
  settings_save();
  window_stack_pop(true);
}

static void prv_next_hrv_interval(void) {
  for (size_t i = 0; i < ARRAY_LENGTH(HRV_INTERVALS); i++) {
    if (HRV_INTERVALS[i] > g_settings.hrv_interval_min) {
      g_settings.hrv_interval_min = HRV_INTERVALS[i];
      return;
    }
  }
  g_settings.hrv_interval_min = HRV_INTERVALS[0];
}

static Item prv_item_at(MenuIndex *index) {
  return (Item)(SECTIONS[index->section].first + index->row);
}

static void prv_select(MenuLayer *menu, MenuIndex *index, void *context) {
  Item item = prv_item_at(index);
  NumberSpec spec;
  if (prv_number_spec(item, &spec)) {
    prv_edit_number(item, spec);
    return;
  }

  bool *toggle = prv_toggle(item);
  if (toggle) *toggle = !*toggle;

  switch (item) {
    case ITEM_THEME: g_settings.theme = (g_settings.theme + 1) % THEME_COUNT; break;
    case ITEM_SEX:
      g_settings.sex = (g_settings.sex + 1) % ARRAY_LENGTH(SEX_NAMES);
      g_settings.profile_set = true;
      break;
    case ITEM_VOLUME_UNIT: g_settings.volume_floz = !g_settings.volume_floz; break;
    case ITEM_ENERGY_UNIT: g_settings.energy_kcal = !g_settings.energy_kcal; break;
    case ITEM_HRV_INTERVAL: prv_next_hrv_interval(); break;
    case ITEM_RESET:
      if (s_reset == RESET_ARMED) hrv_reset_baseline();
      s_reset = s_reset == RESET_ARMED ? RESET_DONE : RESET_ARMED;
      break;
    default: break;
  }

  settings_save();
  layer_mark_dirty(menu_layer_get_layer(menu));
}

// Moving off the reset row takes back the first press.
static void prv_selection_changed(MenuLayer *menu, MenuIndex new_index, MenuIndex old_index,
                                  void *context) {
  s_reset = RESET_IDLE;
}

static uint16_t prv_num_sections(MenuLayer *menu, void *context) {
  return ARRAY_LENGTH(SECTIONS);
}

static uint16_t prv_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  return SECTIONS[section].count;
}

static int16_t prv_header_height(MenuLayer *menu, uint16_t section, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *context) {
  menu_cell_basic_header_draw(ctx, cell, SECTIONS[section].title);
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  Item item = prv_item_at(index);
  char value[32];
  prv_fmt_value(item, value, sizeof(value));
  menu_cell_basic_draw(ctx, cell, prv_title(item), value, NULL);
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_reset = RESET_IDLE;

  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_sections = prv_num_sections,
    .get_num_rows = prv_num_rows,
    .get_header_height = prv_header_height,
    .draw_header = prv_draw_header,
    .draw_row = prv_draw_row,
    .select_click = prv_select,
    .selection_changed = prv_selection_changed
  });
  menu_layer_set_normal_colors(s_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu, GColorWhite, GColorBlack);
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));

  s_number = number_window_create("", (NumberWindowCallbacks) {
    .selected = prv_number_selected
  }, NULL);
}

static void prv_window_unload(Window *window) {
  number_window_destroy(s_number);
  menu_layer_destroy(s_menu);
  window_destroy(s_window);
  s_number = NULL;
  s_menu = NULL;
  s_window = NULL;

  // Tracking switches and body details may have changed, so recompute before repainting.
  hrv_refresh();
  needs_tick(time(NULL), false);
  ui_redraw();
}

void settings_menu_show(void) {
  if (s_window) return;
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload
  });
  window_stack_push(s_window, true);
}

void settings_menu_hide(void) {
  if (!s_window) return;
  window_stack_remove(number_window_get_window(s_number), false);
  window_stack_remove(s_window, false);
}
