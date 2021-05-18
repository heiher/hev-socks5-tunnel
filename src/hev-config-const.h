/*
 ============================================================================
 Name        : hev-config-const.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2021 hev
 Description : Config Constants
 ============================================================================
 */

#ifndef __HEV_CONFIG_CONST_H__
#define __HEV_CONFIG_CONST_H__

#define MAJOR_VERSION (1)
#define MINOR_VERSION (2)
#define MICRO_VERSION (1)

static const int TCP_BUF_SIZE = 8192;
static const int UDP_BUF_SIZE = 1500;
static const int UDP_POOL_SIZE = 512;

static const int IO_TIMEOUT = 60000;
static const int CONNECT_TIMEOUT = 3000;

static const int TASK_STACK_SIZE = 20480;

#endif /* __HEV_CONFIG_CONST_H__ */
