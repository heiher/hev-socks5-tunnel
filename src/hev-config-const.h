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
#define MICRO_VERSION (1)

#define UDP_BUF_SIZE 1500
#define UDP_POOL_SIZE 512
#define UDP_SEND_BATCH 64
#define TUN_READ_BATCH 64
#define COMPACT_PBUF_SIZE 2048
#define TASK_STACK_SIZE 20480
#define SESSION_IDLE_RELEASE_TIME 1000

#endif /* __HEV_CONFIG_CONST_H__ */
