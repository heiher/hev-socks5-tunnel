/*
 ============================================================================
 Name        : hev-list.c
 Authors     : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Double-linked List
 ============================================================================
 */

#include <stddef.h>

#include "hev-list.h"

void
hev_list_add_tail (HevList *self, HevListNode *new_)
{
    new_->prev = self->tail;
    new_->next = NULL;

    if (self->tail)
        self->tail->next = new_;
    else
        self->head = new_;
    self->tail = new_;
}

void
hev_list_del (HevList *self, HevListNode *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        self->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        self->tail = node->prev;
}
