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
#if defined(_MSC_VER)
#include <malloc.h>
#endif
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

#if defined(_MSC_VER)
/*
 * MSVC C mode has no statement expressions. Provide an initializer
 * function instead of the GNU alloca macro.
 */
static inline HevRingBuffer *
hev_ring_buffer_alloca_init (void *mem, size_t size)
{
    HevRingBuffer *self = (HevRingBuffer *)mem;

    self->rp = 0;
    self->wp = 0;
    self->rda_size = 0;
    self->use_size = 0;
    self->max_size = size;

    return self;
}

#define hev_ring_buffer_alloca(s)                           \
    hev_ring_buffer_alloca_init (                           \
        _alloca (sizeof (HevRingBuffer) + (size_t)(s)), (s))
#else
#define hev_ring_buffer_alloca(s)                     \
    ({                                                \
        HevRingBuffer *__self;                        \
        __self = alloca (sizeof (HevRingBuffer) + s); \
        __self->rp = 0;                               \
        __self->wp = 0;                               \
        __self->rda_size = 0;                         \
        __self->use_size = 0;                         \
        __self->max_size = s;                         \
        __self;                                       \
    })
#endif

size_t hev_ring_buffer_get_max_size (HevRingBuffer *self);
size_t hev_ring_buffer_get_use_size (HevRingBuffer *self);

int hev_ring_buffer_reading (HevRingBuffer *self, struct iovec *iov);
void hev_ring_buffer_read_finish (HevRingBuffer *self, size_t size);
void hev_ring_buffer_read_release (HevRingBuffer *self, size_t size);

int hev_ring_buffer_writing (HevRingBuffer *self, struct iovec *iov);
void hev_ring_buffer_write_finish (HevRingBuffer *self, size_t size);

#endif /* __HEV_RING_BUFFER_H__ */
