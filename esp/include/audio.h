#ifndef AUDIO_H
#define AUDIO_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/i2s_std.h"

#include "config.h"

#define RINGBUFFER_SIZE (64 * 1024)

#if (AUDIO_CHANNELS == 1)
# define I2S_CHANNEL I2S_SLOT_MODE_MONO
#elif (AUDIO_CHANNELS == 2)
# define I2S_CHANNEL I2S_SLOT_MODE_STEREO
#else
# error "Incorrect number of audio channels"
#endif /* AUDIO_CHANNELS */

#if (AUDIO_SAMPLE_SIZE == 1)
# define I2S_SAMPLE_SIZE I2S_DATA_BIT_WIDTH_8BIT
#elif (AUDIO_SAMPLE_SIZE == 2)
# define I2S_SAMPLE_SIZE I2S_DATA_BIT_WIDTH_16BIT
#elif (AUDIO_SAMPLE_SIZE == 3)
# define I2S_SAMPLE_SIZE I2S_DATA_BIT_WIDTH_24BIT
#elif (AUDIO_SAMPLE_SIZE == 4)
# define I2S_SAMPLE_SIZE I2S_DATA_BIT_WIDTH_32BIT
#else
# error "Incorrect sample size configuration"
#endif /* AUDIO_SAMPLE_SIZE */

typedef struct {
    RingbufHandle_t rb;
    i2s_chan_handle_t i2s;
} audio_t;

int audio_init(void);

const RingbufHandle_t *get_rb_ptr(void);

void audio_play_mgr(void *arg);

#endif /* AUDIO_H */
