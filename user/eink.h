#ifndef EINK_APP_H
#define EINK_APP_H

void eink_handshake();
void eink_erase();
void eink_update();
void eink_sleep();
void eink_wakeup();
void eink_draw_fill_circle(uint16 x, uint16 y, uint16 r);
void eink_draw_point(uint16 x, uint16 y);
void eink_draw_line(uint16 x1, uint16 y1, uint16 x2, uint16 y2);
void eink_draw_fill_rectangle(uint16 x1, uint16 y1, uint16 x2, uint16 y2);
void eink_set_baud_rate(uint32 br);
void eink_set_color(uint8 fg, uint8 bg);
void eink_draw_text(uint16 x, uint16 y, char const *str);
void eink_set_en_size(uint8 size);
void eink_draw_unconfigured(char const *wifi_name);
void eink_wait();

#endif
