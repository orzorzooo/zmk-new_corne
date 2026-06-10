#include <lvgl.h>
#include <string.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

LV_IMG_DECLARE(corne_screen_2);

#define STATUS_CANVAS_SIZE 68

struct corne_screen_state {
    bool connected;
    uint8_t battery;
};

static lv_obj_t *status_canvas;
static lv_color_t status_cbuf[STATUS_CANVAS_SIZE * STATUS_CANVAS_SIZE];
static lv_color_t status_cbuf_tmp[STATUS_CANVAS_SIZE * STATUS_CANVAS_SIZE];

static void init_rect_dsc(lv_draw_rect_dsc_t *dsc, lv_color_t color) {
    lv_draw_rect_dsc_init(dsc);
    dsc->bg_color = color;
}

static void init_label_dsc(lv_draw_label_dsc_t *dsc) {
    lv_draw_label_dsc_init(dsc);
    dsc->color = lv_color_black();
    dsc->font = &lv_font_montserrat_16;
    dsc->align = LV_TEXT_ALIGN_RIGHT;
}

static void draw_battery(lv_obj_t *canvas, uint8_t battery) {
    lv_draw_rect_dsc_t fg_dsc;
    lv_draw_rect_dsc_t bg_dsc;
    init_rect_dsc(&fg_dsc, lv_color_black());
    init_rect_dsc(&bg_dsc, lv_color_white());

    lv_canvas_draw_rect(canvas, 0, 2, 29, 12, &fg_dsc);
    lv_canvas_draw_rect(canvas, 1, 3, 27, 10, &bg_dsc);
    lv_canvas_draw_rect(canvas, 2, 4, (battery + 2) / 4, 8, &fg_dsc);
    lv_canvas_draw_rect(canvas, 30, 5, 3, 6, &fg_dsc);
    lv_canvas_draw_rect(canvas, 31, 6, 1, 4, &bg_dsc);
}

static void rotate_status_canvas(lv_obj_t *canvas) {
    memcpy(status_cbuf_tmp, status_cbuf, sizeof(status_cbuf_tmp));

    lv_img_dsc_t img = {
        .header.cf = LV_IMG_CF_TRUE_COLOR,
        .header.w = STATUS_CANVAS_SIZE,
        .header.h = STATUS_CANVAS_SIZE,
        .data = (void *)status_cbuf_tmp,
    };

    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
    lv_canvas_transform(canvas, &img, 900, LV_IMG_ZOOM_NONE, -1, 0, STATUS_CANVAS_SIZE / 2,
                        STATUS_CANVAS_SIZE / 2, true);
}

static void corne_screen_update(struct corne_screen_state state) {
    if (status_canvas == NULL) {
        return;
    }

    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_label_dsc_t label_dsc;
    init_rect_dsc(&bg_dsc, lv_color_white());
    init_label_dsc(&label_dsc);

    lv_canvas_draw_rect(status_canvas, 0, 0, STATUS_CANVAS_SIZE, STATUS_CANVAS_SIZE, &bg_dsc);
    draw_battery(status_canvas, state.battery);
    lv_canvas_draw_text(status_canvas, 0, 0, STATUS_CANVAS_SIZE, &label_dsc,
                        state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
    rotate_status_canvas(status_canvas);
}

static struct corne_screen_state corne_screen_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *battery_ev = as_zmk_battery_state_changed(eh);
    const struct zmk_split_peripheral_status_changed *status_ev =
        as_zmk_split_peripheral_status_changed(eh);

    return (struct corne_screen_state){
        .connected =
            status_ev != NULL ? status_ev->connected : zmk_split_bt_peripheral_is_connected(),
        .battery = battery_ev != NULL ? battery_ev->state_of_charge : zmk_battery_state_of_charge(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_screen_status, struct corne_screen_state, corne_screen_update,
                            corne_screen_get_state)

ZMK_SUBSCRIPTION(corne_screen_status, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(corne_screen_status, zmk_split_peripheral_status_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 160, 68);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *image = lv_img_create(screen);

#if LVGL_VERSION_MAJOR >= 9
    lv_image_set_src(image, &corne_screen_2);
#else
    lv_img_set_src(image, &corne_screen_2);
#endif

    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

    status_canvas = lv_canvas_create(screen);
    lv_obj_align(status_canvas, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(status_canvas, status_cbuf, STATUS_CANVAS_SIZE, STATUS_CANVAS_SIZE,
                         LV_IMG_CF_TRUE_COLOR);
    corne_screen_status_init();

    return screen;
}
