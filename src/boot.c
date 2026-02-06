/*
 * Boot loader - Load and boot guest kernels (primarily Linux)
 */

#include "boot.h"
#include "vm.h"
#include "vcpu.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* Linux boot protocol */
#define BZIMAGE_MAGIC          0x53726448  /* "HdrS" */
#define LINUX_MAGIC_ADDR       0x202
#define BOOT_FLAG_ADDR         0x1FE
#define HEADER_ADDR            0x1F1
#define VID_MODE_ADDR          0x1FA
#define RAMDISK_IMAGE_ADDR     0x218
#define RAMDISK_SIZE_ADDR      0x21C
#define CMDLINE_PTR_ADDR       0x228
#define CMDLINE_SIZE_ADDR      0x238

/* Real mode kernel header */
struct linux_header {
    uint8_t  setup_sects;
    uint16_t root_flags;
    uint32_t syssize;
    uint16_t ram_size;
    uint16_t vid_mode;
    uint16_t root_dev;
    uint16_t boot_flag;
    uint16_t jump;
    uint32_t header;
    uint16_t version;
    uint32_t realmode_swtch;
    uint16_t start_sys_seg;
    uint16_t kernel_version;
    uint8_t  type_of_loader;
    uint8_t  loadflags;
    uint16_t setup_move_size;
    uint32_t code32_start;
    uint32_t ramdisk_image;
    uint32_t ramdisk_size;
    uint32_t bootsect_kludge;
    uint16_t heap_end_ptr;
    uint16_t pad1;
    uint32_t cmd_line_ptr;
    uint32_t initrd_addr_max;
    uint32_t kernel_alignment;
    uint8_t  relocatable_kernel;
    uint8_t  min_alignment;
    uint16_t xloadflags;
    uint32_t cmdline_size;
} PACKED;

/* E820 memory map entry */
struct e820_entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} PACKED;

#define E820_RAM       1
#define E820_RESERVED  2
#define E820_ACPI      3
#define E820_NVS       4
#define E820_UNUSABLE  5

/* Zero page structure for Linux boot */
struct boot_params {
    struct screen_info screen;
    struct apm_bios_info apm_bios_info;
    uint8_t  _pad2[4];
    uint64_t tboot_addr;
    uint8_t  _pad3[8];
    uint8_t  edid_info[0x80];
    struct efi_info efi_info;
    uint32_t alt_mem_k;
    uint32_t scratch;
    uint8_t  e820_entries;
    uint8_t  e820_map[0x80];
    uint8_t  _pad4[48];
    uint8_t  eddbuf[0x1a0];
    uint8_t  _pad5[0x60];
    uint32_t edd_mbr_sig_buf[EDD_MBR_SIG_MAX];
    struct e820_entry e820_map_max[E820_MAX_ENTRIES_ZEROPAGE];
    uint8_t  _pad6[276];
    uint8_t  setup_data[0xc0];
    uint64_t edd_mbr_sig_buf_ptr;
    uint64_t kexec_status;
    uint32_t setup_data_count;
    uint64_t acpi_rsdp_addr;
    uint8_t  _pad7[8];
    uint32_t sentinel;
    uint8_t  _pad8[140];
    struct linux_header hdr;
} PACKED;

/*
 * Load a Linux kernel (bzImage format)
 */
static int load_bzimage(struct vm *vm, const char *path)
{
    FILE *fp;
    uint8_t *kernel_buf;
    size_t kernel_size, setup_size, boot_size;
    struct linux_header *hdr;
    uint32_t code32_start;
    void *hva;
    int ret;

    fp = fopen(path, "rb");
    if (!fp) {
        log_error("Failed to open kernel: %s", path);
        return -1;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    kernel_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    kernel_buf = malloc(kernel_size);
    if (!kernel_buf) {
        log_error("Failed to allocate kernel buffer");
        fclose(fp);
        return -1;
    }

    fread(kernel_buf, 1, kernel_size, fp);
    fclose(fp);

    /* Parse header */
    if (kernel_size < 0x200) {
        log_error("Kernel too small");
        free(kernel_buf);
        return -1;
    }

    hdr = (struct linux_header *)(kernel_buf + HEADER_ADDR);

    /* Check magic */
    if (hdr->boot_flag != 0xAA55) {
        log_error("Invalid boot flag");
        free(kernel_buf);
        return -1;
    }

    if (hdr->header != BZIMAGE_MAGIC) {
        log_error("Invalid bzImage magic");
        free(kernel_buf);
        return -1;
    }

    log_info("Loading Linux kernel (version %d.%d)",
             hdr->version >> 8, hdr->version & 0xff);

    /* Calculate sizes */
    setup_size = (hdr->setup_sects + 1) * 512;
    boot_size = kernel_size - setup_size;

    /* Load real mode code (first 640K) */
    hva = vm_gpa_to_hva(vm, 0x10000, setup_size);
    if (!hva) {
        log_error("Failed to map real mode memory");
        free(kernel_buf);
        return -1;
    }
    memcpy(hva, kernel_buf, setup_size);

    /* Load protected mode code (usually at 1MB) */
    code32_start = hdr->code32_start ? hdr->code32_start : 0x100000;
    hva = vm_gpa_to_hva(vm, code32_start, boot_size);
    if (!hva) {
        log_error("Failed to map protected mode memory");
        free(kernel_buf);
        return -1;
    }
    memcpy(hva, kernel_buf + setup_size, boot_size);

    log_info("Loaded kernel: %ld bytes (real mode at 0x10000, code32 at 0x%x)",
             kernel_size, code32_start);

    free(kernel_buf);
    return 0;
}

/*
 * Load initrd
 */
static int load_initrd(struct vm *vm, const char *path, uint64_t max_addr)
{
    FILE *fp;
    uint8_t *initrd_buf;
    size_t initrd_size;
    void *hva;
    uint64_t initrd_addr;

    fp = fopen(path, "rb");
    if (!fp) {
        log_error("Failed to open initrd: %s", path);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    initrd_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    initrd_buf = malloc(initrd_size);
    if (!initrd_buf) {
        log_error("Failed to allocate initrd buffer");
        fclose(fp);
        return -1;
    }

    fread(initrd_buf, 1, initrd_size, fp);
    fclose(fp);

    /* Place initrd at max_addr - size (aligned down) */
    initrd_addr = (max_addr - initrd_size) & ~0xfff;
    if (initrd_addr < 0x100000)
        initrd_addr = 0x10000000;  /* Fallback for large initrds */

    hva = vm_gpa_to_hva(vm, initrd_addr, initrd_size);
    if (!hva) {
        log_error("Failed to map initrd memory");
        free(initrd_buf);
        return -1;
    }

    memcpy(hva, initrd_buf, initrd_size);
    free(initrd_buf);

    /* Update boot params with initrd location */
    hva = vm_gpa_to_hva(vm, 0x10000, sizeof(uint32_t));
    if (hva) {
        *(uint32_t *)((char *)hva + RAMDISK_IMAGE_ADDR) = initrd_addr;
        *(uint32_t *)((char *)hva + RAMDISK_SIZE_ADDR) = initrd_size;
    }

    log_info("Loaded initrd: %ld bytes at 0x%lx", initrd_size, initrd_addr);
    return 0;
}

/*
 * Setup command line
 */
static int setup_cmdline(struct vm *vm, const char *cmdline)
{
    void *hva;
    size_t len;
    uint64_t cmdline_addr = 0x20000;  /* Traditional location */

    len = strlen(cmdline) + 1;

    hva = vm_gpa_to_hva(vm, cmdline_addr, len);
    if (!hva) {
        log_error("Failed to map cmdline memory");
        return -1;
    }

    strcpy(hva, cmdline);

    /* Update boot params with cmdline pointer */
    hva = vm_gpa_to_hva(vm, 0x10000, sizeof(uint32_t));
    if (hva) {
        *(uint32_t *)((char *)hva + CMDLINE_PTR_ADDR) = cmdline_addr;
    }

    log_info("Set cmdline: %s", cmdline);
    return 0;
}

/*
 * Setup E820 memory map
 */
static int setup_e820(struct vm *vm)
{
    struct e820_entry *e820;
    void *hva;
    int num_entries = 0;

    /* E820 map is at 0x2D0 within zero page */
    hva = vm_gpa_to_hva(vm, 0x10000, 0x1000);
    if (!hva) {
        log_error("Failed to map zero page");
        return -1;
    }

    e820 = (struct e820_entry *)((char *)hva + 0x2D0);

    /* RAM: 0 - 640K */
    e820[num_entries].addr = 0;
    e820[num_entries].size = 0xA0000;
    e820[num_entries].type = E820_RAM;
    num_entries++;

    /* Reserved: 640K - 1M */
    e820[num_entries].addr = 0xA0000;
    e820[num_entries].size = 0x60000;
    e820[num_entries].type = E820_RESERVED;
    num_entries++;

    /* RAM: 1M to end of memory (simplified) */
    e820[num_entries].addr = 0x100000;
    e820[num_entries].size = vm->mem_size - 0x100000;
    e820[num_entries].type = E820_RAM;
    num_entries++;

    /* Update e820_entries count */
    *(uint8_t *)((char *)hva + 0x1E8) = num_entries;

    log_debug("Setup E820 map with %d entries", num_entries);
    return 0;
}

/*
 * Setup vCPU registers for boot
 */
static int setup_vcpu_regs(struct vm *vm, struct vcpu *vcpu)
{
    struct hv_regs regs = {0};
    struct hv_sregs sregs = {0};

    /* Set code segment (flat 32-bit) */
    sregs.cs.selector = 0x10;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFFFFF;
    sregs.cs.ar = 0x409B;  /* Code segment, readable, accessed */

    /* Data segments */
    sregs.ds.selector = 0x18;
    sregs.ds.base = 0;
    sregs.ds.limit = 0xFFFFFFFF;
    sregs.ds.ar = 0x4093;  /* Data segment, writable, accessed */

    sregs.es = sregs.ds;
    sregs.fs = sregs.ds;
    sregs.gs = sregs.ds;
    sregs.ss = sregs.ds;

    /* Setup GDT */
    sregs.gdt.base = 0x5000;
    sregs.gdt.limit = 0x7;
    sregs.gdt.ar = 0;

    /* Setup IDT */
    sregs.idt.base = 0;
    sregs.idt.limit = 0xFFFF;
    sregs.idt.ar = 0;

    /* CR0 and CR4 */
    sregs.cr0 = 0x11;  /* PE (protected mode) + ET */
    sregs.cr4 = 0;

    /* EFER */
    sregs.efer = 0;

    /* Set general registers */
    regs.rsi = 0x10000;  /* Pointer to boot params */
    regs.rip = 0x100000; /* Kernel entry point */
    regs.rflags = 0x2;   /* Reserved bit */

    if (hv_set_regs(vcpu->hv_vcpu, &regs) < 0) {
        log_error("Failed to set vCPU registers");
        return -1;
    }

    if (hv_set_sregs(vcpu->hv_vcpu, &sregs) < 0) {
        log_error("Failed to set vCPU special registers");
        return -1;
    }

    log_info("Setup vCPU %d for boot", vcpu->index);
    return 0;
}

/*
 * Setup Linux boot
 */
int boot_setup_linux(struct vm *vm)
{
    struct vcpu *vcpu;
    int ret;

    if (!vm->kernel_path) {
        log_error("No kernel specified");
        return -1;
    }

    /* Load kernel */
    ret = load_bzimage(vm, vm->kernel_path);
    if (ret < 0) {
        log_error("Failed to load kernel");
        return -1;
    }

    /* Load initrd if specified */
    if (vm->initrd_path) {
        ret = load_initrd(vm, vm->initrd_path, vm->mem_size);
        if (ret < 0) {
            log_error("Failed to load initrd");
            return -1;
        }
    }

    /* Setup command line if specified */
    if (vm->cmdline) {
        ret = setup_cmdline(vm, vm->cmdline);
        if (ret < 0) {
            log_error("Failed to setup cmdline");
            return -1;
        }
    }

    /* Setup E820 memory map */
    ret = setup_e820(vm);
    if (ret < 0) {
        log_error("Failed to setup E820 map");
        return -1;
    }

    /* Setup vCPU 0 registers */
    vcpu = vm->vcpus[0];
    ret = setup_vcpu_regs(vm, vcpu);
    if (ret < 0) {
        log_error("Failed to setup vCPU registers");
        return -1;
    }

    log_info("Linux boot setup complete");
    return 0;
}

/*
 * Load raw binary (for minimal test kernels)
 */
int boot_setup_raw_binary(struct vm *vm, const char *path, uint64_t entry)
{
    FILE *fp;
    uint8_t *code;
    size_t size;
    void *hva;
    struct vcpu *vcpu;
    struct hv_regs regs = {0};
    struct hv_sregs sregs = {0};

    fp = fopen(path, "rb");
    if (!fp) {
        log_error("Failed to open binary: %s", path);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    code = malloc(size);
    if (!code) {
        fclose(fp);
        return -1;
    }

    fread(code, 1, size, fp);
    fclose(fp);

    /* Load at entry point */
    hva = vm_gpa_to_hva(vm, entry, size);
    if (!hva) {
        log_error("Failed to map binary memory");
        free(code);
        return -1;
    }

    memcpy(hva, code, size);
    free(code);

    /* Setup vCPU for 64-bit long mode */
    vcpu = vm->vcpus[0];

    /* Flat segments */
    sregs.cs.selector = 0x10;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFFFFF;
    sregs.cs.ar = 0xA09B;  /* 64-bit code segment */

    sregs.ds.selector = 0x18;
    sregs.ds.base = 0;
    sregs.ds.limit = 0xFFFFFFFF;
    sregs.ds.ar = 0xC093;  /* 32-bit data segment */

    sregs.es = sregs.ds;
    sregs.fs = sregs.ds;
    sregs.gs = sregs.ds;
    sregs.ss = sregs.ds;

    /* Enable paging and protected mode */
    sregs.cr0 = 0x80010001;  /* PE, PG, ET */
    sregs.cr4 = 0x620;       /* PAE, OSFXSR, OSXMMEXCPT */
    sregs.efer = 0x1000;     /* LMA */
    sregs.cr3 = 0;           /* No page tables for flat mapping */

    regs.rip = entry;
    regs.rflags = 0x2;

#if defined(__aarch64__)
    /* For ARM64, the vCPU hasn't been created yet (it's created in the vCPU thread).
     * Store the initial PC value which will be applied after vCPU creation. */
    vcpu->initial_rip = entry;
    vcpu->has_initial_state = 1;
    log_debug("Stored initial PC=0x%lx for ARM64 vCPU", entry);
#else
    /* For x86_64, set registers directly */
    hv_set_sregs(vcpu->hv_vcpu, &sregs);
    hv_set_regs(vcpu->hv_vcpu, &regs);
#endif

    log_info("Loaded raw binary: %ld bytes at 0x%lx", size, entry);
    return 0;
}
