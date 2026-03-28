// Minimal freestanding UEFI application.
//
// Milestone goal:
// - initialize COM1 early and print a debug marker
// - allocate memory for a kernel raw blob + stack
// - obtain a memory map key with GetMemoryMap
// - call ExitBootServices
// - set up RSP and jump into the kernel entry at the copied blob base

#include "../common/serial.hpp"
#include "kernel_blob.inc"

#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))

namespace uefi {

using EFI_HANDLE = void*;
using EFI_STATUS = uint64_t;
using UINTN = uint64_t;
using CHAR16 = uint16_t;

// Minimal EFI_STATUS values (only success/failure used for scaffolding).
static constexpr EFI_STATUS EFI_SUCCESS = 0;

struct EFI_MEMORY_DESCRIPTOR {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

enum struct AllocateType : uint32_t {
    ANY_PAGES = 0,
    MAX_ADDRESS = 1,
    ADDRESS = 2,
};

enum struct MemoryType : uint32_t {
    RESERVED_MEMORY_TYPE = 0,
    LOADER_CODE = 1,
    LOADER_DATA = 2,
    BOOT_SERVICES_CODE = 3,
    BOOT_SERVICES_DATA = 4,
    RUNTIME_SERVICES_CODE = 5,
    RUNTIME_SERVICES_DATA = 6,
    CONVENTIONAL_MEMORY = 7,
    UNUSABLE_MEMORY = 8,
    ACPI_RECLAIM_MEMORY = 9,
    ACPI_MEMORY_NVS = 10,
    MEMORY_MAPPED_IO = 11,
    MEMORY_MAPPED_IO_PORT_SPACE = 12,
    PAL_CODE = 13,
    PERSISTENT_MEMORY = 14,
};

// Function table types (only the ones we call are typed; others are `void*` placeholders).
// For EFI calls, enforce the Microsoft x64 ABI at the call site.
using AllocatePagesFn = EFI_STATUS(EFIAPI*)(uint32_t /*AllocateType*/, uint32_t /*MemoryType*/, UINTN, uint64_t*);
using GetMemoryMapFn = EFI_STATUS(EFIAPI*)(UINTN*, EFI_MEMORY_DESCRIPTOR*, UINTN*, UINTN*, uint32_t*);
using ExitBootServicesFn = EFI_STATUS(EFIAPI*)(EFI_HANDLE, UINTN);
using AllocatePoolFn = EFI_STATUS(EFIAPI*)(uint64_t /*MemoryType*/, UINTN /*Size*/, void** /*Buffer*/);

// Match `skift-vaerk-efi` layout: `BootService : Table` i.e. starts with an EFI
// table header, then function pointers.
struct BootTableHeader {
    uint64_t signature;
    uint32_t revision;
    uint32_t headerSize;
    uint32_t crc32;
    uint32_t reserved;
};

struct BootServices {
    BootTableHeader header;
    // Task Priority Services
    void* raiseTpl;
    void* lowerTpl;

    // Memory Services
    AllocatePagesFn allocatePages;
    void* freePages;
    GetMemoryMapFn getMemoryMap;
    void* allocatePool;
    void* freePool;

    // Event & Timer Services
    void* createEvent;
    void* setTimer;
    void* waitForEvent;
    void* signalEvent;
    void* closeEvent;
    void* checkEvent;

    // Protocol Handler Services
    void* installProtocolInterface;
    void* reinstallProtocolInterface;
    void* uninstallProtocolInterface;
    void* handleProtocol;
    void* _reserved;
    void* registerProtocolNotify;
    void* locateHandle;
    void* locateDevicePath;
    void* installConfigurationTable;

    // Image Services
    void* loadImage;
    void* startImage;
    void* exit;
    void* unloadImage;
    ExitBootServicesFn exitBootServices;
};

struct TableHeader {
    uint64_t signature;
    uint32_t revision;
    uint32_t headerSize;
    uint32_t crc32;
    uint32_t reserved;
};

struct Table {
    TableHeader header;
};

struct RuntimeService;
struct SimpleTextInputProtocol;
struct SimpleTextOutputProtocol;
struct BootService;
struct ConfigurationTable;

// SystemTable prefix up to BootServices pointer.
struct SystemTable : Table {
    uint16_t* firmwareVendor;
    uint32_t firmwareRevision;

    EFI_HANDLE* consoleInHandle;
    SimpleTextInputProtocol* conIn;

    EFI_HANDLE* consoleOutHandle;
    SimpleTextOutputProtocol* conOut;

    EFI_HANDLE* standardErrorHandle;
    SimpleTextOutputProtocol* stdErr;

    RuntimeService* runtime;
    BootServices* boot;

    // We truncate after `boot` because we only dereference this field.
};

} // namespace uefi

extern "C" uefi::EFI_STATUS EFIAPI efi_main(
    uefi::EFI_HANDLE imageHandle,
    uefi::SystemTable* systemTable
) {
    using namespace uefi;

    serial::init_115200();
    serial::write_str("UEFI: init\n");

    auto* bs = systemTable->boot;

    // Allocate kernel pages for the raw blob.
    uint64_t kernelPhys = 0;
    UINTN kernelPages = (kernel_blob_len + 4095ull) / 4096ull;
    if (kernelPages == 0)
        kernelPages = 1;

    serial::write_str("UEFI: alloc kernel pages\n");
    (void)bs->allocatePages(
        static_cast<uint32_t>(AllocateType::ANY_PAGES),
        static_cast<uint32_t>(MemoryType::LOADER_CODE),
        kernelPages,
        &kernelPhys
    );
    serial::write_str("UEFI: kernel pages allocated\n");

    // Copy kernel raw blob into allocated memory.
    auto* dst = reinterpret_cast<unsigned char*>(kernelPhys);
    for (unsigned long i = 0; i < kernel_blob_len; ++i)
        dst[i] = kernel_blob[i];

    // Allocate a dedicated stack.
    uint64_t stackPhys = 0;
    constexpr UINTN stackPages = 4; // 16KiB
    serial::write_str("UEFI: alloc stack pages\n");
    (void)bs->allocatePages(
        static_cast<uint32_t>(AllocateType::ANY_PAGES),
        static_cast<uint32_t>(MemoryType::LOADER_DATA),
        stackPages,
        &stackPhys
    );
    serial::write_str("UEFI: stack pages allocated\n");

    uint64_t stackTop = stackPhys + stackPages * 4096ull;
    stackTop &= ~0xFul; // keep 16-byte alignment friendly for the ABI.

    // Obtain a memory map key.
    UINTN mmapSize = 0;
    UINTN mapKey = 0;
    UINTN descSize = 0;
    uint32_t descVersion = 0;

    serial::write_str("UEFI: getMemoryMap(size)\n");
    (void)bs->getMemoryMap(&mmapSize, nullptr, &mapKey, &descSize, &descVersion);
    serial::write_str("UEFI: getMemoryMap(size) done\n");

    // Allocate a buffer for the memory map descriptors.
    constexpr UINTN PAGE = 4096;
    UINTN mmapPages = (mmapSize + PAGE - 1) / PAGE;
    if (mmapPages < 2)
        mmapPages = 2;
    // Kernel stage-1 paging builder reuses this LOADER_DATA region as a
    // temporary page-table pool. Keep generous headroom to avoid exhausting
    // pages on firmware that reports compact memory maps.
    constexpr UINTN MMAP_SCRATCH_EXTRA_PAGES = 128;
    mmapPages += MMAP_SCRATCH_EXTRA_PAGES;

    uint64_t mmapPhys = 0;
    serial::write_str("UEFI: alloc mmap pages\n");
    (void)bs->allocatePages(
        static_cast<uint32_t>(AllocateType::ANY_PAGES),
        static_cast<uint32_t>(MemoryType::LOADER_DATA),
        mmapPages,
        &mmapPhys
    );
    serial::write_str("UEFI: mmap buffer allocated\n");

    auto* mmapPtr = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(mmapPhys);
    serial::write_str("UEFI: getMemoryMap(fill)\n");
    (void)bs->getMemoryMap(&mmapSize, mmapPtr, &mapKey, &descSize, &descVersion);

    serial::write_str("UEFI: ExitBootServices\n");
    bs->exitBootServices(imageHandle, mapKey);

    serial::write_str("UEFI: jump kernel\n");

    // Jump to the kernel entry at the copied blob base.
    // The tutorial kernel uses the System V AMD64 ABI for `extern "C"` entry.
    using KernelEntry = void (*)(
        uint64_t /*mmapPhys*/,
        uint64_t /*mmapSize (bytes used for descriptors)*/,
        uint64_t /*descSize*/,
        uint64_t /*kernelPhys*/,
        uint64_t /*kernelPages*/,
        uint64_t /*stackPhys*/
    );
    auto entry = reinterpret_cast<KernelEntry>(kernelPhys);

    asm volatile(
        "mov %[st], %%rsp\n\t"
        "mov %[mmapPhys], %%rdi\n\t"
        "mov %[mmapSize], %%rsi\n\t"
        "mov %[descSize], %%rdx\n\t"
        "mov %[kernelPhys], %%rcx\n\t"
        "mov %[kernelPages], %%r8\n\t"
        "mov %[stackPhys], %%r9\n\t"
        "call *%[e]\n\t"
        :
        : [st] "r"(stackTop),
          [mmapPhys] "r"(mmapPhys),
          [mmapSize] "r"(mmapSize),
          [descSize] "r"(descSize),
          [kernelPhys] "r"(kernelPhys),
          [kernelPages] "r"(kernelPages),
          [stackPhys] "r"(stackPhys),
          [e] "r"(entry)
        : "rsp", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "memory"
    );

    // If the kernel returns, halt.
    for (;;) {
        asm volatile("cli; hlt");
    }
}

