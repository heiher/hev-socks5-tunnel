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

#include <sys/uio.h>
#include <sys/types.h>

typedef struct _HevRingBuffer HevRingBuffer;

HevRingBuffer *hev_ring_buffer_new (size_t max_size);
void hev_ring_buffer_destroy (HevRingBuffer *self);

size_t hev_ring_buffer_get_max_size (HevRingBuffer *self);
size_t hev_ring_buffer_get_use_size (HevRingBuffer *self);

int hev_ring_buffer_reading (HevRingBuffer *self, struct iovec *iov);
void hev_ring_buffer_read_finish (HevRingBuffer *self, size_t size);
void hev_ring_buffer_read_release (HevRingBuffer *self, size_t size);

int hev_ring_buffer_writing (HevRingBuffer *self, struct iovec *iov);
void hev_ring_buffer_write_finish (HevRingBuffer *self, size_t size);

#endif /* __HEV_RING_BUFFER_H__ */
