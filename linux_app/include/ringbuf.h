#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

typedef struct {
    uint8_t *buf;
    size_t size;
    size_t read_pos;
    size_t write_pos;
    size_t available;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ringbuf_t;

int ringbuf_init(ringbuf_t *rb, size_t size);

void ringbuf_destroy(ringbuf_t *rb);

size_t ringbuf_write(ringbuf_t *rb, const void *data, const size_t nbytes);

size_t ringbuf_read_block(ringbuf_t *rb, uint8_t *output, const size_t block_size);

#endif /* RINGBUF_H */
