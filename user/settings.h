#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

// Global settings, stored in all memories
struct GlobalSettings {
  char      conf_essid[32] __attribute__ ((aligned (4)));  // Wireless ESSID
  char      conf_passw[32] __attribute__ ((aligned (4)));  // WPA/WEP password
  char      conf_hostn[32] __attribute__ ((aligned (4)));  // Server hostname
  char      conf_path [64] __attribute__ ((aligned (4)));  // Server path to the servlet URI
  uint32_t  conf_port;                                     // Server port for the service
  uint32_t  checksum;
};

int ICACHE_FLASH_ATTR check_settings_checksum(uint32_t *checksum, struct GlobalSettings *s);
int ICACHE_FLASH_ATTR recover_settings(struct GlobalSettings *s);
void ICACHE_FLASH_ATTR store_settings(struct GlobalSettings *s);

#endif
