#include <ets_sys.h>
#include <osapi.h>
#include <gpio.h>
#include <os_type.h>
#include <ip_addr.h>
#include <espconn.h>
#include <user_interface.h>
#include "user_config.h"
#include "uart.h"
#include "settings.h"

int ICACHE_FLASH_ATTR check_settings_checksum(uint32_t *checksum, struct GlobalSettings *s) {
	uint32_t * d = (uint32_t*)s;
	uint32_t sum = 0x2BADFACE; // Must not be zero
	int i;
	for(i = 0; i < (sizeof(*s) - sizeof(uint32_t))/sizeof(uint32_t); i++) {
		sum += d[i];
	}
	if (checksum) *checksum = sum;
	return sum == s->checksum;
}


int ICACHE_FLASH_ATTR recover_settings(struct GlobalSettings *s) {
	// Try to recover data from the RTC memory first
	system_rtc_mem_read(64, s, sizeof(*s));
	if (check_settings_checksum(0, s))
		return 1;

	// Then read flash
	spi_flash_read(0x3C000, (uint32 *)s, sizeof(*s));
	if (check_settings_checksum(0, s))
		return 1;

	// Then try shadow flash too
	spi_flash_read(0x3D000, (uint32 *)s, sizeof(*s));
	if (check_settings_checksum(0, s))
		return 1;

	// Dump the RTC memory here
	uint32_t w = 0;
	int i;
	os_printf("Failed to restore settings, RTC dump:\n");
	for (i = 64; i < 192; i++) {
		if ((i % 16) == 0) os_printf("\n%04x ", i);
		system_rtc_mem_read(i,  &w, 4);
		os_printf("%08x ", w);
	}
	os_printf("\n");
	return 0; // Done!
}

void ICACHE_FLASH_ATTR store_settings(struct GlobalSettings *s) {
	// First write the config and then the magic
	os_printf("Store %s %s\n", s->conf_essid, s->conf_passw);
	// First in flash
	spi_flash_erase_sector(0x3C);
	spi_flash_write(0x3C000, (uint32_t*)s, sizeof(*s));
	spi_flash_erase_sector(0x3D);
	spi_flash_write(0x3D000, (uint32_t*)s, sizeof(*s));
	// Then in RTC mem
	system_rtc_mem_write(64, s, sizeof(*s));
}
