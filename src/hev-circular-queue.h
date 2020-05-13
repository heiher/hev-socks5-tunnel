/*
 ============================================================================
 Name        : hev-circular-queue.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2020 Everyone.
 Description : Circular Queue
 ============================================================================
 */

#ifndef __HEV_CIRCULAR_QUEUE_H__
#define __HEV_CIRCULAR_QUEUE_H__

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _HevCircularQueue HevCircularQueue;

HevCircularQueue *hev_circular_queue_new (size_t max_size);

HevCircularQueue *hev_circular_queue_ref (HevCircularQueue *self);
void hev_circular_queue_unref (HevCircularQueue *self);

size_t hev_circular_queue_get_max_size (HevCircularQueue *self);
size_t hev_circular_queue_get_use_size (HevCircularQueue *self);

int hev_circular_queue_push (HevCircularQueue *self, void *data);
void *hev_circular_queue_pop (HevCircularQueue *self);
void *hev_circular_queue_peek (HevCircularQueue *self);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_CIRCULAR_QUEUE_H__ */
