#include <pebble.h>

#define PERSIST_BG_COLOR      1
#define PERSIST_DIAL_COLOR    2
#define PERSIST_HANDS_COLOR   3
#define PERSIST_TRANSPARENT   4

static GColor s_bg_color;
static GColor s_dial_color;
static GColor s_hands_color;
static bool s_transparent_hands;

static int s_hours, s_minutes;
static char s_date_buf[4];

static Window *s_window;
static Layer *s_bg_layer;
static Layer *s_hands_layer;
static Layer *s_date_border_layer;
static TextLayer *s_date_text_layer;

static GFont s_font_dial;
static GFont s_font_date;


static void tick_handler(struct tm *tick_time, TimeUnits units_changed);

static void apply_colors(void) {
    window_set_background_color(s_window, s_bg_color);
    text_layer_set_text_color(s_date_text_layer, s_dial_color);
    layer_mark_dirty(s_bg_layer);
    layer_mark_dirty(s_hands_layer);
    layer_mark_dirty(s_date_border_layer);
}

// ----- settings persistence -----

static void load_settings(void) {
    s_bg_color = persist_exists(PERSIST_BG_COLOR) ?
        (GColor){ .argb = (uint8_t)persist_read_int(PERSIST_BG_COLOR) } : GColorBlack;
    s_dial_color = persist_exists(PERSIST_DIAL_COLOR) ?
        (GColor){ .argb = (uint8_t)persist_read_int(PERSIST_DIAL_COLOR) } : GColorWhite;
    s_hands_color = persist_exists(PERSIST_HANDS_COLOR) ?
        (GColor){ .argb = (uint8_t)persist_read_int(PERSIST_HANDS_COLOR) } : GColorRed;
    s_transparent_hands = persist_exists(PERSIST_TRANSPARENT) ?
        (persist_read_int(PERSIST_TRANSPARENT) != 0) : false;
}

// ----- drawing -----

static void bg_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    static const char *nums[] = {
        "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"
    };
    // Center positions scaled from the original 336x336 background
    // The layout is a rounded rectangle, not a circle or grid
    static const int16_t centers[12][2] = {
        {100,  18},  // 12: top center
        {162,  18},  // 1:  top right corner
        {183,  63},  // 2:  right upper edge
        {183, 112},  // 3:  right center edge
        {183, 161},  // 4:  right lower edge
        {158, 210},  // 5:  bottom right corner
        {100, 210},  // 6:  bottom center
        { 40, 210},  // 7:  bottom left corner
        { 18, 161},  // 8:  left lower edge
        { 18, 112},  // 9:  left center edge
        { 18,  63},  // 10: left upper edge
        { 40,  18},  // 11: top left corner
    };

    graphics_context_set_text_color(ctx, s_dial_color);
    for (int i = 0; i < 12; i++) {
        GRect box = GRect(centers[i][0] - 25, centers[i][1] - 18, 50, 36);
        graphics_draw_text(ctx, nums[i], s_font_dial, box,
                           GTextOverflowModeTrailingEllipsis,
                           GTextAlignmentCenter, NULL);
    }
}

static void rotate_point(GPoint center, int px, int py, int32_t angle, GPoint *out) {
    int32_t sa = sin_lookup(angle);
    int32_t ca = cos_lookup(angle);
    int d = -py;
    out->x = center.x + (sa * d + ca * px) / TRIG_MAX_RATIO;
    out->y = center.y + (-ca * d + sa * px) / TRIG_MAX_RATIO;
}

static void draw_opaque_hand(GContext *ctx, GPoint center,
                              int32_t angle, int tip_d, int base_d, int width) {
    GPoint tip, base;
    rotate_point(center, 0, tip_d, angle, &tip);
    rotate_point(center, 0, base_d, angle, &base);

    graphics_context_set_stroke_color(ctx, s_hands_color);
    graphics_context_set_stroke_width(ctx, width);
    graphics_draw_line(ctx, tip, base);

    graphics_context_set_fill_color(ctx, s_hands_color);
    graphics_fill_circle(ctx, tip, width / 2);
    graphics_fill_circle(ctx, base, width / 2);
}

static void draw_transparent_hand(GContext *ctx, GPoint center,
                                   int32_t angle, int half_w,
                                   int line_top, int line_bot) {
    GPoint lt, lb, rt, rb;
    rotate_point(center, -half_w, line_top, angle, &lt);
    rotate_point(center, -half_w, line_bot, angle, &lb);
    rotate_point(center,  half_w, line_top, angle, &rt);
    rotate_point(center,  half_w, line_bot, angle, &rb);

    graphics_context_set_stroke_color(ctx, s_hands_color);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, lt, lb);
    graphics_draw_line(ctx, rt, rb);

    GPoint top_c, bot_c;
    rotate_point(center, 0, line_top, angle, &top_c);
    rotate_point(center, 0, line_bot, angle, &bot_c);

    int arc_d = half_w * 2 + 1;
    int32_t start_out = angle - TRIG_MAX_ANGLE / 4;
    int32_t end_out   = angle + TRIG_MAX_ANGLE / 4;
    graphics_draw_arc(ctx,
        GRect(top_c.x - half_w, top_c.y - half_w, arc_d, arc_d),
        GOvalScaleModeFitCircle, start_out, end_out);

    int32_t start_in = angle + TRIG_MAX_ANGLE / 4;
    int32_t end_in   = angle + 3 * TRIG_MAX_ANGLE / 4;
    graphics_draw_arc(ctx,
        GRect(bot_c.x - half_w, bot_c.y - half_w, arc_d, arc_d),
        GOvalScaleModeFitCircle, start_in, end_in);
}

static void draw_root_line(GContext *ctx, GPoint center, int32_t angle,
                            int length, GColor color) {
    GPoint top, bot;
    rotate_point(center, 0, -length, angle, &top);
    bot = center;
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, top, bot);
}

static void hands_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);

    int32_t hour_total = (s_hours % 12) * 3600 + s_minutes * 60;
    int32_t hour_angle = (int32_t)((int64_t)TRIG_MAX_ANGLE * hour_total / 43200);
    int32_t min_angle = TRIG_MAX_ANGLE * s_minutes / 60;

    if (s_transparent_hands) {
        draw_transparent_hand(ctx, center, hour_angle, 4, -54, -18);
        draw_transparent_hand(ctx, center, min_angle,  4, -77, -18);
    } else {
        draw_opaque_hand(ctx, center, hour_angle, -58, -14, 8);
        draw_opaque_hand(ctx, center, min_angle, -81, -14, 8);
    }

    draw_root_line(ctx, center, hour_angle, 14, s_hands_color);
    draw_root_line(ctx, center, min_angle,  14, s_hands_color);

    graphics_context_set_fill_color(ctx, s_hands_color);
    graphics_fill_circle(ctx, center, 5);
    graphics_context_set_fill_color(ctx, s_dial_color);
    graphics_fill_circle(ctx, center, 4);
    graphics_context_set_fill_color(ctx, s_bg_color);
    graphics_fill_circle(ctx, center, 2);
}

static void date_border_update_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    graphics_context_set_stroke_color(ctx, s_dial_color);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, b);
}

// ----- tick service -----

static void update_tick_subscription(void) {
    tick_timer_service_unsubscribe();
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    s_hours = tick_time->tm_hour;
    s_minutes = tick_time->tm_min;

    if (units_changed & DAY_UNIT) {
        snprintf(s_date_buf, sizeof(s_date_buf), "%02d", tick_time->tm_mday);
        text_layer_set_text(s_date_text_layer, s_date_buf);
    }

    layer_mark_dirty(s_hands_layer);
}

// ----- AppMessage -----

static void inbox_received(DictionaryIterator *iter, void *context) {
    Tuple *t;

    t = dict_find(iter, MESSAGE_KEY_BACKGROUND_COLOR);
    if (t) {
        s_bg_color = GColorFromHEX(t->value->int32);
        persist_write_int(PERSIST_BG_COLOR, s_bg_color.argb);
    }

    t = dict_find(iter, MESSAGE_KEY_DIAL_COLOR);
    if (t) {
        s_dial_color = GColorFromHEX(t->value->int32);
        persist_write_int(PERSIST_DIAL_COLOR, s_dial_color.argb);
    }

    t = dict_find(iter, MESSAGE_KEY_HANDS_COLOR);
    if (t) {
        s_hands_color = GColorFromHEX(t->value->int32);
        persist_write_int(PERSIST_HANDS_COLOR, s_hands_color.argb);
    }

    t = dict_find(iter, MESSAGE_KEY_TRANSPARENT_HANDS);
    if (t) {
        s_transparent_hands = (t->value->int32 != 0);
        persist_write_int(PERSIST_TRANSPARENT, s_transparent_hands ? 1 : 0);
    }

    apply_colors();
}

static void inbox_dropped(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Message dropped: %d", (int)reason);
}

// ----- window -----

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    int cx = bounds.size.w / 2;
    int cy = bounds.size.h / 2;

    s_bg_layer = layer_create(bounds);
    layer_set_update_proc(s_bg_layer, bg_update_proc);
    layer_add_child(window_layer, s_bg_layer);

    int date_w = 36;
    int date_h = 24;
    int date_y = cy + 48;
    s_date_border_layer = layer_create(GRect(cx - date_w / 2 - 3, date_y - 2,
                                              date_w + 6, date_h + 4));
    layer_set_update_proc(s_date_border_layer, date_border_update_proc);
    layer_add_child(window_layer, s_date_border_layer);

    s_date_text_layer = text_layer_create(GRect(cx - date_w / 2, date_y, date_w, date_h));
    text_layer_set_background_color(s_date_text_layer, GColorClear);
    text_layer_set_text_color(s_date_text_layer, s_dial_color);
    text_layer_set_font(s_date_text_layer, s_font_date);
    text_layer_set_text_alignment(s_date_text_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_date_text_layer));

    s_hands_layer = layer_create(bounds);
    layer_set_update_proc(s_hands_layer, hands_update_proc);
    layer_add_child(window_layer, s_hands_layer);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    s_hours = t->tm_hour;
    s_minutes = t->tm_min;
    snprintf(s_date_buf, sizeof(s_date_buf), "%02d", t->tm_mday);
    text_layer_set_text(s_date_text_layer, s_date_buf);

    apply_colors();
}

static void window_unload(Window *window) {
    layer_destroy(s_bg_layer);
    layer_destroy(s_hands_layer);
    layer_destroy(s_date_border_layer);
    text_layer_destroy(s_date_text_layer);
}

// ----- init / deinit -----

static void init(void) {
    s_font_dial = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_SAN_FRANCISCO_26));
    s_font_date = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_SAN_FRANCISCO_18));

    load_settings();

    s_window = window_create();
    window_set_background_color(s_window, s_bg_color);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);

    update_tick_subscription();

    app_message_register_inbox_received(inbox_received);
    app_message_register_inbox_dropped(inbox_dropped);
    app_message_open(256, 64);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();

    fonts_unload_custom_font(s_font_dial);
    fonts_unload_custom_font(s_font_date);

    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
