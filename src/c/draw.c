#include "biomon.h"

#define SPARK_MIN_RANGE_BPM 12  // Keeps a steady pulse from being stretched into mountains

Icon ICON_HEART = {
  "..XX...XX..",
  ".XXXX.XXXX.",
  "XXXXXXXXXXX",
  "XXXXXXXXXXX",
  "XXXXXXXXXXX",
  ".XXXXXXXXX.",
  "..XXXXXXX..",
  "...XXXXX...",
  "....XXX....",
  ".....X.....",
  "..........."
};

Icon ICON_DROP = {
  ".....X.....",
  ".....X.....",
  "....XXX....",
  "....XXX....",
  "...XXXXX...",
  "..XXXXXXX..",
  "..XXXXXXX..",
  ".XXXXXXXXX.",
  ".XXXXXXXXX.",
  "..XXXXXXX..",
  "...XXXXX..."
};

Icon ICON_FOOD = {
  ".X.X.X...XX",
  ".X.X.X..XXX",
  ".X.X.X..XXX",
  ".X.X.X..XXX",
  ".XXXXX..XXX",
  "..XXX...XXX",
  "...X.....XX",
  "...X.....XX",
  "...X.....XX",
  "...X.....XX",
  "...X.....XX"
};

Icon ICON_APPLE = {
  "......X....",
  ".....X.....",
  "..XXX.XXX..",
  ".XXXXXXXXX.",
  "XXXXXXXXXXX",
  "XXXXXXXXXXX",
  "XXXXXXXXXXX",
  "XXXXXXXXXXX",
  ".XXXXXXXXX.",
  ".XXXXXXXXX.",
  "..XXX.XXX.."
};

Icon ICON_CROSS = {
  "....XXX....",
  "....XXX....",
  "....XXX....",
  "....XXX....",
  "XXXXXXXXXXX",
  "XXXXXXXXXXX",
  "XXXXXXXXXXX",
  "....XXX....",
  "....XXX....",
  "....XXX....",
  "....XXX...."
};

Status bio_status(void) {
  bool water_low = g_bio.water >= 0 && g_bio.water < NEED_PROMPT_PCT;
  bool food_low = g_bio.food >= 0 && g_bio.food < NEED_PROMPT_PCT;
  if (water_low && food_low) return g_bio.food < g_bio.water ? STATUS_FOOD : STATUS_WATER;
  if (water_low) return STATUS_WATER;
  if (food_low) return STATUS_FOOD;

  if (g_bio.stress >= STRESS_HIGH) return STATUS_STRESS;
  if (g_bio.calibrating) return STATUS_CALIBRATING;
  return STATUS_OK;
}

// Stress turned upside down, so it can be drawn as the health bar every game HUD has.
int bio_vitality(void) {
  return g_bio.stress < 0 ? -1 : 100 - g_bio.stress;
}

void draw_text(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align,
               GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

void draw_bar(GContext *ctx, GRect rect, int pct, GColor fill, GColor track) {
  graphics_context_set_fill_color(ctx, track);
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
  if (pct <= 0) return;
  if (pct > 100) pct = 100;

  rect.size.w = (rect.size.w * pct) / 100;
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
}

void draw_segments(GContext *ctx, GRect rect, int pct, int seg_w, int gap, GColor fill,
                   GColor track) {
  int count = (rect.size.w + gap) / (seg_w + gap);
  int lit = pct <= 0 ? 0 : (pct * count + 99) / 100;
  if (lit > count) lit = count;

  for (int i = 0; i < count; i++) {
    graphics_context_set_fill_color(ctx, i < lit ? fill : track);
    graphics_fill_rect(ctx, GRect(rect.origin.x + i * (seg_w + gap), rect.origin.y, seg_w,
                                  rect.size.h), 0, GCornerNone);
  }
}

void draw_ring(GContext *ctx, GPoint center, int radius, int thickness, int pct, GColor fill,
               GColor track) {
  GRect rect = GRect(center.x - radius, center.y - radius, radius * 2, radius * 2);
  graphics_context_set_fill_color(ctx, track);
  graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, thickness, 0, TRIG_MAX_ANGLE);
  if (pct <= 0) return;
  if (pct > 100) pct = 100;

  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, thickness, 0,
                       (TRIG_MAX_ANGLE * pct) / 100);
}

void draw_icon(GContext *ctx, Icon icon, GPoint origin, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  for (int row = 0; row < ICON_SIZE; row++) {
    for (int col = 0; col < ICON_SIZE; col++) {
      if (icon[row][col] != 'X') continue;
      int run_start = col;
      while (col + 1 < ICON_SIZE && icon[row][col + 1] == 'X') col++;
      graphics_draw_line(ctx, GPoint(origin.x + run_start, origin.y + row),
                         GPoint(origin.x + col, origin.y + row));
    }
  }
}

// A rectangle with its bottom-right corner sliced off.
void draw_cut_rect(GContext *ctx, GRect rect, int cut, GColor color, bool filled) {
  int16_t left = rect.origin.x;
  int16_t top = rect.origin.y;
  int16_t right = left + rect.size.w - 1;
  int16_t bottom = top + rect.size.h - 1;
  GPoint points[] = {
    { left, top }, { right, top }, { right, bottom - cut }, { right - cut, bottom },
    { left, bottom }
  };
  GPathInfo info = { .num_points = ARRAY_LENGTH(points), .points = points };
  GPath *path = gpath_create(&info);
  if (!path) return;

  if (filled) {
    graphics_context_set_fill_color(ctx, color);
    gpath_draw_filled(ctx, path);
  } else {
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, path);
  }
  gpath_destroy(path);
}

// The newest value sits at the right edge, so a short history grows in from the right.
void draw_sparkline(GContext *ctx, GRect rect, const uint8_t *values, int count, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);

  int mid_y = rect.origin.y + rect.size.h / 2;
  int right = rect.origin.x + rect.size.w - 1;
  if (count < 2) {
    graphics_draw_line(ctx, GPoint(rect.origin.x, mid_y), GPoint(right, mid_y));
    return;
  }

  int low = 255;
  int high = 0;
  for (int i = 0; i < count; i++) {
    if (values[i] < low) low = values[i];
    if (values[i] > high) high = values[i];
  }
  if (high - low < SPARK_MIN_RANGE_BPM) {
    int pad = (SPARK_MIN_RANGE_BPM - (high - low)) / 2;
    low -= pad;
    high = low + SPARK_MIN_RANGE_BPM;
  }

  GPoint previous = GPointZero;
  for (int i = 0; i < count; i++) {
    int x = right - ((count - 1 - i) * (rect.size.w - 1)) / (HR_HIST_LEN - 1);
    int y = rect.origin.y + rect.size.h - 1 -
            ((values[i] - low) * (rect.size.h - 1)) / (high - low);
    GPoint point = GPoint(x, y);
    if (i > 0) graphics_draw_line(ctx, previous, point);
    previous = point;
  }
}

void fmt_value(char *buf, size_t size, int value) {
  if (value < 0) {
    snprintf(buf, size, "--");
  } else {
    snprintf(buf, size, "%d", value);
  }
}

// 742 -> "742", 7240 -> "7.2K", 12800 -> "12K"
void fmt_count(char *buf, size_t size, int value) {
  if (value < 1000) {
    fmt_value(buf, size, value);
  } else if (value < 10000) {
    snprintf(buf, size, "%d.%dK", value / 1000, (value % 1000) / 100);
  } else {
    snprintf(buf, size, "%dK", value / 1000);
  }
}

// 452 -> "7.5H"
void fmt_hours(char *buf, size_t size, int minutes) {
  if (minutes < 0) {
    snprintf(buf, size, "--");
  } else {
    snprintf(buf, size, "%d.%dH", minutes / 60, (minutes % 60) / 6);
  }
}
