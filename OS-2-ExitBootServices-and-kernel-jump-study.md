# OS-2 — ExitBootServices and kernel jump (was milestone 15)

This guide mirrors the structure and level of detail of `OS-4-paging-study.md`.

The core objective of milestone 15 is crossing the **firmware -> kernel** boundary while preserving the required contracts:

1. **UEFI memory map contract**: the `mapKey` you pass to `ExitBootServices` must match the filled memory map.
2. **CPU/ABI contract**: after `ExitBootServices`, you must start executing kernel code with a valid stack and correct calling convention.

## Step 0: What “boundary crossing” means here

Before the boundary:
- UEFI boot services exist and may be used to allocate memory and query platform state.

At/after the boundary (`ExitBootServices`):
- UEFI boot services must be treated as unavailable.
- Your kernel can only rely on what is already in RAM and on CPU state (registers + stack + code execution).

In this tutorial milestone, the handoff is implemented by:
- UEFI app: `[os/uefi/efi_main.cpp](/run/media/dimmus/dev1/os/0_brutal_skift/os_book/os/uefi/efi_main.cpp)`
- Kernel entry: `[os/kernel/init.cpp](/run/media/dimmus/dev1/os/0_brutal_skift/os_book/os/kernel/init.cpp)`

## Step 1: First `GetMemoryMap` (size discovery)

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
UINTN mmapSize = 0;
UINTN mapKey = 0;
UINTN descSize = 0;
uint32_t descVersion = 0;

serial::write_str("UEFI: getMemoryMap(size)\n");
(void)bs->getMemoryMap(&mmapSize, nullptr, &mapKey, &descSize, &descVersion);
serial::write_str("UEFI: getMemoryMap(size) done\n");
```

Line-by-line explanation:

1. `mmapSize = 0;`: start with “unknown size”.
2. `mapKey = 0;`: will be filled by `GetMemoryMap`.
3. `descSize = 0;`: will be filled with the descriptor size (important for later parsing).
4. `descVersion = 0;`: descriptor format version (also used with `getMemoryMap`).
5. `getMemoryMap(&mmapSize, nullptr, ...)`: UEFI returns the buffer size needed to store the current memory map.
6. The `nullptr` buffer argument means: “don’t fill yet; just tell me the required size”.

Deeper explanation:

This first call is a contract boundary: you can’t safely call `ExitBootServices` until you have a complete memory map buffer and a `mapKey` that matches it. The rest of milestone 15 is about making the “fill” call consistent with that `mapKey`.

## Step 2: Allocate descriptor storage (real RAM for descriptors)

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
constexpr UINTN PAGE = 4096;
UINTN mmapPages = (mmapSize + PAGE - 1) / PAGE;
if (mmapPages < 2)
    mmapPages = 2;

uint64_t mmapPhys = 0;
serial::write_str("UEFI: alloc mmap pages\n");
(void)bs->allocatePages(
    static_cast<uint32_t>(AllocateType::ANY_PAGES),
    static_cast<uint32_t>(MemoryType::LOADER_DATA),
    mmapPages,
    &mmapPhys
);
serial::write_str("UEFI: mmap buffer allocated\n");
```

Line-by-line explanation:

1. `PAGE = 4096`: descriptors are stored in 4KiB pages (matching the typical page size we’ll map later).
2. `mmapPages = ceil(mmapSize / PAGE)`: compute how many pages are needed.
3. `if (mmapPages < 2) mmapPages = 2;`: clamp to a minimum to reduce “edge” failures.
4. `mmapPhys = 0;`: output physical base address of the allocated buffer.
5. `allocatePages(... ANY_PAGES ...)`: request any suitable physical location.
6. `MemoryType::LOADER_DATA`: declare it as loader-owned scratch storage.

Deeper explanation:

The memory map descriptors must live in RAM that you can access later. This allocation produces a **real physical RAM region** (`mmapPhys`) which UEFI will fill with descriptor records in the next step.

## Step 3: Second `GetMemoryMap` (fill the buffer)

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
auto* mmapPtr = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(mmapPhys);
serial::write_str("UEFI: getMemoryMap(fill)\n");
(void)bs->getMemoryMap(&mmapSize, mmapPtr, &mapKey, &descSize, &descVersion);
```

Line-by-line explanation:

1. `mmapPtr = reinterpret_cast<...>(mmapPhys)`: treat the allocated physical buffer as an array of descriptor records.
2. `getMemoryMap(&mmapSize, mmapPtr, ...)`: UEFI writes the actual descriptor entries into the buffer.
3. `mapKey` is updated again: it must match this filled map.

C++ note: what `reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(mmapPhys)` means

- `mmapPhys` is an integer physical address.
- `reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(...)` tells the compiler to treat that address as pointing to `EFI_MEMORY_DESCRIPTOR` records.
- That lets the `getMemoryMap` call write descriptor structs into the buffer using the correct layout.

Deeper explanation:

From this point forward, `mapKey` must stay correct until you call `ExitBootServices`. If you allocate memory again between the “fill” call and `ExitBootServices`, UEFI may change the memory map and invalidate the key.

## Step 4: `ExitBootServices(imageHandle, mapKey)`

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
serial::write_str("UEFI: ExitBootServices\n");
bs->exitBootServices(imageHandle, mapKey);
```

Line-by-line explanation:

1. `exitBootServices(..., mapKey)`: tells firmware to switch off boot services.
2. If the key is wrong, the firmware will refuse the transition (and you won’t reach the kernel).

Deeper explanation:

After this call, the platform is in a state where boot services are gone. Your kernel must be fully self-contained: it can only depend on code and data already copied into allocated pages and CPU state like `RSP`.

## Step 5: Kernel jump + stack setup

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
uint64_t stackTop = stackPhys + stackPages * 4096ull;
stackTop &= ~0xFul; // keep 16-byte alignment friendly for the ABI.

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
```

Line-by-line explanation:

1. `stackTop = stackPhys + stackPages * 4096`: compute the end/top address of the stack region.
2. `stackTop &= ~0xFul`: adjust to keep 16-byte alignment.
3. `KernelEntry = void (*) ( ... )`: declares the function pointer type for the kernel entry.
4. `entry = reinterpret_cast<KernelEntry>(kernelPhys)`: entry address is the physical base where the kernel blob was copied.
5. `asm volatile("mov %[st], %%rsp\n\t" ...)`: set `RSP` so kernel code can make calls/locals.
6. `mov ... %%rdi/%%rsi/...`: set the SysV AMD64 ABI argument registers so `kernel_entry` receives parameters.
7. `call *%[e]`: transfers control to kernel mode code.

C++ note: why we use a function-pointer alias and `reinterpret_cast` here

- `using KernelEntry = void (*)(...)` defines a type for the exact “call signature” we expect at the kernel entry.
- `entry = reinterpret_cast<KernelEntry>(kernelPhys)` converts the integer physical address of the kernel blob into that function-pointer type.
- With this conversion, the subsequent `call *entry` becomes conceptually “call a function at this address”, with the right argument ABI.

Deeper explanation:

This is the second contract boundary: after `ExitBootServices`, you’re no longer in a firmware-managed environment. The CPU still needs:
- a valid stack pointer (`RSP`) for every push/pop/call/return,
- and a calling convention that matches what the kernel expects.

If you don’t set these correctly, you typically fault quickly (often before you see a “next” serial marker).

C++ note: what `asm volatile(...)` means in this loader

- `asm volatile(...)` embeds raw assembly and prevents the compiler from optimizing it away/reordering across the boundary.
- In boot code, this matters because we must set registers and immediately transfer control.

## Step 6: Kernel behavior: `HELLO INIT` and halt

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
extern "C" void kernel_entry(
    uint64_t mmapPhys,
    uint64_t mmapSizeUsed,
    uint64_t descSize,
    uint64_t kernelPhys,
    uint64_t kernelPages,
    uint64_t stackPhys
) {
    serial::init_115200();
    write_hello_init();

    for (;;) {
        asm volatile("cli; hlt");
    }
}
```

Line-by-line explanation:

1. `extern "C" void kernel_entry(...)`: kernel entry point. `extern "C"` keeps the symbol/ABI predictable.
2. `serial::init_115200();`: initialize COM1 so serial prints work.
3. `write_hello_init();`: prints `HELLO INIT\n`.
4. `for (;;){ cli; hlt }`: halt forever once done.

Deeper explanation:

The expected milestone success signal is simple: if you see `HELLO INIT`, you know:
- `ExitBootServices` succeeded,
- the control transfer reached kernel-mode code,
- and the stack was usable enough to run the serial initialization path.

