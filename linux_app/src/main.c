#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "audio.h"
#include "discovery.h"
#include "rtp_client.h"

bool is_running = true;

void sigint_hndl(int sig) {
    printf("SIGINT\n");
    is_running = false;
}

int choose_headphones(const headphone_response_t *hps, const int n) {
    for (int i = 0; i < n; i++) {
        printf("%d) %s %s %s %s\n", i + 1, hps[i].type, hps[i].model, hps[i].id, hps[i].ip_v4);
    }

    int idx;
    do {
        printf("Choose headphones to connect[1-%d]: \n> ", n);
    } while (scanf("%d", &idx) != 1 || idx < 1 || idx > n);

    idx--;

    return idx;
}

int connect_to_wifi_hp(const char *ip4) {
    rtp_session_t session = { 0 };
    pulse_audio_t pulse = { 0 };

    if (rtp_session_create(&session, ip4, RTP_PORT) != 0) {
        fprintf(stderr, "Failed to init rtp session\n");
        return -1;
    }

    if (audio_init(&pulse) != 0) {
        fprintf(stderr, "Failed to init audio\n");
        audio_destroy(&pulse);
        return -1;
    }

    while (is_running) {
        sleep(1);
    }

    rtp_session_destroy(&session);
    audio_destroy(&pulse);

    return 0;
}

int main(void) {
    signal(SIGINT, sigint_hndl);

    discovery_server_t server = { 0 };
    headphone_response_t hps[16];

    if (discovery_server_init(&server) != 0) {
        fprintf(stderr, "Failed to init discovery server. errno=%d, strerror=%s\n",
                errno, strerror(errno));
        discovery_server_destroy(&server);
        return EXIT_FAILURE;
    }

    int cnt = discover_headphones(&server, hps, 16);

    discovery_server_destroy(&server);

    if (cnt > 0) {
        int conn = choose_headphones(hps, cnt);
        printf("Connecting to %s %s %s %s\n", hps[conn].type, hps[conn].model, hps[conn].id, 
                hps[conn].ip_v4);

        if (connect_to_wifi_hp(hps[conn].ip_v4) != 0) {
            printf("Failed to connect to wifi headphones\n");
            return EXIT_FAILURE;
        }

    }
    else {
        printf("Nothing found\n");
    }

    return EXIT_SUCCESS;
}
