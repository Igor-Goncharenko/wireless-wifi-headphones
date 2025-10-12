#ifndef AUDIO_H
#define AUDIO_H

#include <inttypes.h>
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pulse/volume.h>

#include "ringbuf.h"

#define AUDIO_BUF_SIZE (1024 * 256)

typedef struct {
    pa_threaded_mainloop *mainloop;
    pa_context *context;
    pa_stream *stream;
    uint32_t module_idx;
    int module_loaded;
    ringbuf_t audio_buf;
} pulse_audio_t;

int audio_init(pulse_audio_t *pulse);

void audio_destroy(pulse_audio_t *pulse);

#endif /* AUDIO_H */
