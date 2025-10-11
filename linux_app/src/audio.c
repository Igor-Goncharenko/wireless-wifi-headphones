#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pulse/volume.h>

#include "rtp_client.h"

#define CONTEXT_NAME "WiFi Headphones Output"
#define DEVICE_NAME "WifiHeadphones"
#define DEVICE_DESC "WiFi-Headphones"
#define STREAM_NAME DEVICE_NAME "Monitor"

static void stream_read_cb(pa_stream *s, size_t length, void *userdata) {
    pulse_audio_t *pulse = (pulse_audio_t*) userdata;
    const void *data;

    if (pa_stream_peek(s, &data, &length) < 0) return;

    if (data != NULL && length > 0) {
        printf("Audio captured: %zu bytes \n", length);

        int marker = (pulse->packet_count % 100 == 0);  // Marker every 100 packets
        int packets;
        if ((packets = rtp_send_packet(pulse->session, data, length, marker)) != 0) {
            pulse->packet_count += packets;
            if (pulse->packet_count % 100 == 0) {
                printf("Sent %d packets\n", pulse->packet_count);
            }
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
        fprintf(stderr, "ERROR: Module failed to load\n");
        pulse->module_loaded = -1;
    } else {
        pulse->module_idx = idx;
        pulse->module_loaded = 1;
        printf("Module loaded successfully, idx=%" PRIu32 "\n", idx);
    }

    pa_threaded_mainloop_signal(pulse->mainloop, 0);
}

static int create_virtual_sink(pulse_audio_t *pulse) {
    pa_threaded_mainloop_lock(pulse->mainloop);

    pulse->module_loaded = 0;
    
    const char *args = "sink_name=" DEVICE_NAME " sink_properties=device.description=" DEVICE_DESC "";
    printf("%s\n", args);
    
    pa_operation *op = pa_context_load_module(pulse->context, "module-null-sink", args, 
                                             load_module_cb, pulse);
    
    if (!op) {
        fprintf(stderr, "Failed to create load module operation\n");
        pa_threaded_mainloop_unlock(pulse->mainloop);
        return -1;
    }

    while (!pulse->module_loaded) {
        pa_threaded_mainloop_wait(pulse->mainloop);
    }
    
    pa_operation_unref(op);
    pa_threaded_mainloop_unlock(pulse->mainloop);

    if (pulse->module_loaded == -1) {
        fprintf(stderr, "Module loading failed\n");
        return -1;
    }

    usleep(100000);
    printf("Virtual sink created successfully\n");

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
        printf("Virtual sink removed\n");
    }
}

static int create_monitor_stream(pulse_audio_t *pulse) {
    pa_sample_spec sample_spec = {
        .format = PA_SAMPLE_S16LE,
        .rate = 44100,
        .channels = 2
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

int audio_init(pulse_audio_t *pulse, rtp_session_t *session) {
    pulse->session = session;

    if (pulse_audio_init(pulse) != 0) {
        printf("Failed to init PulseAudio\n");
        return -1;
    }
    
    if (create_virtual_sink(pulse) != 0) {
        printf("Failed to create virtual sink\n");
        return -1;
    }
    
    if (create_monitor_stream(pulse) != 0) {
        printf("Failed to create monitor stream\n");
        return -1;
    }
    
    printf("Virtual output device created via PulseAudio API!\n");
    printf("Check: pactl list sinks short | grep Wifi\n");
    printf("Press Enter to stop...\n");
    
    char buf[4];
    scanf("%s", buf);
    
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
}
