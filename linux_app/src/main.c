#include <stdio.h>
#include <stdlib.h>
#include "audio.h"

int main(void) {
    pulse_audio_t pulse = { 0 };

    if (audio_init(&pulse) != 0) {
        fprintf(stderr, "Failed to init audio\n");
        audio_destroy(&pulse);
        return EXIT_FAILURE;
    }

    audio_destroy(&pulse);

    return EXIT_SUCCESS;
}
