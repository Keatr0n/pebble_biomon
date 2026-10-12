#include "biomon.h"
#include "needs_core.h"

// The menu for logging food and drink. It's a small tree, with a window for each level:
//
//   FOOD  > how big a meal
//   DRINK > which drink > how much of it
//
// Every drink counts in full towards water. One with energy in it counts towards food as
// well, so a glass of juice moves both meters.
#define DEFAULT_PORTION 3  // MEAL
#define DEFAULT_SERVING 1  // CUP
#define ROW_H 50
#define ROW_H_TITLE_ONLY 38

typedef enum {
  PAGE_ROOT,
  PAGE_FOOD,
  PAGE_DRINK,
  PAGE_SERVING
} Page;

typedef enum {
  ROOT_FOOD,
  ROOT_DRINK,
  ROOT_UNDO,
  ROOT_ROWS
} RootRow;

typedef struct {
  const char *name;
  const char *examples;
  // Each unit gets a round figure of its own, not an awkward conversion of the other's.
  int16_t kj;
  int16_t kcal;
} Portion;

typedef struct {
  const char *name;
  int16_t kj_per_100ml;
} Drink;

typedef struct {
  const char *name;
  int16_t ml;
} Serving;

typedef struct Level {
  struct Level *parent;  // The level this one was opened from, unless it's the first
  Window *window;
  MenuLayer *menu;
  Page page;
  const Drink *drink;    // On the serving page, what's being poured
} Level;

typedef struct {
  GColor bg;
  GColor text;
  GColor highlight;
} MenuColors;

static const Portion PORTIONS[] = {
  {"NIBBLE", "BISCUIT, FRUIT", 400, 100},
  {"SNACK", "MUESLI BAR, TOAST", 800, 200},
  {"LIGHT MEAL", "SANDWICH, SOUP", 1500, 350},
  {"MEAL", "DINNER PLATE", 2500, 600},
  {"BIG MEAL", "BURGER, CHIPS", 3500, 850},
  {"FEAST", "THREE COURSES", 5000, 1200}
};

static const Drink DRINKS[] = {
  {"WATER", 0},
  {"COFFEE", 0},
  {"TEA", 0},
  {"MILKY COFFEE", 200},
  {"JUICE", 190},
  {"SOFT DRINK", 180},
  {"MILK", 270}
};

static const Serving SERVINGS[] = {
  {"SMALL CUP", 150},
  {"CUP", 250},
  {"MUG OR CAN", 375},
  {"BOTTLE", 600},
  {"LARGE BOTTLE", 1000}
};

static const MenuColors COLORS[THEME_COUNT] = {
  [THEME_SUBNAUTICA] = { { GColorOxfordBlueARGB8 }, { GColorCelesteARGB8 },
                         { GColorChromeYellowARGB8 } },
  [THEME_RUST] = { { GColorBlackARGB8 }, { GColorWhiteARGB8 }, { GColorKellyGreenARGB8 } },
  [THEME_CYBERPUNK] = { { GColorBlackARGB8 }, { GColorYellowARGB8 }, { GColorFollyARGB8 } }
};

static void prv_push(Page page, Level *parent, const Drink *drink);

// Whole ml or fl oz, for the day's totals.
static int prv_volume_display(int32_t ml) {
  return g_settings.volume_floz ? (ml * 1000 + 14787) / 29574 : ml;
}

static const char *prv_volume_unit(void) {
  return g_settings.volume_floz ? "FL OZ" : "ML";
}

static int prv_energy_display(int32_t dkj) {
  return g_settings.energy_kcal ? (dkj * 100 + 2092) / 4184 : dkj / 10;
}

static const char *prv_energy_unit(void) {
  return g_settings.energy_kcal ? "KCAL" : "KJ";
}

// The day's total against the requirement. That's an estimate from the wearer's body
// details, so it's shown as one - and not at all while those are still the defaults.
static void prv_fmt_today(Need need, char *buf, size_t size) {
  bool water = need == NEED_WATER;
  int taken = water ? prv_volume_display(needs_today(NEED_WATER))
                    : prv_energy_display(needs_today(NEED_FOOD));
  int wanted = water ? prv_volume_display(needs_core_water_need_ml(&g_settings))
                     : prv_energy_display(needs_core_food_need_dkj(&g_settings));
  const char *unit = water ? prv_volume_unit() : prv_energy_unit();

  if (g_settings.profile_set) {
    snprintf(buf, size, "TODAY %d OF ~%d %s", taken, wanted, unit);
  } else {
    snprintf(buf, size, "TODAY %d %s", taken, unit);
  }
}

// "250 ML", "2500 KJ" or, for a drink with energy in it, "250 ML + 475 KJ".
static void prv_fmt_entry(LogEntry entry, char *buf, size_t size) {
  char volume[16] = "";
  char energy[16] = "";
  if (entry.water_ml && g_settings.volume_floz) {
    // A serving in fl oz is rarely a whole number, so it gets a decimal place.
    int tenths = (entry.water_ml * 10000 + 14787) / 29574;
    snprintf(volume, sizeof(volume), "%d.%d FL OZ", tenths / 10, tenths % 10);
  } else if (entry.water_ml) {
    snprintf(volume, sizeof(volume), "%d ML", (int)entry.water_ml);
  }
  if (entry.food_dkj) {
    snprintf(energy, sizeof(energy), "%d %s", prv_energy_display(entry.food_dkj),
             prv_energy_unit());
  }
  snprintf(buf, size, "%s%s%s", volume, volume[0] && energy[0] ? " + " : "", energy);
}

static LogEntry prv_portion_entry(const Portion *portion) {
  return (LogEntry) {
    .food_dkj = g_settings.energy_kcal ? (portion->kcal * 4184) / 100 : portion->kj * 10
  };
}

static LogEntry prv_serving_entry(const Drink *drink, const Serving *serving) {
  return (LogEntry) {
    .water_ml = serving->ml,
    .food_dkj = g_settings.track_food ? (drink->kj_per_100ml * serving->ml) / 10 : 0
  };
}

// The top level only offers what's being tracked, and an undo only when there's
// something to take back.
static int prv_root_rows(RootRow *rows) {
  int count = 0;
  LogEntry last;
  if (g_settings.track_food) rows[count++] = ROOT_FOOD;
  if (g_settings.track_water) rows[count++] = ROOT_DRINK;
  if (needs_last_entry(&last)) rows[count++] = ROOT_UNDO;
  return count;
}

static RootRow prv_root_row(int index) {
  RootRow rows[ROOT_ROWS];
  prv_root_rows(rows);
  return rows[index];
}

// Closes the whole menu, back to the HUD.
static void prv_close(Level *level) {
  // The levels underneath go first and quietly, so this one slides off to show the HUD.
  Level *under = level->parent;
  while (under) {
    Level *next = under->parent;
    window_stack_remove(under->window, false);
    under = next;
  }
  window_stack_remove(level->window, true);
}

static uint16_t prv_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  RootRow rows[ROOT_ROWS];
  switch (((Level *)context)->page) {
    case PAGE_ROOT: return prv_root_rows(rows);
    case PAGE_FOOD: return ARRAY_LENGTH(PORTIONS);
    case PAGE_DRINK: return ARRAY_LENGTH(DRINKS);
    case PAGE_SERVING: return ARRAY_LENGTH(SERVINGS);
  }
  return 0;
}

static int16_t prv_header_height(MenuLayer *menu, uint16_t section, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

#if defined(PBL_RECT)
// The system's own rows are tall enough that barely three fit on the screen.
static int16_t prv_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return ((Level *)context)->page == PAGE_DRINK ? ROW_H_TITLE_ONLY : ROW_H;
}
#endif

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *context) {
  Level *level = context;
  char today[48];
  const char *text = "LOG";
  if (level->page == PAGE_SERVING) {
    text = level->drink->name;
  } else if (level->page != PAGE_ROOT) {
    prv_fmt_today(level->page == PAGE_FOOD ? NEED_FOOD : NEED_WATER, today, sizeof(today));
    text = today;
  }

  // The system's header hugs the left edge, which a round screen cuts off.
  GRect box = layer_get_bounds(cell);
  box.origin.y -= 1;
  box = grect_inset(box, GEdgeInsets(0, 4));
  draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), box,
            PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft),
            COLORS[g_settings.theme].text);
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  Level *level = context;
  const char *title = "";
  char subtitle[48] = "";

  switch (level->page) {
    case PAGE_ROOT:
      switch (prv_root_row(index->row)) {
        case ROOT_FOOD:
          title = "FOOD";
          prv_fmt_today(NEED_FOOD, subtitle, sizeof(subtitle));
          break;
        case ROOT_DRINK:
          title = "DRINK";
          prv_fmt_today(NEED_WATER, subtitle, sizeof(subtitle));
          break;
        default: {
          LogEntry last = { 0 };
          needs_last_entry(&last);
          title = "UNDO LAST";
          prv_fmt_entry(last, subtitle, sizeof(subtitle));
          break;
        }
      }
      break;
    case PAGE_FOOD: {
      const Portion *portion = &PORTIONS[index->row];
      title = portion->name;
      snprintf(subtitle, sizeof(subtitle), "%d %s - %s",
               prv_energy_display(prv_portion_entry(portion).food_dkj), prv_energy_unit(),
               portion->examples);
      break;
    }
    case PAGE_DRINK:
      title = DRINKS[index->row].name;
      break;
    case PAGE_SERVING: {
      const Serving *serving = &SERVINGS[index->row];
      title = serving->name;
      prv_fmt_entry(prv_serving_entry(level->drink, serving), subtitle, sizeof(subtitle));
      break;
    }
  }
  menu_cell_basic_draw(ctx, cell, title, subtitle[0] ? subtitle : NULL, NULL);
}

static void prv_select(MenuLayer *menu, MenuIndex *index, void *context) {
  Level *level = context;
  switch (level->page) {
    case PAGE_ROOT:
      switch (prv_root_row(index->row)) {
        case ROOT_FOOD: prv_push(PAGE_FOOD, level, NULL); break;
        case ROOT_DRINK: prv_push(PAGE_DRINK, level, NULL); break;
        default:
          needs_undo();
          prv_close(level);
          break;
      }
      break;
    case PAGE_FOOD:
      needs_log(prv_portion_entry(&PORTIONS[index->row]));
      prv_close(level);
      break;
    case PAGE_DRINK:
      prv_push(PAGE_SERVING, level, &DRINKS[index->row]);
      break;
    case PAGE_SERVING:
      needs_log(prv_serving_entry(level->drink, &SERVINGS[index->row]));
      prv_close(level);
      break;
  }
}

static void prv_window_load(Window *window) {
  Level *level = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  MenuColors colors = COLORS[g_settings.theme];

  level->menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(level->menu, level, (MenuLayerCallbacks) {
    .get_num_rows = prv_num_rows,
    .get_header_height = prv_header_height,
    .get_cell_height = PBL_IF_RECT_ELSE(prv_cell_height, NULL),
    .draw_header = prv_draw_header,
    .draw_row = prv_draw_row,
    .select_click = prv_select
  });
  menu_layer_set_normal_colors(level->menu, colors.bg, colors.text);
  menu_layer_set_highlight_colors(level->menu, colors.highlight, colors.bg);
  menu_layer_set_click_config_onto_window(level->menu, window);
  layer_add_child(root, menu_layer_get_layer(level->menu));

  // Start on the everyday amount, with the smaller and larger ones either side.
  int row = level->page == PAGE_FOOD ? DEFAULT_PORTION
          : level->page == PAGE_SERVING ? DEFAULT_SERVING : 0;
  menu_layer_set_selected_index(level->menu, (MenuIndex) { 0, row }, MenuRowAlignCenter, false);
}

static void prv_window_unload(Window *window) {
  Level *level = window_get_user_data(window);
  menu_layer_destroy(level->menu);
  window_destroy(window);
  free(level);
}

static void prv_push(Page page, Level *parent, const Drink *drink) {
  Level *level = malloc(sizeof(Level));
  if (!level) return;
  *level = (Level) { .parent = parent, .window = window_create(), .page = page, .drink = drink };

  window_set_user_data(level->window, level);
  window_set_background_color(level->window, COLORS[g_settings.theme].bg);
  window_set_window_handlers(level->window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload
  });
  window_stack_push(level->window, true);
}

void log_menu_show(Need need) {
  if (need == NEED_WATER) {
    if (g_settings.track_water) prv_push(PAGE_DRINK, NULL, NULL);
  } else if (need == NEED_FOOD) {
    if (g_settings.track_food) prv_push(PAGE_FOOD, NULL, NULL);
  } else if (g_settings.track_water || g_settings.track_food) {
    prv_push(PAGE_ROOT, NULL, NULL);
  }
}
