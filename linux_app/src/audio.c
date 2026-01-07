#include "audio.h"
#include "ringbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <syslog.h>
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pulse/volume.h>

#include "config.h"

#define CONTEXT_NAME "WiFi Headphones Output"
#define DEVICE_NAME "WifiHeadphones"
#define DEVICE_DESC "WiFi-Headphones"
#define STREAM_NAME DEVICE_NAME "Monitor"

#if (AUDIO_SAMPLE_SIZE == 1)
# define PULSE_SAMPLE_SIZE PA_SAMPLE_U8
#elif (AUDIO_SAMPLE_SIZE == 2)
# define PULSE_SAMPLE_SIZE PA_SAMPLE_S16LE
#elif (AUDIO_SAMPLE_SIZE == 3)
# define PULSE_SAMPLE_SIZE PA_SAMPLE_S24LE
#elif (AUDIO_SAMPLE_SIZE == 4)
# define PULSE_SAMPLE_SIZE PA_SAMPLE_S32LE
#else
# error "Incorrect sample size"
#endif /* AUDIO_SAMPLE_SIZE */

static void stream_read_cb(pa_stream *s, size_t length, void *userdata) {
    pulse_audio_t *pulse = (pulse_audio_t*) userdata;
    size_t actual_len = length;
    const void *data;

    if (pa_stream_peek(s, &data, &actual_len) < 0) return;

    if (data != NULL && actual_len > 0) {
        if (ringbuf_write(&pulse->audio_buf, data, actual_len) < actual_len) {
            //fprintf(stderr, "WARNING: Audio buffer overflow, dropping %zu bytes\n", actual_len);
        }
    }

    pa_stream_drop(s);
}

static void context_state_cb(pa_context *c, void *userdata) {
    pulse_audio_t *pulse = (pulse_audio_t*) userdata;

    if (pa_context_get_state(c) == PA_CONTEXT_READY) {
        pa_threaded_mainloop_signal(pulse->mainloop, 0);
    }
}

static void stream_state_cb(pa_stream *s, void *userdata) {
    //pulse_audio_t *pulse = (pulse_audio_t*) userdata;
}

static void load_module_cb(pa_context *c, uint32_t idx, void *userdata) {
    pulse_audio_t *pulse = (pulse_audio_t*) userdata;

    if (idx == PA_INVALID_INDEX) {
        syslog(LOG_ERR, "Module failed to load");
        pulse->module_loaded = -1;
    } else {
        pulse->module_idx = idx;
        pulse->module_loaded = 1;
        syslog(LOG_INFO, "Module loaded successfully, idx=%" PRIu32, idx);
    }

    pa_threaded_mainloop_signal(pulse->mainloop, 0);
}

static int create_virtual_sink(pulse_audio_t *pulse) {
    pa_threaded_mainloop_lock(pulse->mainloop);

    pulse->module_loaded = 0;
    
    const char *args = "sink_name=" DEVICE_NAME " sink_properties=device.description=" DEVICE_DESC "";
    
    pa_operation *op = pa_context_load_module(pulse->context, "module-null-sink", args, 
                                             load_module_cb, pulse);
    
    if (!op) {
        syslog(LOG_ERR, "Failed to create load module operation");
        pa_threaded_mainloop_unlock(pulse->mainloop);
        return -1;
    }

    while (!pulse->module_loaded) {
        pa_threaded_mainloop_wait(pulse->mainloop);
    }
    
    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(pulse->mainloop);

    if (pulse->module_loaded == -1) {
        syslog(LOG_ERR, "Module loading failed");
        return -1;
    }

    usleep(100000);
    syslog(LOG_INFO, "Virtual sink created successfully");

    return 0;
}

static void remove_virtual_sink(pulse_audio_t *pulse) {
    if (pulse->module_loaded) {
        pa_threaded_mainloop_lock(pulse->mainloop);

        pa_operation *op = pa_context_unload_module(pulse->context, pulse->module_idx, NULL, NULL);

        if (op) {
            pa_operation_unref(op);
        }

        pa_threaded_mainloop_unlock(pulse->mainloop);
        usleep(100000);
        syslog(LOG_INFO, "Virtual sink removed");
    }
}

static int create_monitor_stream(pulse_audio_t *pulse) {
    pa_sample_spec sample_spec = {
        .format = PULSE_SAMPLE_SIZE,
        .rate = AUDIO_SAMPLE_RATE,
        .channels = AUDIO_CHANNELS,
    };
    
    pulse->stream = pa_stream_new(pulse->context, STREAM_NAME, 
                                 &sample_spec, NULL);
    
    pa_stream_set_state_callback(pulse->stream, stream_state_cb, pulse);
    pa_stream_set_read_callback(pulse->stream, stream_read_cb, pulse);
    
    if (pa_stream_connect_record(pulse->stream, DEVICE_NAME ".monitor", 
                                NULL, PA_STREAM_PEAK_DETECT) < 0) {
        return -1;
    }
    
    return 0;
}

static int pulse_audio_init(pulse_audio_t *pulse) {
    pulse->mainloop = pa_threaded_mainloop_new();
    pa_mainloop_api *api = pa_threaded_mainloop_get_api(pulse->mainloop);
    
    pulse->context = pa_context_new(api, CONTEXT_NAME);
    pa_context_set_state_callback(pulse->context, context_state_cb, pulse);
    
    if (pa_context_connect(pulse->context, NULL, 0, NULL) < 0) {
        return -1;
    }
    
    pa_threaded_mainloop_lock(pulse->mainloop);
    pa_threaded_mainloop_start(pulse->mainloop);
    
    while (pa_context_get_state(pulse->context) != PA_CONTEXT_READY) {
        pa_threaded_mainloop_wait(pulse->mainloop);
    }
    
    pa_threaded_mainloop_unlock(pulse->mainloop);
    return 0;
}

void audio_destroy(pulse_audio_t *pulse) {
    if (pulse->stream) {
        pa_stream_disconnect(pulse->stream);
        pa_stream_unref(pulse->stream);
        pulse->stream = NULL;
    }
    remove_virtual_sink(pulse);

    if (pulse->context) {
        pa_context_disconnect(pulse->context);
        pa_context_unref(pulse->context);
        pulse->context = NULL;
    }
    if (pulse->mainloop) {
        pa_threaded_mainloop_stop(pulse->mainloop);
        pa_threaded_mainloop_free(pulse->mainloop);
        pulse->mainloop = NULL;
    }

    ringbuf_destroy(&pulse->audio_buf);
}

int audio_init(pulse_audio_t *pulse) {
    if (pulse_audio_init(pulse) != 0) {
        syslog(LOG_ERR, "Failed to init PulseAudio");
        goto error;
    }
    if (create_virtual_sink(pulse) != 0) {
        syslog(LOG_ERR, "Failed to create virtual sink");
        goto error;
    }
    if (create_monitor_stream(pulse) != 0) {
        syslog(LOG_ERR, "Failed to create monitor stream");
        goto error;
    }
    if (ringbuf_init(&pulse->audio_buf, AUDIO_BUF_SIZE) != 0) {
        syslog(LOG_ERR, "Failed to init audio buffer");
        goto error;
    }
    return 0;

error:
    audio_destroy(pulse);
    return -1;
}
