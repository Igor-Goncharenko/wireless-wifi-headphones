#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "discovery.h"
#include "rtp_client.h"

int connect_to_wifi_hp(const char *ip4) {
    rtp_session_t session = { 0 };
    pulse_audio_t pulse = { 0 };

    if (rtp_session_create(&session, ip4, RTP_PORT) != 0) {
        fprintf(stderr, "Failed to init rtp session\n");
        return -1;
    }

    if (audio_init(&pulse, &session) != 0) {
        fprintf(stderr, "Failed to init audio\n");
        audio_destroy(&pulse);
        return -1;
    }

    rtp_session_destroy(&session);

    return 0;
}

int main(void) {
    discovery_server_t server = { 0 };

    headphone_response_t hps[16];

    if (discovery_server_init(&server) != 0) {
        fprintf(stderr, "Failed to init discovery server. errno=%d, strerror=%s\n",
                errno, strerror(errno));
        discovery_server_destroy(&server);
        return EXIT_FAILURE;
    }

    int cnt = discover_headphones(&server, hps, 16);

    if (cnt > 0) {
        for (int i = 0; i < cnt; i++) {
            printf("%d) %s %s %s %s\n", i + 1, hps[i].type, hps[i].model, hps[i].id, hps[i].ip_v4);
        }

        int idx;
        do {
            printf("Choose headphones to connect[1-%d]: \n> ", cnt);
        } while (scanf("%d", &idx) != 1 || idx < 1 || idx > cnt);

        idx--;

        printf("Connecting to %s %s %s %s\n", hps[idx].type, hps[idx].model, hps[idx].id, hps[idx].ip_v4);

        if (connect_to_wifi_hp(hps[idx].ip_v4) != 0) {
            printf("Failed to connect to wifi headphones\n");
            discovery_server_destroy(&server);
            return EXIT_FAILURE;
        }
    }
    else {
        printf("Nothing found\n");
    }

    discovery_server_destroy(&server);

    return EXIT_SUCCESS;
}
