#include <lvgl.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

LV_IMG_DECLARE(Frame1);
LV_IMG_DECLARE(Frame2);
LV_IMG_DECLARE(Frame3);

static lv_obj_t *battery_frame;

static const lv_img_dsc_t *frame_for_battery(uint8_t battery) {
    if (battery >= 70) {
        return &Frame1;
    }

    if (battery >= 40) {
        return &Frame2;
    }

    return &Frame3;
}

static void corne_screen_update(uint8_t battery) {
    if (battery_frame == NULL) {
        return;
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_image_set_src(battery_frame, frame_for_battery(battery));
#else
    lv_img_set_src(battery_frame, frame_for_battery(battery));
#endif
}

static uint8_t corne_screen_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return ev != NULL ? ev->state_of_charge : zmk_battery_state_of_charge();
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_screen_battery, uint8_t, corne_screen_update,
                            corne_screen_get_state)

ZMK_SUBSCRIPTION(corne_screen_battery, zmk_battery_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 160, 68);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    battery_frame = lv_img_create(screen);
    lv_obj_align(battery_frame, LV_ALIGN_CENTER, 0, 0);
    corne_screen_battery_init();

    return screen;
}
