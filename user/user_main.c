#include <user_interface.h>
#include <ets_sys.h>
#include <osapi.h>
#include <gpio.h>
#include <os_type.h>
#include "user_config.h"
#include "uart.h"
#include "eink.h"
#include <ip_addr.h>
#include <c_types.h>
#include <espconn.h>
#include "settings.h"
#include "httpd.h"
#include <mem.h>

#ifndef VERSION
#define VERSION "dev"
#endif

struct espconn host_conn;
struct GlobalSettings global_settings;
ip_addr_t host_ip;
esp_tcp host_tcp;
int sleep_time_ms = 600 * 1000;

void ICACHE_FLASH_ATTR delay_ms(int ms) {
	while (ms > 0) {
		system_soft_wdt_feed();
		os_delay_us(10000);
		ms -= 10;
	}
}

extern UartDevice UartDev;

// Web server for config setting
static const char *index_200 =
		"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
				"<html><head><title>E-ink Wifi Setup</title></head><body>"
				"<form action=\"push\" method=POST>"
				"<center><table>"
				"<tr><td>ESSID:</td><td><input type=\"text\" name=\"essid\"></td></tr>"
				"<tr><td>Password:</td><td><input type=\"text\" name=\"pass\"></td></tr>"
				"<tr><td>Server:</td><td><input type=\"text\" name=\"host\"></td></tr>"
				"<tr><td>Port:</td><td><input type=\"text\" name=\"port\"></td></tr>"
				"<tr><td>URL:</td><td><input type=\"text\" name=\"path\"></td></tr>"
				"</table><input type=\"submit\"></center>"
				"</form>"
				"</body></html>";

static const char *push_200 =
		"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
				"<html><head><title>E-ink Wifi Setup</title></head><body>"
				"<center>"
				"Config saved! Rebooting ... <br/> You can safely disconnect from this wifi access point"
				"</center>"
				"</body></html>";

static const char *wifi_name = "EINKWIFI";

unsigned ICACHE_FLASH_ATTR get_index(char * buffer, const char * body) {
	// Just return a form!
	strcpy(buffer, index_200);
	return strlen(index_200);
}
unsigned ICACHE_FLASH_ATTR push_settings(char * buffer, const char * body) {
	os_printf("TOPARSE %s\n", body);
	// Parse body variables
	parse_form_s(body, global_settings.conf_essid, "essid");
	parse_form_s(body, global_settings.conf_passw, "pass");
	parse_form_s(body, global_settings.conf_hostn, "host");
	parse_form_s(body, global_settings.conf_path, "path");
	global_settings.conf_port = parse_form_d(body, "port");
#define Z(X) global_settings.X[sizeof(global_settings.X) - 1] = '\0'
	Z(conf_essid);
	Z(conf_passw);
	Z(conf_hostn);
	Z(conf_path);
#undef Z

	os_printf("Parsed %s %s %s\n", global_settings.conf_essid,
			global_settings.conf_passw, global_settings.conf_hostn);
	os_printf("Parsed path: %s\n", global_settings.conf_path);

	// Compute checksum
	check_settings_checksum(&global_settings.checksum, &global_settings);
	// Update settings
	store_settings(&global_settings);

	strcpy(buffer, push_200);
	return strlen(push_200);
}

t_url_desc urls[] = { { "/", get_index }, { "/push", push_settings }, { NULL, NULL }, };

void ICACHE_FLASH_ATTR start_web_server() {
	// Set SoftAP mode
	if (wifi_get_opmode() != 2)
		wifi_set_opmode(2);

	// Setup AP mode
	struct softap_config config;
	strcpy(config.ssid, wifi_name);
	config.password[0] = 0;
	config.channel = 1;
	config.ssid_hidden = 0;
	config.authmode = AUTH_OPEN;
	config.ssid_len = 0;
	config.beacon_interval = 100;
	config.max_connection = 4;

	wifi_softap_set_config(&config); // Set ESP8266 softap config .

	httpd_start(80, urls);
}

// Check if the GPIO4 is grounded (if yes, we clear the current the configuration)
int ICACHE_FLASH_ATTR clearbutton_pressed() {
	// Set GPIO14 as input mode with pull up
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO4_U);
	// Set as input now
	gpio_output_set(0, 0, 0, BIT4);

	return GPIO_INPUT_GET(GPIO_ID_PIN(4));
}

int ICACHE_FLASH_ATTR parse_answer(char * pdata, unsigned short len) {
	if (len < 13 || memcmp(pdata, "HTTP/1.", 7) != 0) {
		return 404; // No answer line found
	}
	return cheap_atoi(pdata + 9);
}

// Advance the pointer and length while the find character is not found (if negate_test is 0), or until it's found (if negate_test is 1)
int ICACHE_FLASH_ATTR glob_until(char find, char ** ppdata,
		unsigned short * len, int negate_test) {
	while (*len && ((**ppdata != find) ^ negate_test)) {
		--(*len);
		++(*ppdata);
	}
	return *len != 0;
}

int ICACHE_FLASH_ATTR parse_header(char ** ppdata, unsigned short * len,
		char ** header, char ** value) {
	*header = *ppdata;
	if (**ppdata == '\r' && *len && *(*ppdata + 1) == '\n') {
		(*len) -= 2;
		(*ppdata) += 2;
		return 0; // End of headers found
	}
	glob_until(':', ppdata, len, 0);
	if (!*len)
		return -1; // Unexpected end of stream

	**ppdata = '\0'; // Make it zero terminated so we can parse it
	--(*len);
	++(*ppdata);

	// Strip whitespace too
	glob_until(' ', ppdata, len, 1);
	*value = *ppdata;

	if (!glob_until('\n', ppdata, len, 0))
		return -1; // Unexpected end of stream

	*(*ppdata - 1) = '\0'; // Make it zero terminated too
	--(*len);
	++(*ppdata);
	return 1; // Header found
}

void coords_increment(uint16 *x, uint16 *y, uint8 inc) {
	*x = *x + inc;
	if (*x > 800) {
		*y += 1;
		*x = *x % 800;
	}
}

void eink_draw_color_point(uint8 color, uint16 x, uint16 y) {
	static uint8 last_color = 0;
	if (color == 3) {
		return;
	}
	if (color != last_color) {
		eink_set_color(color, 3);
		last_color = color;
	}
	eink_draw_point(x, y);
}

int eink_draw_rle(unsigned short len, char *pdata) {
	static char stored_data[200];
	static unsigned short stored_len;
	static uint16 x = 1, y = 1;
	char rle_header = *pdata;
	unsigned short rle_len = (rle_header & 0x7F) + 1;
	short i;
	int processed = 1;
	static short cnt1 = 0, cnt0 = 0;

	if (stored_len > 0) {
		rle_header = stored_data[0];
		rle_len = rle_header & 0x80 ? (rle_header & 0x7F) + 1 : 1;
		os_memcpy(stored_data + stored_len, pdata, rle_len + 1 - stored_len);
		i = stored_len;
		stored_len = 0;
		eink_draw_rle(rle_len + 1, stored_data);
		processed = rle_len + 1 - i;
	} else if (((rle_header & 0x80) == 0x80 && len - 1 < rle_len) || len == 1) {
		os_memcpy(stored_data, pdata, len);
		processed = stored_len = len;
	} else if ((rle_header & 0x80) == 0x80) {
		processed += rle_len;
		while (rle_len--) {
			pdata++;
			for (i = 6; i >= 0; i -= 2) {
				eink_draw_color_point(((*pdata) >> i) & 3, x, y);
				coords_increment(&x, &y, 1);
			}
		}
	} else if ((rle_header & 0x80) == 0) {
		pdata++;
		processed++;
		while (rle_len--) {
			for (i = 6; i >= 0; i -= 2) {
				eink_draw_color_point(((*pdata) >> i) & 3, x, y);
				coords_increment(&x, &y, 1);
			}
		}
	}
	return processed;
}

void ICACHE_FLASH_ATTR data_received(void *arg, char *pdata, unsigned short len) {
	struct espconn *conn = arg;
	char *header, *value;
	int ret = 0, content_len = len;

	os_printf("Data received: %d bytes\n", len);
	static int topline_received = 0;
	if (!topline_received) {
		// Scan through the data to strip headers!
		if ((ret = parse_answer(pdata, len)) > 302) {
			os_printf("Bad answer from server: %d from %s\n", ret, pdata);
			eink_wakeup();
			eink_erase();
			eink_draw_text(200, 200, "Shit happened");
			eink_update();
			return;
		}
		topline_received = 1;
		// Skip answer line (we don't care about the result here)
		glob_until('\n', &pdata, &len, 0);
		pdata++;
		len--;
	}

	static int header_received = 0;
	if (!header_received) {
		// Then parse all header lines, and act accordingly
		while ((ret = parse_header(&pdata, &len, &header, &value)) > 0) {
#define STREQ(X,Y) memcmp(X, Y, sizeof(Y)) == 0
			if (STREQ(header, "Sleep-Duration-Ms")) {
				sleep_time_ms = cheap_atoi(value);
			} else if (STREQ(header, "Content-Length")) {
				content_len = cheap_atoi(value);
			}
#undef STREQ
			os_printf("Got header %s with value %s\n", header, value);
		}
		if (ret == 0) {
			header_received = 1;
			// Tell image is coming!
			eink_wakeup();
			eink_erase();
		}
	}

	if (header_received) {
		unsigned short offset = 0;
		while (len - offset > 0) {
			offset += eink_draw_rle(len - offset, pdata + offset);
		}
	}
}

void nullwr(char c) {
}

void ICACHE_FLASH_ATTR tcp_connected(void *arg) {
	struct espconn *conn = arg;

	os_printf("%s\n", __FUNCTION__);
	espconn_regist_recvcb(conn, data_received);

	char buffer[256];
	os_sprintf(buffer,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"User-agent: WEID %s\r\n"
			"Connection: close\r\n\r\n",
			global_settings.conf_path, global_settings.conf_hostn, VERSION);

	espconn_send(conn, buffer, os_strlen(buffer));
}

void ICACHE_FLASH_ATTR tcp_disconnected(void *arg) {
	struct espconn *conn = arg;
	eink_update();
	eink_wait();
	eink_sleep();
	wifi_station_disconnect();
	system_deep_sleep(10000000);

	// put_back_to_sleep();
}

void ICACHE_FLASH_ATTR tcp_error(void *arg, sint8 err) {
	char t[400];
	os_sprintf(t, "TCP Failure: %d", err);
	// TCP error, just show a message and back to sleep!
	eink_wakeup();
	eink_draw_text(200, 300, t);
	eink_update();
}

void ICACHE_FLASH_ATTR dns_done_cb(const char *name, ip_addr_t *ipaddr,
		void *arg) {
	struct espconn *conn = arg;

	os_printf("%s\n", __FUNCTION__);

	if (ipaddr == NULL) {
		os_printf("DNS lookup failed\n");
		wifi_station_disconnect();

		eink_wakeup();
		eink_erase();
		eink_draw_text(200, 200, "DNS Failure");
		eink_update();
	} else {
		os_printf("Connecting...\n");

		conn->type = ESPCONN_TCP;
		conn->state = ESPCONN_NONE;
		conn->proto.tcp = &host_tcp;
		espconn_regist_time(conn, 30, 0);
		conn->proto.tcp->local_port = espconn_port();
		conn->proto.tcp->remote_port = global_settings.conf_port;
		conn->proto.tcp->remote_port = global_settings.conf_port;
		os_memcpy(conn->proto.tcp->remote_ip, &ipaddr->addr, 4);

		espconn_regist_connectcb(conn, tcp_connected);
		espconn_regist_disconcb(conn, tcp_disconnected);
		espconn_regist_reconcb(conn, tcp_error);

		espconn_connect(conn);
	}
}

void ICACHE_FLASH_ATTR wifi_callback(System_Event_t *evt) {
	os_printf("%s: %d\n", __FUNCTION__, evt->event);

	switch (evt->event) {
	case EVENT_STAMODE_CONNECTED: {
		os_printf("connect to ssid %s, channel %d\n",
				evt->event_info.connected.ssid,
				evt->event_info.connected.channel);
		break;
	}

	case EVENT_STAMODE_DISCONNECTED: {
		if (evt->event_info.disconnected.reason != REASON_ASSOC_LEAVE) {
			eink_wakeup();
			// eink_erase();
			eink_draw_text(200, 200, "Disconnected");
			eink_draw_text(100, 250, evt->event_info.disconnected.ssid);
			char temp[400];
			os_sprintf(temp, "Reason: %d", evt->event_info.disconnected.reason);
			eink_draw_text(100, 300, temp);
			eink_update();
		}
		os_printf("disconnect from ssid %s, reason %d\n",
				evt->event_info.disconnected.ssid,
				evt->event_info.disconnected.reason);
		break;
	}

	case EVENT_STAMODE_GOT_IP: {
		os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
				IP2STR(&evt->event_info.got_ip.ip),
				IP2STR(&evt->event_info.got_ip.mask),
				IP2STR(&evt->event_info.got_ip.gw));
		os_printf("\n");

		espconn_gethostbyname(&host_conn, global_settings.conf_hostn, &host_ip,
				dns_done_cb);
		break;
	}

	case EVENT_STAMODE_DHCP_TIMEOUT: {
		eink_wakeup();
		eink_erase();
		eink_draw_text(200, 200, "DHCP Failure");
		eink_update();
		break;
	}
	}
}

#define FUNC_U0CTS    4
#define FUNC_U0RTS    4

void ICACHE_FLASH_ATTR prepare_uart(void) {
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_U0CTS); //CONFIG MTCK PIN FUNC TO U0CTS
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_U0RTS); //CONFIG MTDO PIN FUNC TO U0RTS
	SET_PERI_REG_MASK(0x3ff00028, BIT2); //SWAP PIN : U0TXD<==>U0RTS(MTDO) , U0RXD<==>U0CTS(MTCK)
}

//Init function
void ICACHE_FLASH_ATTR user_init() {
	struct rst_info* rst = system_get_rst_info();

	os_memset(&global_settings, 0, sizeof(global_settings));

	if (rst->reason == REASON_EXT_SYS_RST || rst->reason == REASON_WDT_RST
			|| rst->reason == REASON_DEFAULT_RST) {
		gpio_init();
		uart_init(BIT_RATE_115200, BIT_RATE_115200);
		prepare_uart();
		os_printf("Booting...\n");
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
		gpio_output_set(BIT12 | BIT14, 0, BIT12 | BIT14, 0);
		UartDev.exist_parity = 0;

		delay_ms(1); // Wait a bit to let it reset cleanly

		if (clearbutton_pressed() == 0)
			store_settings(&global_settings); // This will nuke the saved settings

		GPIO_OUTPUT_SET(GPIO_ID_PIN(14), 1);
		os_delay_us(100);
		GPIO_OUTPUT_SET(GPIO_ID_PIN(14), 0);
		os_delay_us(10000);

		eink_handshake();
		eink_wait();

		eink_set_baud_rate(460800);
		/*
		 According to the doc:
		 You may need to wait 100ms for the module to return the result after sending this command,
		 since the host may take a period of time to change its Baud rate.
		 */
		delay_ms(100);
		uart_init(BIT_RATE_460800, BIT_RATE_115200);
		eink_wait();
	} else {
		delay_ms(1);
		UartDev.exist_parity = 0;
		uart_init(BIT_RATE_460800, BIT_RATE_115200);
		prepare_uart();
		delay_ms(1);
		eink_wakeup();
	}

	if (clearbutton_pressed() == 0)
		store_settings(&global_settings);

	if (recover_settings(&global_settings)) {
		// We got some settings, now go and connect
		static struct station_config config;
		wifi_station_set_hostname("einkdisp");
		wifi_set_opmode_current(STATION_MODE);

		os_printf("Info %s, %s, %s, %d\n", global_settings.conf_passw,
				global_settings.conf_essid, global_settings.conf_hostn,
				global_settings.conf_port);

		config.bssid_set = 0;
		os_memcpy(&config.ssid, global_settings.conf_essid,
				sizeof(global_settings.conf_essid));
		os_memcpy(&config.password, global_settings.conf_passw,
				sizeof(global_settings.conf_passw));
		wifi_station_set_config(&config);

		// Connect to the server, get some stuff and process it!
		wifi_set_event_handler_cb(wifi_callback);
	} else {
		os_printf("Starting web server\n");

		// Start web server and wait for connections!
		start_web_server();

		// Display the AP setup screen
		eink_draw_unconfigured(wifi_name);
		eink_sleep();
	}
}
