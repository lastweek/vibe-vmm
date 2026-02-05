#ifndef VIBE_VMM_UTILS_H
#define VIBE_VMM_UTILS_H

#include <stddef.h>
#include <stdint.h>

/* Container of macro */
#define container_of(ptr, type, member) ({                  \
    const typeof(((type *)0)->member) *__mptr = (ptr);     \
    (type *)((char *)__mptr - offsetof(type, member));     \
})

/* Array size macro */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Bit manipulation */
#define BIT(x) (1U << (x))

/* Align macros */
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)

/* Page size (4KB) */
#define PAGE_SHIFT  12
#define PAGE_SIZE  (1U << PAGE_SHIFT)
#define PAGE_MASK  (~(PAGE_SIZE - 1))

/* Page alignment macros */
#define PAGE_ALIGN_UP(x)    ALIGN_UP(x, PAGE_SIZE)
#define PAGE_ALIGN_DOWN(x)  ALIGN_DOWN(x, PAGE_SIZE)

/* Minimum/Maximum */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/* String length of array */
#define STRLEN(x) (sizeof(x) - 1)

/* Likely/unlikely branch hints */
#if __GNUC__ >= 3
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

/* Attribute macros */
#define UNUSED __attribute__((unused))
#define PACKED  __attribute__((packed))
#define ALIGN(x) __attribute__((aligned(x)))

/* Logging functions */
extern int log_level;

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

#define log_error(fmt, ...)                                            \
    do { if (log_level >= LOG_LEVEL_ERROR) fprintf(stderr, "[ERROR] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while (0)

#define log_warn(fmt, ...)                                             \
    do { if (log_level >= LOG_LEVEL_WARN) fprintf(stderr, "[WARN] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while (0)

#define log_info(fmt, ...)                                             \
    do { if (log_level >= LOG_LEVEL_INFO) fprintf(stderr, "[INFO] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while (0)

#define log_debug(fmt, ...)                                            \
    do { if (log_level >= LOG_LEVEL_DEBUG) fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while (0)

/* Panic function */
#define panic(fmt, ...)                                                \
    do {                                                               \
        fprintf(stderr, "[PANIC] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        abort();                                                       \
    } while (0)

#endif /* VIBE_VMM_UTILS_H */
