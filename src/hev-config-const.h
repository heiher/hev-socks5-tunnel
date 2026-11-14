/*
 ============================================================================
 Name        : hev-config-const.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2024 hev
 Description : Config Constants
 ============================================================================
 */

#ifndef __HEV_CONFIG_CONST_H__
#define __HEV_CONFIG_CONST_H__

#define MAJOR_VERSION (2)
#define MINOR_VERSION (19)
#define MICRO_VERSION (3)

#define UDP_BUF_SIZE 1500
#define UDP_POOL_SIZE 512
#define UDP_SEND_BATCH 64
#define TUN_READ_BATCH 64
#define COMPACT_PBUF_SIZE 2048
#define TASK_STACK_SIZE 20480
#define SESSION_IDLE_RELEASE_TIME 1000
/* Charged per queued pbuf: its struct, headers and malloc overhead. */
#define PBUF_CHARGE_OVERHEAD 128
/* Sessions that hold buffers without progress for so long are reclaimed. */
#define BUFFER_RECLAIM_IDLE_TIME 2000
/* A session upstream answered is kept for so long to finish its handshake. */
#define SESSION_EVICT_YOUNG_TIME 2000

#endif /* __HEV_CONFIG_CONST_H__ */
