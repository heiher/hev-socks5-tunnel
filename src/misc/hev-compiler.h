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

#if defined(_MSC_VER)
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
#define ALIGN_UP(addr, align)                                                  \
    (((uintptr_t)(addr) + (uintptr_t)(align) - 1) & ~((uintptr_t)(align) - 1))

#define ALIGN_DOWN(addr, align) \
    ((uintptr_t)(addr) & ~((uintptr_t)(align) - 1))
#else
#define ALIGN_UP(addr, align) \
    ((addr + (typeof (addr))align - 1) & ~((typeof (addr))align - 1))

#define ALIGN_DOWN(addr, align) ((addr) & ~((typeof (addr))align - 1))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof (x) / sizeof (x[0]))
#endif

#ifndef EXPORT_SYMBOL
#if defined(_MSC_VER)
#define EXPORT_SYMBOL
#else
#define EXPORT_SYMBOL __attribute__ ((visibility ("default")))
#endif
#endif

#if defined(_MSC_VER)
#define barrier() atomic_signal_fence (memory_order_seq_cst)
#else
#define barrier() __asm__ __volatile__ ("" : : : "memory")
#endif

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
#if defined(_MSC_VER)
        memcpy ((void *)dst, (const void *)src, size);
#else
        __builtin_memcpy ((void *)dst, (const void *)src, size);
#endif
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
#if defined(_MSC_VER)
        memcpy ((void *)dst, (const void *)src, size);
#else
        __builtin_memcpy ((void *)dst, (const void *)src, size);
#endif
        barrier ();
    }
}

#if defined(_MSC_VER)

/*
 * The default association is pointer-sized and only valid for pointers on x64.
 */
#define HEV_ONCE_PTR(x)                                                        \
    _Generic ((x),                                                             \
        _Bool: (volatile _Bool *)&(x),                                         \
        char: (volatile char *)&(x),                                           \
        signed char: (volatile signed char *)&(x),                             \
        unsigned char: (volatile unsigned char *)&(x),                         \
        short: (volatile short *)&(x),                                         \
        unsigned short: (volatile unsigned short *)&(x),                       \
        int: (volatile int *)&(x),                                             \
        unsigned int: (volatile unsigned int *)&(x),                           \
        long: (volatile long *)&(x),                                           \
        unsigned long: (volatile unsigned long *)&(x),                         \
        long long: (volatile long long *)&(x),                                 \
        unsigned long long: (volatile unsigned long long *)&(x),               \
        float: (volatile float *)&(x),                                         \
        double: (volatile double *)&(x),                                       \
        default: (void *volatile *)&(x))

#define READ_ONCE(x) (*HEV_ONCE_PTR (x))
#define WRITE_ONCE(x, val) (*HEV_ONCE_PTR (x) = (val))

#else /* GNU compatible compilers */

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

#endif /* _MSC_VER */

#ifndef container_of
#if defined(_MSC_VER)
#define container_of(ptr, type, member)                     \
    ((type *)(void *)((char *)(ptr) - offsetof (type, member)))
#else
#define container_of(ptr, type, member)               \
    ({                                                \
        void *__mptr = (void *)(ptr);                 \
        ((type *)(__mptr - offsetof (type, member))); \
    })
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __HEV_COMPILER_H__ */
