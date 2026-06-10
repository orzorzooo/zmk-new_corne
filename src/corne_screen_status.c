#include <lvgl.h>

LV_IMG_DECLARE(corne_screen_01);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 160, 68);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *image = lv_img_create(screen);

#if LVGL_VERSION_MAJOR >= 9
    lv_image_set_src(image, &corne_screen_01);
    lv_image_set_pivot(image, 34, 80);
    lv_image_set_rotation(image, 900);
#else
    lv_img_set_src(image, &corne_screen_01);
    lv_img_set_pivot(image, 34, 80);
    lv_img_set_angle(image, 900);
#endif

    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

    return screen;
}
