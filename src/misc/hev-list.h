/*
 ============================================================================
 Name        : hev-list.h
 Authors     : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Double-linked List
 ============================================================================
 */

#ifndef __HEV_LIST_H__
#define __HEV_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _HevList HevList;
typedef struct _HevListNode HevListNode;

struct _HevList
{
    HevListNode *head;
    HevListNode *tail;
};

struct _HevListNode
{
    HevListNode *next;
    HevListNode *prev;
};

static inline HevListNode *
hev_list_node_prev (HevListNode *node)
{
    return node->prev;
}

static inline HevListNode *
hev_list_node_next (HevListNode *node)
{
    return node->next;
}

static inline HevListNode *
hev_list_first (HevList *self)
{
    return self->head;
}

static inline HevListNode *
hev_list_last (HevList *self)
{
    return self->tail;
}

void hev_list_add_tail (HevList *self, HevListNode *new_);
void hev_list_del (HevList *self, HevListNode *node);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_LIST_H__ */
