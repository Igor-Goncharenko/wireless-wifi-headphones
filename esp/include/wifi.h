#ifndef WIFI_H
#define WIFI_H

void wifi_init_sta(void);

const char* wifi_get_ip4_str(void);

void shutdown_wifi_before_restart(void);

#endif /* WIFI_H */
