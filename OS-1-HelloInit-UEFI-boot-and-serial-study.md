# OS-1 — HelloInit UEFI boot and serial (was milestone 14)

The core learning boundary in milestone 14 is:

`UEFI -> ExitBootServices -> kernel entry -> serial log`

## Step 0: “what boundary are we teaching?”

Before the boundary:
- You still have UEFI boot services available.
- You can allocate memory and query the platform memory layout.

At/after the boundary (`ExitBootServices`):
- UEFI boot services must be treated as unavailable.
- Your kernel must run without relying on any further firmware calls.
- Control transfer is done by setting CPU state (stack + calling convention) and jumping into kernel code.

Expected “contract boundary” behavior:
- If the handoff is correct, the kernel prints `HELLO INIT` and halts.
- If it’s wrong, you typically crash before the kernel prints anything.

## Step 1 preface: we didn’t “start UEFI ourselves”

Even though we call this milestone “UEFI boot”, our code does not manually bring up firmware.

What happens in the real machine (and in QEMU/OVMF) is:

1. OVMF firmware starts on reset and is responsible for basic CPU/platform bring-up.
   It creates a working execution environment (for x86_64: long mode, a stack, and the ability to execute our program).
2. OVMF loads our UEFI executable from the FAT image and *calls its entrypoint*.
3. That entrypoint is the function we export in this tutorial: `efi_main(...)`.
4. Along with calling it, firmware passes a `imageHandle` and a pointer to the `SystemTable`.

So by the time our `efi_main` runs, we already have:
- a CPU that can execute our code,
- a valid stack for function calls,
- and UEFI-provided pointers (`systemTable->boot`, etc.) that let us call real UEFI services.

In other words: “bare metal” is handled by firmware; our job in milestone 14 is to use the *interfaces firmware gives us*.

Stack in this context is CPU state + a region of RAM.
- RSP (stack pointer) is a CPU register. It holds the address of the current top of the stack.
- The stack itself lives in normal main memory (RAM). UEFI allocates physical pages for it (e.g. stackPhys + stackPages*4096).
- When you set stackTop and then run call, the CPU uses RSP to do stack operations (push/pop/call/return), and those stack operations read/write the RAM pages backing that address range.
So: stack = RAM region, and where your CPU is using that stack = controlled by RSP.

## Step 1: Enter UEFI and initialize COM1 serial

## What is COM1 serial?

COM1 is a classic serial port connected to a simple UART chip (commonly an UART16550-compatible device). Under QEMU, -serial stdio routes COM1 output to the QEMU terminal/stdout, so you don’t need physical wiring. On hardware, COM1 would correspond to the physical serial port’s pins (or the USB-to-serial adapter’s UART), and you’d connect with a serial terminal to see output.

- It maps to I/O ports near `0x3F8` on x86_64 systems.
- A UART lets us send characters (bytes) one at a time over two wires, which QEMU/OVMF forwards to the “serial stdio” console.
- Because it does not require a runtime library, it is ideal for early boot debugging.

In this tutorial:

- `os/common/serial.hpp` implements the UART driver using port I/O (`outb`/`inb`) and polls UART status before sending each character.
- Both the UEFI app and the kernel include and use the same `serial.hpp`, so the serial “truth log” continues across the UEFI -> kernel boundary.

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
extern "C" uefi::EFI_STATUS EFIAPI efi_main(
    uefi::EFI_HANDLE imageHandle,
    uefi::SystemTable* systemTable
) {
    serial::init_115200();
    serial::write_str("UEFI: init\n");

    auto* bs = systemTable->boot;
```

Line-by-line explanation:

1. `extern "C" ... efi_main(...)`: exported UEFI entry symbol with C linkage (no C++ name mangling).
2. `EFIAPI`: uses the correct calling convention for UEFI function pointers on x86_64.
3. `serial::init_115200();`: programs the UART so serial output works.
4. `serial::write_str("UEFI: init\n");`: prints a marker so we know we reached the UEFI entry.
5. `auto* bs = systemTable->boot;`: grabs the firmware’s `BootServices` table pointer.

Deeper explanation:

Serial output is the earliest reliable observability channel. At this point, you can’t assume paging, interrupts, or libc exist—so COM1 is your “truth source” for progress.

## Step 2: Allocate kernel pages and copy the kernel blob

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
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
```

Line-by-line explanation:

1. `kernelPhys = 0;`: will be filled with the physical base address returned by firmware.
2. `kernelPages = .../4096`: computes how many 4KiB pages are needed for the raw kernel blob.
3. `if (kernelPages == 0) kernelPages = 1;`: defensive clamp (shouldn’t happen, but avoids 0-page allocations).
4. `allocatePages(... ANY_PAGES ...)`: requests physical memory.
5. `MemoryType::LOADER_CODE`: marks the allocation as code-like for the memory map contract.
6. `&kernelPhys`: output parameter receives the physical base.
7. Copy loop: writes each byte of the kernel raw blob (`kernel_blob[i]`) into RAM at `kernelPhys + i`.

In step 4 we have the explanation of memory map contract.

C++ note: what `reinterpret_cast<unsigned char*>(kernelPhys)` does

- `kernelPhys` is a physical address (an integer).
- `reinterpret_cast<unsigned char*>(kernelPhys)` tells the compiler: “treat that address as a pointer to a byte array”.
- Once that cast exists, `dst[i] = ...` becomes a normal “write one byte at address (kernelPhys + i)” operation.

Deeper explanation:

UEFI loads your kernel as a *raw binary* by copying bytes into allocated physical pages. After `ExitBootServices`, the kernel code is just bytes in RAM; there’s no further “loading” step from firmware.

## Step 3: Allocate a dedicated stack

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
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
```

Line-by-line explanation:

1. `stackPhys = 0;`: output for the physical base of the stack allocation.
2. `stackPages = 4`: stack size = 16KiB.
3. `allocatePages(... LOADER_DATA ...)`: stack is “data-like” in memory map terms.
4. `stackTop = stackPhys + stackPages*4096`: compute the end/top of the stack region.
5. `stackTop &= ~0xF`: align down to 16 bytes for the ABI’s friendliness.

C++ note: how `stackTop &= ~0xFul` works

- `stackTop &= mask` is shorthand for `stackTop = stackTop & mask` (in-place “AND assignment”).
- `0xFul` is `15` (`0b1111`) but forced to an unsigned type.
- `~0xFul` bitwise-negates that value, producing a mask where:
  - the low 4 bits are `0`
  - all higher bits are `1`
- When you do `stackTop & ~0xFul`, the result keeps the higher bits but **clears the lowest 4 bits**.
- Clearing the lowest 4 bits makes `stackTop` a multiple of 16, because 16-byte alignment means the low 4 address bits must be `0`.

C++ note: why `static_cast<uint32_t>(AllocateType::ANY_PAGES)` is used

- `AllocateType` and `MemoryType` are `enum struct` types (strongly typed).
- UEFI function pointer signatures expect raw integer parameters (not the enum types).
- `static_cast<uint32_t>(...)` explicitly converts the enum value into the underlying integer type that the ABI call expects.

Deeper explanation:

This stack is not optional. Your kernel entry is normal x86_64 code and will use `RSP` for calls and local variables. If `RSP` points to an unmapped/invalid region, it will page fault or corrupt execution immediately.

## Step 4: Obtain the memory map key (`GetMemoryMap(size)`)

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

1. `mmapSize = 0;`: tells firmware you don’t know the required buffer size yet.
2. `mapKey = 0;`, `descSize = 0;`, `descVersion = 0;`: will be filled based on the platform state.
3. `getMemoryMap(&mmapSize, nullptr, &mapKey, ...)`: first call pattern that returns the required size (and also fills key/descriptor info).
4. The `serial::write_str` calls provide observable progress markers.

What is `UINTN`?

In UEFI, `UINTN` is an **unsigned integer type whose width matches the platform’s native word size** (pointer-size).

In this tutorial’s UEFI shim (`os/uefi/efi_main.cpp`) we define:
- `using UINTN = uint64_t;`

So in our code, `UINTN` is used for values like:
- `mmapSize` (sizes/buffer lengths)
- `mapKey` (the consistency token)
- `descSize` / `descVersion` (descriptor format info)

Deeper explanation:

The memory-map key (`mapKey`) must match the filled memory map at `ExitBootServices`. That’s the key reason the loader uses a two-call pattern: size discovery, then fill.

## The UEFI memory map contract (what “must match” really means)

UEFI maintains an internal table describing the machine’s current memory layout (what regions are RAM, reserved, firmware code/data, etc.). In your loader:

1. `GetMemoryMap(size)` gives you:
   - `mmapSize`: how big a buffer you need
   - `mapKey`: a “version”/consistency token for that exact snapshot of the memory map
   - `descSize/descVersion`: the descriptor format you must use when parsing
2. You allocate a buffer and call `GetMemoryMap(fill)` again, which fills that buffer with the descriptors for the snapshot that `mapKey` corresponds to.

Then, when you call `ExitBootServices(imageHandle, mapKey)`:

- UEFI checks whether the memory map has changed since the snapshot identified by `mapKey`.
- If it has changed, `ExitBootServices` fails, because leaving boot services with a stale memory map (wrong “world view”) would break later assumptions.

## Step 5: Allocate descriptor storage for the memory map

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

1. `PAGE = 4096`: descriptor storage is page-based.
2. `mmapPages = ceil(mmapSize/PAGE)`: pages needed for the whole descriptor buffer.
3. clamp to minimum 2 pages.
4. `mmapPhys`: output physical address of the descriptor buffer.
5. `allocatePages(... LOADER_DATA ...)`: declare the buffer as loader data.

Deeper explanation:

This creates the RAM region where UEFI writes the memory-map descriptors. Without this storage, you can’t call `ExitBootServices` successfully.

## Step 6: Fill the memory map buffer (`GetMemoryMap(fill)`)

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
auto* mmapPtr = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(mmapPhys);
serial::write_str("UEFI: getMemoryMap(fill)\n");
(void)bs->getMemoryMap(&mmapSize, mmapPtr, &mapKey, &descSize, &descVersion);
```

Line-by-line explanation:

1. `mmapPtr`: treats the allocated RAM region as an array of `EFI_MEMORY_DESCRIPTOR`.
2. `getMemoryMap(&mmapSize, mmapPtr, &mapKey, ...)`: UEFI writes descriptors into `mmapPhys`.
3. `mapKey` is updated again: it must correspond to this filled map.

C++ note: what `reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(mmapPhys)` does

- `mmapPhys` is the physical base address of the descriptor buffer (an integer).
- `reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(mmapPhys)` treats that base as a pointer to the descriptor struct type.
- After the cast, calling `getMemoryMap` can fill the bytes as an array of `EFI_MEMORY_DESCRIPTOR` records.

Deeper explanation:

Between the “fill” call and `ExitBootServices`, you must avoid allocations that could change the memory map. Even a small allocation can alter the map key and make exit fail.

## Step 7: ExitBootServices

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
serial::write_str("UEFI: ExitBootServices\n");
bs->exitBootServices(imageHandle, mapKey);
```

Line-by-line explanation:

1. prints an observable marker.
2. calls `exitBootServices` with `mapKey`.

Deeper explanation:

After this call returns successfully, you are no longer allowed to rely on any UEFI boot service calls. The only safe path is to jump into the kernel with the memory you already allocated and copied.

## Step 8: Jump into the kernel (set RSP + pass args)

### Code (excerpt from `os/uefi/efi_main.cpp`)

```cpp
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

1. `using KernelEntry = void (*) (uint64_t /*mmapPhys*/, uint64_t /*mmapSize (bytes used for descriptors)*/, uint64_t /*descSize*/, uint64_t /*kernelPhys*/, uint64_t /*kernelPages*/, uint64_t /*stackPhys*/)` : declares the kernel entry function signature (exactly 6 integer/pointer args).
2. `entry = reinterpret_cast<KernelEntry>(kernelPhys)`: treats the kernel physical base as a callable function pointer.
3. `mov %[st], %%rsp`: sets the kernel stack pointer.
4. `mov ... %%rdi/%%rsi/..`: sets ABI argument registers so `kernel_entry` receives parameters.
5. `call *%[e]`: transfers control to the kernel entry.

psABI (x86-64 SysV) exact sections (registers + stack alignment) used by this handoff:
- `\section{Function Calling Sequence}`: https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex#L404-541
- `\subsection{The Stack Frame}` (16-byte alignment + red zone): https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex#L482-541
- `\subsection{Parameter Passing}` / INTEGER register sequence (`RDI, RSI, RDX, RCX, r8, r9`): https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex#L543-716

C++ note: why use a function-pointer alias + `reinterpret_cast`

- `using KernelEntry = void (*) (uint64_t /*mmapPhys*/, uint64_t /*mmapSize (bytes used for descriptors)*/, uint64_t /*descSize*/, uint64_t /*kernelPhys*/, uint64_t /*kernelPages*/, uint64_t /*stackPhys*/)` is a type alias that describes “a function that takes exactly these 6 integer/pointer arguments and returns void”.
- `entry = reinterpret_cast<KernelEntry>(kernelPhys)` converts the integer physical address where the kernel bytes live into that function-pointer type.
- This is not a “regular C++ function call”; it’s a bare-metal jump using C++ types to make the call-site ABI match what the kernel expects.

Deeper explanation:

This is a “bare metal calling” moment: the CPU needs a valid stack and the correct calling convention. That’s why `stackTop` and the argument registers are set immediately before the `call`.

Extra C++ note: what `asm volatile(...)` is doing here

- `asm volatile(...)` embeds raw assembly into the C++ code.
- `volatile` tells the compiler not to optimize away or reorder this assembly block, which is critical for “set registers, then call” correctness.

## Step 9: Kernel prints `HELLO INIT` and halts

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
extern "C" void kernel_entry() {
    serial::init_115200();
    write_hello_init();

    for (;;) {
        asm volatile("cli; hlt");
    }
}
```

Line-by-line explanation:

1. `extern "C" ... kernel_entry()`: predictable entry symbol, matches how the loader calls it.
2. `serial::init_115200();`: initializes COM1 on the kernel side as well.
3. `write_hello_init();`: prints `HELLO INIT`.
4. `cli; hlt` loop: stops execution forever after printing.

Deeper explanation:

In milestone 14, seeing `HELLO INIT` is the success criteria. It proves:
- the kernel code bytes were copied correctly,
- the handoff reached kernel mode,
- and the stack was usable enough to run serial initialization and the print loop.

## Expected output (QEMU serial)

When running `os/`, you should see a sequence like:

- `UEFI: init`
- `UEFI: alloc kernel pages`
- `UEFI: alloc stack pages`
- `UEFI: getMemoryMap(size) ... done`
- `UEFI: ExitBootServices`
- `UEFI: jump kernel`
- `HELLO INIT`

