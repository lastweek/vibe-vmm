/*
 * Vibe-VMM - Main entry point
 *
 * A minimal Virtual Machine Monitor inspired by Firecracker
 */

#include "vm.h"
#include "vcpu.h"
#include "boot.h"
#include "devices.h"
#include "hypervisor.h"
#include "vfio.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Global VM pointer (for signal handler) */
static struct vm *g_vm = NULL;
static volatile int g_running = 1;

/* Command line options */
struct cmdline_args {
    char     *kernel_path;
    char     *initrd_path;
    char     *cmdline;
    uint64_t mem_size;
    int      num_vcpus;
    char     *disk_path;
    char     *net_tap;
    char     *vfio_bdf;
    int      enable_console;
    int      log_level;
    char     *binary_path;      /* Raw binary for testing */
    uint64_t binary_entry;      /* Entry point for raw binary */
};

/* Default values */
#define DEFAULT_MEM_SIZE   (512 * 1024 * 1024)  /* 512 MB */
#define DEFAULT_NUM_VCPUS  1

/*
 * Signal handler for graceful shutdown
 */
static void signal_handler(int sig)
{
    (void)sig;
    log_info("Received shutdown signal");
    g_running = 0;
    if (g_vm)
        vm_stop(g_vm);
}

/*
 * Setup signal handlers
 */
static void setup_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
}

/*
 * Parse size string (e.g., "512M", "1G")
 */
static uint64_t parse_size(const char *str)
{
    uint64_t size;
    char suffix;

    if (sscanf(str, "%lu%c", &size, &suffix) == 2) {
        switch (suffix) {
        case 'G':
        case 'g':
            size *= 1024 * 1024 * 1024;
            break;
        case 'M':
        case 'm':
            size *= 1024 * 1024;
            break;
        case 'K':
        case 'k':
            size *= 1024;
            break;
        }
    } else {
        size = atoi(str);
    }

    return size;
}

/*
 * Print usage
 */
static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [OPTIONS]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --kernel <path>       Guest kernel (bzImage/vmlinux)\n");
    fprintf(stderr, "  --initrd <path>       Initial ramdisk\n");
    fprintf(stderr, "  --cmdline <string>    Kernel command line\n");
    fprintf(stderr, "  --mem <size>          Guest memory size (default: 512M)\n");
    fprintf(stderr, "                        Examples: 512M, 1G, 256M\n");
    fprintf(stderr, "  --cpus <num>          Number of vCPUs (default: 1)\n");
    fprintf(stderr, "  --disk <path>         Disk image for virtio-blk\n");
    fprintf(stderr, "  --net tap=<ifname>    TAP interface for virtio-net\n");
    fprintf(stderr, "  --vfio <BDF>          VFIO device passthrough (e.g., 0000:01:00.1)\n");
    fprintf(stderr, "  --console             Enable MMIO debug console\n");
    fprintf(stderr, "  --binary <path>       Load raw binary at entry point\n");
    fprintf(stderr, "  --entry <addr>        Entry point for raw binary (hex)\n");
    fprintf(stderr, "  --log <level>         Log level: 0=none, 1=error, 2=warn, 3=info, 4=debug\n");
    fprintf(stderr, "  --help                Show this help message\n");
    fprintf(stderr, "\nExample:\n");
    fprintf(stderr, "  %s --kernel bzImage --mem 1G --cpus 2 --console\n", prog);
}

/*
 * Parse command line arguments
 */
static int parse_args(int argc, char *argv[], struct cmdline_args *args)
{
    static struct option long_options[] = {
        { "kernel", required_argument, 0, 'k' },
        { "initrd", required_argument, 0, 'i' },
        { "cmdline", required_argument, 0, 'c' },
        { "mem", required_argument, 0, 'm' },
        { "cpus", required_argument, 0, 'n' },
        { "disk", required_argument, 0, 'd' },
        { "net", required_argument, 0, 't' },
        { "vfio", required_argument, 0, 'v' },
        { "console", no_argument, 0, 'C' },
        { "binary", required_argument, 0, 'b' },
        { "entry", required_argument, 0, 'e' },
        { "log", required_argument, 0, 'l' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    int opt, option_index = 0;

    memset(args, 0, sizeof(*args));
    args->mem_size = DEFAULT_MEM_SIZE;
    args->num_vcpus = DEFAULT_NUM_VCPUS;
    args->log_level = LOG_LEVEL_INFO;

    while ((opt = getopt_long(argc, argv, "k:i:c:m:n:d:t:v:Cb:e:l:h",
                              long_options, &option_index)) != -1) {
        switch (opt) {
        case 'k':
            args->kernel_path = strdup(optarg);
            break;

        case 'i':
            args->initrd_path = strdup(optarg);
            break;

        case 'c':
            args->cmdline = strdup(optarg);
            break;

        case 'm':
            args->mem_size = parse_size(optarg);
            if (args->mem_size == 0) {
                fprintf(stderr, "Invalid memory size: %s\n", optarg);
                return -1;
            }
            break;

        case 'n':
            args->num_vcpus = atoi(optarg);
            if (args->num_vcpus <= 0 || args->num_vcpus > VM_MAX_VCPUS) {
                fprintf(stderr, "Invalid CPU count: %s (max %d)\n",
                        optarg, VM_MAX_VCPUS);
                return -1;
            }
            break;

        case 'd':
            args->disk_path = strdup(optarg);
            break;

        case 't':
            if (strncmp(optarg, "tap=", 4) == 0) {
                args->net_tap = strdup(optarg + 4);
            } else {
                fprintf(stderr, "Invalid network format (use tap=<ifname>)\n");
                return -1;
            }
            break;

        case 'v':
            args->vfio_bdf = strdup(optarg);
            break;

        case 'C':
            args->enable_console = 1;
            break;

        case 'b':
            args->binary_path = strdup(optarg);
            break;

        case 'e':
            args->binary_entry = strtoull(optarg, NULL, 16);
            break;

        case 'l':
            args->log_level = atoi(optarg);
            if (args->log_level < LOG_LEVEL_NONE ||
                args->log_level > LOG_LEVEL_DEBUG) {
                fprintf(stderr, "Invalid log level: %s\n", optarg);
                return -1;
            }
            break;

        case 'h':
            print_usage(argv[0]);
            exit(0);

        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    /* Validate arguments */
    if (!args->kernel_path && !args->binary_path) {
        fprintf(stderr, "Error: No kernel or binary specified\n");
        print_usage(argv[0]);
        return -1;
    }

    if (args->binary_path && !args->binary_entry) {
        fprintf(stderr, "Error: Binary entry point not specified\n");
        return -1;
    }

    /* Set default cmdline if not specified */
    if (args->kernel_path && !args->cmdline) {
        args->cmdline = strdup("console=hvc0 earlyprintk=serial panic=1");
    }

    return 0;
}

/*
 * Free command line arguments
 */
static void free_args(struct cmdline_args *args)
{
    free(args->kernel_path);
    free(args->initrd_path);
    free(args->cmdline);
    free(args->disk_path);
    free(args->net_tap);
    free(args->vfio_bdf);
    free(args->binary_path);
}

/*
 * Main function
 */
int main(int argc, char *argv[])
{
    struct cmdline_args args;
    struct vm *vm;
    struct device *dev;
    struct vfio_container *vfio_cont = NULL;
    int ret, i;

    printf("Vibe-VMM v0.1 - A Minimal Virtual Machine Monitor\n");
    printf("==============================================\n\n");

    /* Parse command line */
    ret = parse_args(argc, argv, &args);
    if (ret < 0)
        return EXIT_FAILURE;

    /* Set log level */
    log_level = args.log_level;

    /* Setup signal handlers */
    setup_signals();

    /* Initialize hypervisor (auto-detect platform and architecture) */
    log_info("Initializing hypervisor...");
    ret = hv_init(HV_TYPE_AUTO);

    if (ret < 0) {
        fprintf(stderr, "Failed to initialize hypervisor\n");
        free_args(&args);
        return EXIT_FAILURE;
    }

    /* Create VM */
    log_info("Creating VM...");
    vm = vm_create();
    if (!vm) {
        fprintf(stderr, "Failed to create VM\n");
        hv_cleanup();
        free_args(&args);
        return EXIT_FAILURE;
    }

    g_vm = vm;

    /* Configure VM */
    if (args.kernel_path) {
        vm_set_kernel(vm, args.kernel_path);
        vm_set_initrd(vm, args.initrd_path);
        vm_set_cmdline(vm, args.cmdline);
    }

    /* Add memory regions */
    log_info("Allocating guest memory: %ld MB", args.mem_size / (1024 * 1024));

    /* Low memory (first 3.5GB) */
    ret = vm_add_memory_region(vm, 0, args.mem_size);
    if (ret < 0) {
        fprintf(stderr, "Failed to add memory region\n");
        goto cleanup;
    }

    /* Create vCPUs */
    log_info("Creating %d vCPU(s)...", args.num_vcpus);
    ret = vm_create_vcpus(vm, args.num_vcpus);
    if (ret < 0) {
        fprintf(stderr, "Failed to create vCPUs\n");
        goto cleanup;
    }

    /* Register devices */
    log_info("Registering devices...");

    /* MMIO debug console */
    if (args.enable_console) {
        dev = mmio_console_create();
        if (dev) {
            vm_register_device(vm, dev);
        }
    }

    /* Virtio console */
    dev = virtio_console_create();
    if (dev) {
        vm_register_device(vm, dev);
    }

    /* Virtio block */
    if (args.disk_path) {
        dev = virtio_blk_create(args.disk_path);
        if (dev) {
            vm_register_device(vm, dev);
        }
    }

    /* Virtio network */
    if (args.net_tap) {
        dev = virtio_net_create(args.net_tap);
        if (dev) {
            vm_register_device(vm, dev);
        }
    }

    /* VFIO passthrough */
    if (args.vfio_bdf) {
        struct vfio_dev *vfio_dev;

        vfio_cont = vfio_container_create();
        if (!vfio_cont) {
            fprintf(stderr, "Failed to create VFIO container\n");
            goto cleanup;
        }

        vfio_dev = vfio_device_open(vfio_cont, args.vfio_bdf);
        if (!vfio_dev) {
            fprintf(stderr, "Failed to open VFIO device\n");
            goto cleanup;
        }

        ret = vfio_register_with_vm(vm, vfio_dev, args.vfio_bdf);
        if (ret < 0) {
            fprintf(stderr, "Failed to register VFIO device\n");
            goto cleanup;
        }
    }

    /* Boot setup */
    log_info("Setting up boot...");
    if (args.kernel_path) {
        ret = boot_setup_linux(vm);
        if (ret < 0) {
            fprintf(stderr, "Failed to setup Linux boot\n");
            goto cleanup;
        }
    } else if (args.binary_path) {
        ret = boot_setup_raw_binary(vm, args.binary_path, args.binary_entry);
        if (ret < 0) {
            fprintf(stderr, "Failed to setup raw binary\n");
            goto cleanup;
        }
    }

    /* Start VM */
    log_info("Starting VM...");
    printf("\n");
    printf("==============================================\n");
    printf("VM is running. Press Ctrl+C to stop.\n");
    printf("==============================================\n");
    printf("\n");

    ret = vm_start(vm);
    if (ret < 0) {
        fprintf(stderr, "Failed to start VM\n");
        goto cleanup;
    }

    /* Wait for VM to stop */
    while (g_running && vm->state == VM_STATE_RUNNING) {
        /* Sleep and wait */
        sleep(1);

        /* Check vCPU status */
        for (i = 0; i < vm->num_vcpus; i++) {
            if (vm->vcpus[i]->should_stop) {
                g_running = 0;
                break;
            }
        }
    }

    printf("\n");
    printf("==============================================\n");
    printf("VM stopped.\n");
    printf("==============================================\n");
    printf("\n");

    /* Print statistics */
    log_info("VM exit statistics:");
    for (i = 0; i < vm->num_vcpus; i++) {
        struct vcpu *vcpu = vm->vcpus[i];
        log_info("  vCPU %d: exits=%ld, io=%ld, mmio=%ld, halt=%ld",
                 vcpu->index, vcpu->exit_count, vcpu->io_count,
                 vcpu->mmio_count, vcpu->halt_count);
    }

cleanup:
    /* Cleanup */
    if (vm)
        vm_destroy(vm);

    if (vfio_cont)
        vfio_container_destroy(vfio_cont);

    hv_cleanup();
    free_args(&args);

    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
