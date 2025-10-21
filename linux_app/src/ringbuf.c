#include "ringbuf.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

 #define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

int ringbuf_init(ringbuf_t *rb, size_t size) {
    if ((rb->buf = malloc(size)) == NULL) {
        syslog(LOG_ERR, "Failed to allocate memory for ring buffer");
        return -1;
    }

    rb->size = size;
    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->available = size;

    if (pthread_mutex_init(&rb->mutex, NULL) != 0) {
        syslog(LOG_ERR, "Failed to init pthread mutex: errno=%d, strerror=\"%s\"",
                errno, strerror(errno));
        free(rb->buf);
        return -1;
    }

    if (pthread_cond_init(&rb->cond, NULL) != 0) {
        syslog(LOG_ERR, "Failed to init pthread cond: errno=%d, strerror=\"%s\"",
                errno, strerror(errno));
        pthread_mutex_destroy(&rb->mutex);
        free(rb->buf);
        return -1;
    }

    return 0;
}

void ringbuf_destroy(ringbuf_t *rb) {
    pthread_mutex_lock(&rb->mutex);
    if (rb->buf != NULL) {
        free(rb->buf);
    }
    pthread_mutex_unlock(&rb->mutex);
    pthread_mutex_destroy(&rb->mutex);
    pthread_cond_destroy(&rb->cond);
}

size_t ringbuf_write(ringbuf_t *rb, const void *data, const size_t nbytes) {
    pthread_mutex_lock(&rb->mutex);

    size_t written = 0;
    while (written < nbytes && rb->available < rb->size) {
        size_t to_write = min(nbytes - written, rb->size - rb->write_pos);
        to_write = min(to_write, rb->size - rb->available);
        
        memcpy(rb->buf + rb->write_pos, data + written, to_write);
        written += to_write;
        rb->write_pos = (rb->write_pos + to_write) % rb->size;
        rb->available += to_write;
    }

    pthread_cond_signal(&rb->cond);
    pthread_mutex_unlock(&rb->mutex);
    return written;
}

size_t ringbuf_read_block(ringbuf_t *rb, uint8_t *output, const size_t block_size) {
    pthread_mutex_lock(&rb->mutex);
    
    while (rb->available < block_size) {
        pthread_cond_wait(&rb->cond, &rb->mutex);
    }
    
    size_t to_read = block_size;
    if (rb->read_pos + to_read > rb->size) {
        size_t first_part = rb->size - rb->read_pos;
        memcpy(output, rb->buf + rb->read_pos, first_part);
        memcpy(output + first_part, rb->buf, to_read - first_part);
    } else {
        memcpy(output, rb->buf + rb->read_pos, to_read);
    }
    
    rb->read_pos = (rb->read_pos + to_read) % rb->size;
    rb->available -= to_read;
    
    pthread_mutex_unlock(&rb->mutex);
    return to_read;
}
