#include "ui.h"
#include "ui_input.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "ui";

static lv_obj_t *s_status;

static void menu_item_event_cb(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_target_obj(e);
    const char *name = lv_label_get_text(lv_obj_get_child(item, 0));

    switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED:
        lv_label_set_text_fmt(s_status, "> %s", name);
        ESP_LOGI(TAG, "selected: %s", name);
        break;
    case LV_EVENT_KEY:
        if (lv_event_get_key(e) == LV_KEY_ESC) {
            lv_label_set_text(s_status, "< back");
        }
        break;
    default:
        break;
    }
}

static void add_menu_item(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, lv_pct(100), 20);
    lv_obj_add_event_cb(btn, menu_item_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

static void build_main_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 2, 0);
    lv_obj_set_style_pad_row(scr, 2, 0);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "BLE: --  USB: --");

    lv_obj_t *menu = lv_obj_create(scr);
    lv_obj_set_width(menu, lv_pct(100));
    lv_obj_set_flex_grow(menu, 1);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(menu, 2, 0);
    lv_obj_set_style_pad_row(menu, 2, 0);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);

    add_menu_item(menu, "Type test");
    add_menu_item(menu, "Keyboard layout");
    add_menu_item(menu, "Pairing");
    add_menu_item(menu, "About");
}

esp_err_t ui_start(button_handle_t btn)
{
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                              lv_palette_main(LV_PALETTE_CYAN), true, LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);

    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_group_set_wrap(group, true);

    esp_err_t ret = ESP_OK;
    lv_indev_t *indev = ui_input_create(btn);
    if (indev) {
        lv_indev_set_group(indev, group);
        build_main_screen();
    } else {
        ret = ESP_ERR_NO_MEM;
    }

    lvgl_port_unlock();
    return ret;
}
