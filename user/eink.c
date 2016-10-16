#include <ets_sys.h>
#include <osapi.h>
#include <gpio.h>
#include <user_interface.h>
#include "uart.h"
#include "eink.h"
#include <mem.h>

LOCAL int unconfirmed_commands = 0;

void eink_write(uint8 cmd, uint16 argv_length, uint8 *argv)
{
    uint8 parity = 0xA5; // parity without CMD & args is 0xA5
    uint16 len = 9 + argv_length, // default message length (without args) is 9 bytes
           i;

    uart_tx_one_char(0xA5);
    uart_tx_one_char(len >> 8);
    uart_tx_one_char(len & 0xFF);
    parity ^= len >> 8;
    parity ^= len & 0xFF;
    uart_tx_one_char(cmd);
    parity ^= cmd;

    for (i = 0; i < argv_length; i++) {
        parity ^= argv[i];
        uart_tx_one_char(argv[i]);
    }

    uart_tx_one_char(0xCC);
    uart_tx_one_char(0x33);
    uart_tx_one_char(0xC3);
    uart_tx_one_char(0x3C);
    uart_tx_one_char(parity);

    if (++unconfirmed_commands >= 100) { // flush EInk-UART queue every 100 commands
        eink_wait();
    }
}

void eink_handshake()
{
    eink_write(0x00, 0, NULL);
}

void eink_erase()
{
    eink_write(0x2E, 0, NULL);
}

void eink_update()
{
    eink_write(0x0A, 0, NULL);
}

void eink_sleep()
{
    unconfirmed_commands--; // do not wait it
    eink_write(0x08, 0, NULL);
}

void eink_wakeup()
{
    GPIO_OUTPUT_SET(GPIO_ID_PIN(12), 1);
    os_delay_us(100);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(12), 0);
    system_soft_wdt_feed();
    os_delay_us(10000);
    eink_handshake();
    eink_wait();
}

void eink_draw_fill_circle(uint16 x, uint16 y, uint16 r)
{
    uint8 args[] = {x >> 8, x & 0xFF, y >> 8, y & 0xFF, r >> 8, r & 0xFF};
    eink_write(0x27, 6, args);
}

void eink_draw_point(uint16 x, uint16 y)
{
    uint8 args[] = {x >> 8, x & 0xFF, y >> 8, y & 0xFF};
    eink_write(0x20, 4, args);
}

void eink_draw_line(uint16 x1, uint16 y1, uint16 x2, uint16 y2)
{
    uint8 args[] = {x1 >> 8, x1 & 0xFF, y1 >> 8, y1 & 0xFF, x2 >> 8, x2 & 0xFF, y2 >> 8, y2 & 0xFF};
    eink_write(0x22, 8, args);
}

void eink_draw_fill_rectangle(uint16 x1, uint16 y1, uint16 x2, uint16 y2)
{
    uint8 args[] = {x1 >> 8, x1 & 0xFF, y1 >> 8, y1 & 0xFF, x2 >> 8, x2 & 0xFF, y2 >> 8, y2 & 0xFF};
    eink_write(0x24, 8, args);
}

void eink_draw_text(uint16 x, uint16 y, char const *str)
{
    uint16 len = strlen(str), i;
    uint8 *args;
    args = (uint8*)os_malloc(sizeof(uint8) * (len + 5));

    args[0] = x >> 8;
    args[1] = x & 0xFF;
    args[2] = y >> 8;
    args[3] = y & 0xFF;
    args[4 + len] = 0;

    for (i = 0; i < len; i++) {
        args[4 + i] = str[i];
    }

    eink_write(0x30, 5 + len, args);
    os_free(args);
}

void eink_set_color(uint8 fg, uint8 bg)
{
    uint8 args[] = {fg, bg};
    eink_write(0x10, 2, args);
}

void eink_set_baud_rate(uint32 br)
{
    uint8 args[] = {(br >> 24) & 0xFF, (br >> 16) & 0xFF, (br >> 8) & 0xFF, br & 0xFF};
    eink_write(0x01, 4, args);
}

void eink_set_en_size(uint8 size)
{
    eink_write(0x1E, 1, &size);
}

void eink_wait()
{
    int o = 0, k = 0, c;
    do {
        c = uart0_rx_one_char();
        if (c == 79) {
            o++;
        } else if (c == 75) {
            k++;
        }
        if (o > 0 && k > 0) {
            unconfirmed_commands--;
            o--;
            k--;
        }
        system_soft_wdt_feed();
        os_delay_us(10);
    } while (unconfirmed_commands > 0);
}

void eink_draw_unconfigured(char const *wifi_name)
{
    eink_erase();
    eink_set_en_size(1);
    eink_draw_text(260, 10, "device is not configured");
    eink_set_en_size(2);
    eink_draw_text(180, 200, "Connect to access point");
    eink_set_en_size(3);
    eink_draw_text(270, 260, wifi_name);
    eink_set_en_size(1);
    eink_draw_text(200, 340, "And browse to: http://192.168.4.1/");
    eink_update();
    eink_wait();
}
