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
        printf("%s %s %s %s\n", hps[i].type, hps[i].model, hps[i].id, hps[i].ip);
    }

    return EXIT_SUCCESS;
}
