#ifndef VIBE_VMM_BOOT_H
#define VIBE_VMM_BOOT_H

#include "vm.h"
#include <stdint.h>

/* Screen info for boot params */
struct screen_info {
    uint8_t  orig_x;
    uint8_t  orig_y;
    uint16_t ext_mem_k;
    uint16_t orig_video_page;
    uint8_t  orig_video_mode;
    uint8_t  orig_video_cols;
    uint16_t unused2;
    uint16_t orig_video_ega_bx;
    uint16_t unused3;
    uint8_t  orig_video_lines;
    uint8_t  orig_video_isVGA;
    uint16_t orig_video_points;
    uint16_t lfb_width;
    uint16_t lfb_height;
    uint16_t lfb_depth;
    uint32_t lfb_base;
    uint32_t lfb_size;
    uint16_t cl_magic;
    uint16_t cl_offset;
    uint16_t lfb_linelength;
    uint8_t  red_size;
    uint8_t  red_pos;
    uint8_t  green_size;
    uint8_t  green_pos;
    uint8_t  blue_size;
    uint8_t  blue_pos;
    uint8_t  rsvd_size;
    uint8_t  rsvd_pos;
    uint16_t vesapm_seg;
    uint16_t vesapm_off;
    uint16_t pages;
    uint16_t vesa_attributes;
    uint32_t capabilities;
    uint32_t ext_lfb_base;
    uint8_t  _reserved[2];
} __attribute__((packed));

/* APM BIOS info */
struct apm_bios_info {
    uint16_t version;
    uint16_t cseg;
    uint32_t offset;
    uint16_t cseg_16;
    uint16_t dseg;
    uint16_t flags;
    uint16_t cseg_len;
    uint16_t cseg_16_len;
    uint16_t dseg_len;
} __attribute__((packed));

/* EFI info */
struct efi_info {
    uint32_t loader_signature;
    uint32_t systab;
    uint32_t memdesc_size;
    uint32_t memdesc_version;
    uint32_t memmap;
    uint32_t memmap_size;
    uint32_t systab_hi;
    uint32_t memmap_hi;
} __attribute__((packed));

/* E820 max entries */
#define E820_MAX_ENTRIES_ZEROPAGE 128

/* EDD MBR signature max */
#define EDD_MBR_SIG_MAX 16

/* Boot functions */
int boot_setup_linux(struct vm *vm);
int boot_setup_raw_binary(struct vm *vm, const char *path, uint64_t entry);

#endif /* VIBE_VMM_BOOT_H */
