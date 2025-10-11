#ifndef AUDIO_H
#define AUDIO_H

#include <inttypes.h>
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pulse/volume.h>

#include "rtp_client.h"

typedef struct {
    pa_threaded_mainloop *mainloop;
    pa_context *context;
    pa_stream *stream;
    uint32_t module_idx;
    int module_loaded;
    rtp_session_t *session;
    uint32_t packet_count;
} pulse_audio_t;

int audio_init(pulse_audio_t *pulse, rtp_session_t *session);

void audio_destroy(pulse_audio_t *pulse);

#endif /* AUDIO_H */
