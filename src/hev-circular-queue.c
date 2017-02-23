/*
 ============================================================================
 Name        : hev-circular-queue.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 everyone.
 Description : Circular queue
 ============================================================================
 */

#include "hev-memory-allocator.h"

#include "hev-circular-queue.h"

struct _HevCircularQueue
{
    size_t rp;
    size_t use_size;
    size_t max_size;
    unsigned int ref_count;

    void *data[0];
};

HevCircularQueue *
hev_circular_queue_new (size_t max_size)
{
    HevCircularQueue *self;

    self = hev_malloc (sizeof (HevCircularQueue) + sizeof (void *) * max_size);
    if (!self)
        return NULL;

    self->rp = 0;
    self->use_size = 0;
    self->max_size = max_size;
    self->ref_count = 1;

    return self;
}

HevCircularQueue *
hev_circular_queue_ref (HevCircularQueue *self)
{
    self->ref_count++;

    return self;
}

void
hev_circular_queue_unref (HevCircularQueue *self)
{
    self->ref_count--;
    if (self->ref_count)
        return;

    hev_free (self);
}

size_t
hev_circular_queue_get_max_size (HevCircularQueue *self)
{
    return self->max_size;
}

size_t
hev_circular_queue_get_use_size (HevCircularQueue *self)
{
    return self->use_size;
}

int
hev_circular_queue_push (HevCircularQueue *self, void *data)
{
    size_t wp;

    if (self->use_size == self->max_size)
        return -1;

    wp = (self->rp + self->use_size) % self->max_size;
    self->data[wp] = data;
    self->use_size++;

    return 0;
}

void *
hev_circular_queue_pop (HevCircularQueue *self)
{
    void *data;

    if (0 == self->use_size)
        return NULL;

    data = self->data[self->rp];
    self->rp = (self->rp + 1) % self->max_size;
    self->use_size--;

    return data;
}

void *
hev_circular_queue_peek (HevCircularQueue *self)
{
    if (0 == self->use_size)
        return NULL;

    return self->data[self->rp];
}
