/*
 * Memory management
 */

#include "mm.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * Create a memory context
 */
struct mm_ctx* mm_create(void)
{
    struct mm_ctx *mm;

    mm = calloc(1, sizeof(*mm));
    if (!mm)
        return NULL;

    mm->num_slots = 0;
    mm->total_size = 0;

    return mm;
}

/*
 * Destroy a memory context
 */
void mm_destroy(struct mm_ctx *mm)
{
    int i;

    if (!mm)
        return;

    /* Free all slots */
    for (i = 0; i < mm->num_slots; i++) {
        if (mm->slots[i].hva)
            mm_free_guest_mem(mm->slots[i].hva, mm->slots[i].size);
    }

    free(mm);
}

/*
 * Allocate guest memory
 */
void* mm_alloc_guest_mem(uint64_t size)
{
    void *ptr;

    /* Align to page size */
    size = PAGE_ALIGN_UP(size);

    /* Using calloc instead of mmap for simplicity */
    /* For production, consider using mmap with MAP_ANONYMOUS | MAP_PRIVATE */
    ptr = calloc(1, size);
    if (!ptr) {
        log_error("Failed to allocate guest memory (%ld MB)", size / (1024 * 1024));
        return NULL;
    }

    log_debug("Allocated guest memory: %p (%ld MB)", ptr, size / (1024 * 1024));
    return ptr;
}

/*
 * Free guest memory
 */
void mm_free_guest_mem(void *ptr, uint64_t size)
{
    if (!ptr)
        return;

    free(ptr);
    log_debug("Freed guest memory: %p", ptr);
}

/*
 * Add a memory slot to context
 */
int mm_add_slot(struct mm_ctx *mm, uint64_t gpa, void *hva, uint64_t size, uint64_t flags)
{
    struct mm_slot *slot;

    if (mm->num_slots >= 32) {
        log_error("Too many memory slots");
        return -1;
    }

    slot = &mm->slots[mm->num_slots];
    slot->gpa = gpa;
    slot->hva = hva;
    slot->size = size;
    slot->slot_id = mm->num_slots;
    slot->flags = flags;

    mm->total_size += size;
    mm->num_slots++;

    log_debug("Added memory slot %d: GPA 0x%lx -> HVA %p (size=%ld)",
              slot->slot_id, gpa, hva, size);
    return 0;
}

/*
 * Find slot at GPA
 */
struct mm_slot* mm_find_slot(struct mm_ctx *mm, uint64_t gpa)
{
    int i;

    for (i = 0; i < mm->num_slots; i++) {
        struct mm_slot *slot = &mm->slots[i];

        if (gpa >= slot->gpa && gpa < (slot->gpa + slot->size))
            return slot;
    }

    return NULL;
}

/*
 * Translate GPA to HVA
 */
void* mm_gpa_to_hva(struct mm_ctx *mm, uint64_t gpa, uint64_t size)
{
    struct mm_slot *slot;

    slot = mm_find_slot(mm, gpa);
    if (!slot)
        return NULL;

    /* Check if access fits within slot */
    if ((gpa + size) > (slot->gpa + slot->size)) {
        log_warn("Access crosses slot boundary");
        return NULL;
    }

    uint64_t offset = gpa - slot->gpa;
    return (char *)slot->hva + offset;
}

/*
 * Write to guest physical memory
 */
int mm_write_gpa(struct mm_ctx *mm, uint64_t gpa, const void *data, uint64_t size)
{
    void *hva;

    hva = mm_gpa_to_hva(mm, gpa, size);
    if (!hva) {
        log_error("Failed to translate GPA 0x%lx for write", gpa);
        return -1;
    }

    memcpy(hva, data, size);
    return 0;
}

/*
 * Read from guest physical memory
 */
int mm_read_gpa(struct mm_ctx *mm, uint64_t gpa, void *data, uint64_t size)
{
    void *hva;

    hva = mm_gpa_to_hva(mm, gpa, size);
    if (!hva) {
        log_error("Failed to translate GPA 0x%lx for read", gpa);
        return -1;
    }

    memcpy(data, hva, size);
    return 0;
}
