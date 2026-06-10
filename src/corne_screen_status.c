#include <lvgl.h>
#include <stdio.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

LV_IMG_DECLARE(corne_screen_01);

struct corne_screen_state {
    bool connected;
    uint8_t battery;
};

static lv_obj_t *status_label;

static void corne_screen_update(struct corne_screen_state state) {
    if (status_label == NULL) {
        return;
    }

    char text[18];
    snprintf(text, sizeof(text), "%s  %u%%", state.connected ? "BT" : "--", state.battery);
    lv_label_set_text(status_label, text);
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
    lv_image_set_src(image, &corne_screen_01);
#else
    lv_img_set_src(image, &corne_screen_01);
#endif

    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

    status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(status_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(status_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(status_label, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_right(status_label, 2, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_TOP_RIGHT, -2, 0);
    corne_screen_status_init();

    return screen;
}
