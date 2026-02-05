#ifndef VIBE_VMM_MM_H
#define VIBE_VMM_MM_H

#include <stdint.h>
#include <stddef.h>

/* Memory region flags */
#define MM_FLAG_READABLE   (1 << 0)
#define MM_FLAG_WRITABLE   (1 << 1)
#define MM_FLAG_EXECUTABLE (1 << 2)
#define MM_FLAG_LOG_DIRTY  (1 << 3)

/* Memory slot */
struct mm_slot {
    uint64_t gpa;         /* Guest physical address */
    void     *hva;        /* Host virtual address */
    uint64_t size;        /* Size in bytes */
    uint32_t slot_id;     /* Hypervisor slot ID */
    uint64_t flags;       /* Flags */
};

/* Memory context */
struct mm_ctx {
    struct mm_slot slots[32];  /* Maximum 32 slots */
    int num_slots;
    uint64_t total_size;
};

/* Initialize memory context */
struct mm_ctx* mm_create(void);
void mm_destroy(struct mm_ctx *mm);

/* Allocate guest memory */
void* mm_alloc_guest_mem(uint64_t size);

/* Free guest memory */
void mm_free_guest_mem(void *ptr, uint64_t size);

/* Add memory slot to context */
int mm_add_slot(struct mm_ctx *mm, uint64_t gpa, void *hva, uint64_t size, uint64_t flags);

/* Translate GPA to HVA */
void* mm_gpa_to_hva(struct mm_ctx *mm, uint64_t gpa, uint64_t size);

/* Find slot at GPA */
struct mm_slot* mm_find_slot(struct mm_ctx *mm, uint64_t gpa);

/* Copy to/from guest memory */
int mm_write_gpa(struct mm_ctx *mm, uint64_t gpa, const void *data, uint64_t size);
int mm_read_gpa(struct mm_ctx *mm, uint64_t gpa, void *data, uint64_t size);

/* Helper: write 8/16/32/64 bit value to GPA */
static inline int mm_write8(struct mm_ctx *mm, uint64_t gpa, uint8_t val) {
    return mm_write_gpa(mm, gpa, &val, 1);
}

static inline int mm_write16(struct mm_ctx *mm, uint64_t gpa, uint16_t val) {
    return mm_write_gpa(mm, gpa, &val, 2);
}

static inline int mm_write32(struct mm_ctx *mm, uint64_t gpa, uint32_t val) {
    return mm_write_gpa(mm, gpa, &val, 4);
}

static inline int mm_write64(struct mm_ctx *mm, uint64_t gpa, uint64_t val) {
    return mm_write_gpa(mm, gpa, &val, 8);
}

/* Helper: read 8/16/32/64 bit value from GPA */
static inline int mm_read8(struct mm_ctx *mm, uint64_t gpa, uint8_t *val) {
    return mm_read_gpa(mm, gpa, val, 1);
}

static inline int mm_read16(struct mm_ctx *mm, uint64_t gpa, uint16_t *val) {
    return mm_read_gpa(mm, gpa, val, 2);
}

static inline int mm_read32(struct mm_ctx *mm, uint64_t gpa, uint32_t *val) {
    return mm_read_gpa(mm, gpa, val, 4);
}

static inline int mm_read64(struct mm_ctx *mm, uint64_t gpa, uint64_t *val) {
    return mm_read_gpa(mm, gpa, val, 8);
}

#endif /* VIBE_VMM_MM_H */
