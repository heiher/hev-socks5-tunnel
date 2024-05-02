/*
 ============================================================================
 Name        : hev-ring-buffer.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Ring buffer
 ============================================================================
 */

#ifndef __HEV_RING_BUFFER_H__
#define __HEV_RING_BUFFER_H__

#include <stdlib.h>
#include <sys/uio.h>
#include <sys/types.h>

typedef struct _HevRingBuffer HevRingBuffer;

struct _HevRingBuffer
{
    size_t rp;
    size_t wp;
    size_t rda_size;
    size_t use_size;
    size_t max_size;

    unsigned char data[0];
};

#define hev_ring_buffer_alloca(s)                     \
    ({                                                \
        HevRingBuffer *__self;                        \
                                                      \
        __self = alloca (sizeof (HevRingBuffer) + s); \
        if (!__self)                                  \
            NULL;                                     \
                                                      \
        __self->rp = 0;                               \
        __self->wp = 0;                               \
        __self->rda_size = 0;                         \
        __self->use_size = 0;                         \
        __self->max_size = s;                         \
                                                      \
        __self;                                       \
    })

size_t hev_ring_buffer_get_max_size (HevRingBuffer *self);
size_t hev_ring_buffer_get_use_size (HevRingBuffer *self);

int hev_ring_buffer_reading (HevRingBuffer *self, struct iovec *iov);
void hev_ring_buffer_read_finish (HevRingBuffer *self, size_t size);
void hev_ring_buffer_read_release (HevRingBuffer *self, size_t size);

int hev_ring_buffer_writing (HevRingBuffer *self, struct iovec *iov);
void hev_ring_buffer_write_finish (HevRingBuffer *self, size_t size);

#endif /* __HEV_RING_BUFFER_H__ */
