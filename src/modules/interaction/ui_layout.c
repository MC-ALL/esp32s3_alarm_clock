#include <ui_layout.h>

lv_color_t ui_color_black(void) { return lv_color_black(); }
lv_color_t ui_color_white(void) { return lv_color_white(); }
lv_color_t ui_color_blue(void) { return lv_color_hex(0x0057d8); }
lv_color_t ui_color_orange(void) { return lv_color_hex(0xc95400); }
lv_color_t ui_color_purple(void) { return lv_color_hex(0x5b2cbf); }
lv_color_t ui_color_teal(void) { return lv_color_hex(0x007a99); }
lv_color_t ui_color_green(void) { return lv_color_hex(0x16833a); }
lv_color_t ui_color_dark(void) { return lv_color_hex(0x101418); }
lv_color_t ui_color_gray(void) { return lv_color_hex(0x26313d); }
lv_color_t ui_color_red(void) { return lv_color_hex(0xc31828); }
lv_color_t ui_color_yellow(void) { return lv_color_hex(0xd19a00); }
lv_color_t ui_color_humi(void) { return lv_color_hex(0x006f6a); }
lv_color_t ui_color_lux(void) { return lv_color_hex(0x6e7300); }
lv_color_t ui_color_ip(void) { return lv_color_hex(0x245a7a); }
lv_color_t ui_color_sync(void) { return lv_color_hex(0x5d4e9d); }
lv_color_t ui_color_todo_net(void) { return lv_color_hex(0x476600); }
lv_color_t ui_color_panel(void) { return lv_color_hex(0x000000); }

void ui_obj_plain(lv_obj_t *obj, lv_color_t bg)
{
	lv_obj_remove_style_all(obj);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(obj, bg, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t bg)
{
	lv_obj_t *obj = lv_obj_create(parent);
	ui_obj_plain(obj, bg);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, w, h);
	return obj;
}

lv_obj_t *ui_scroll_area(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t bg)
{
	lv_obj_t *obj = ui_box(parent, x, y, w, h, bg);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scroll_dir(obj, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_ON);
	lv_obj_set_style_width(obj, 4, LV_PART_SCROLLBAR);
	lv_obj_set_style_radius(obj, 0, LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_color(obj, ui_color_white(), LV_PART_SCROLLBAR);
	return obj;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
		   int32_t x, int32_t y, int32_t w, int32_t h, lv_text_align_t align)
{
	lv_obj_t *obj = lv_label_create(parent);
	lv_obj_remove_style_all(obj);
	lv_obj_set_style_text_font(obj, font, 0);
	lv_obj_set_style_text_color(obj, color, 0);
	lv_obj_set_style_text_align(obj, align, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
	lv_label_set_text(obj, text);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, w, h);
	return obj;
}

lv_obj_t *ui_label_wrap(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
			int32_t x, int32_t y, int32_t w, int32_t h, lv_text_align_t align)
{
	lv_obj_t *obj = ui_label(parent, text, font, color, x, y, w, h, align);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
	return obj;
}
