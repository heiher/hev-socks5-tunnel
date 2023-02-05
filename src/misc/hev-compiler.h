/*
 ============================================================================
 Name        : hev-compiler.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Compiler
 ============================================================================
 */

#ifndef __HEV_COMPILER_H__
#define __HEV_COMPILER_H__

#ifdef __cplusplus
extern "C" {
#endif

#define ALIGN_UP(addr, align) \
    ((addr + (typeof (addr))align - 1) & ~((typeof (addr))align - 1))

#define ALIGN_DOWN(addr, align) ((addr) & ~((typeof (addr))align - 1))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof (x) / sizeof (x[0]))
#endif

#ifndef EXPORT_SYMBOL
#define EXPORT_SYMBOL __attribute__ ((visibility ("default")))
#endif

#define barrier() __asm__ __volatile__ ("" : : : "memory")

static inline void
__read_once_size (void *dst, const volatile void *src, int size)
{
    switch (size) {
    case sizeof (char):
        *(char *)dst = *(volatile char *)src;
        break;
    case sizeof (short):
        *(short *)dst = *(volatile short *)src;
        break;
    case sizeof (int):
        *(int *)dst = *(volatile int *)src;
        break;
    default:
        barrier ();
        __builtin_memcpy ((void *)dst, (const void *)src, size);
        barrier ();
    }
}

static inline void
__write_once_size (volatile void *dst, const void *src, int size)
{
    switch (size) {
    case sizeof (char):
        *(volatile char *)dst = *(char *)src;
        break;
    case sizeof (short):
        *(volatile short *)dst = *(short *)src;
        break;
    case sizeof (int):
        *(volatile int *)dst = *(int *)src;
        break;
    default:
        barrier ();
        __builtin_memcpy ((void *)dst, (const void *)src, size);
        barrier ();
    }
}

#define READ_ONCE(x)                                  \
    ({                                                \
        union                                         \
        {                                             \
            typeof (x) __val;                         \
            char __c[1];                              \
        } __u;                                        \
        __read_once_size (__u.__c, &(x), sizeof (x)); \
        __u.__val;                                    \
    })

#define WRITE_ONCE(x, val)                             \
    ({                                                 \
        union                                          \
        {                                              \
            typeof (x) __val;                          \
            char __c[1];                               \
        } __u = { .__val = (typeof (x))(val) };        \
        __write_once_size (&(x), __u.__c, sizeof (x)); \
        __u.__val;                                     \
    })

#ifndef container_of
#define container_of(ptr, type, member)               \
    ({                                                \
        void *__mptr = (void *)(ptr);                 \
        ((type *)(__mptr - offsetof (type, member))); \
    })
#endif

#ifdef __cplusplus
}
#endif

#endif /* __HEV_COMPILER_H__ */
