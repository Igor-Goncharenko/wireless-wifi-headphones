#include <stdio.h>
#include <stdlib.h>
//#include "audio.h"
#include "discovery.h"

int main(void) {
    //pulse_audio_t pulse = { 0 };
    discovery_server_t server = { 0 };

    headphone_response_t hps[16];

    //if (audio_init(&pulse) != 0) {
    //    fprintf(stderr, "Failed to init audio\n");
    //    audio_destroy(&pulse);
    //    return EXIT_FAILURE;
    //}

    //audio_destroy(&pulse);
    
    discovery_server_init(&server);
    int cnt = discover_headphones(&server, hps, 16);

    for (int i = 0; i < cnt; i++) {
        printf("%d) %s %s %s %s\n", i + 1, hps[i].type, hps[i].model, hps[i].id, hps[i].ip_v4);
    }

    int idx;
    do {
    printf("Choose headphones to connect: \n> ");
    } while (scanf("%d", &idx) != 1 || idx < 1 || idx > cnt);

    idx--;

    printf("Connecting to %s %s %s %s\n", hps[idx].type, hps[idx].model, hps[idx].id, hps[idx].ip_v4);

    return EXIT_SUCCESS;
}
