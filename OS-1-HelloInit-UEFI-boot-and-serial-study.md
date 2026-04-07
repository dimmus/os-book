# OS-1 — UEFI boot and serial

**Prerequisite (disk image and FAT):** [OS-0 — Introduction](OS-0-Introduction.md) explains how `build/fat.img` is produced, why FAT16, and what `BOOTX64.EFI` / `efi_in` refer to.

The core learning boundary is:

`UEFI -> ExitBootServices -> kernel entry -> serial log`

## Changed and new files

Paths are relative to the tutorial tree [`src-os/`](src-os/) in this book repo.

| Role | Path |
| --- | --- |
| UART / COM1 | [`src-os/common/serial.hpp`](src-os/common/serial.hpp) |
| UEFI app: alloc, memory map, `ExitBootServices`, jump | [`src-os/uefi/efi_main.cpp`](src-os/uefi/efi_main.cpp) |
| Kernel entry, `HELLO INIT` | [`src-os/kernel/init.cpp`](src-os/kernel/init.cpp) |
| Top-level build / QEMU | [`src-os/Makefile`](src-os/Makefile) |
| Kernel ELF → `kernel.raw` | [`src-os/kernel/Makefile`](src-os/kernel/Makefile), [`src-os/kernel/ld.ld`](src-os/kernel/ld.ld) |
| Embed blob in UEFI | [`src-os/uefi/Makefile`](src-os/uefi/Makefile), [`src-os/uefi/gen_kernel_blob.py`](src-os/uefi/gen_kernel_blob.py) → `kernel_blob.inc` |

## Memory and CPU state snapshot

**What you get (firmware / UEFI, before `kernel_entry`):**

| Source | In RAM / state |
| --- | --- |
| `allocatePages` | **`kernelPhys`**: region holding copied **`kernel.raw`** blob |
| `allocatePages` | **`stackPhys`..`stackTop`**: kernel stack (16 KiB in this tree) |
| `GetMemoryMap` | **`mmapPhys`**: buffer of **`EFI_MEMORY_DESCRIPTOR`** records (size + key contract for exit) |
| CPU | Long mode, firmware **GDT/IDT**, **`CR3`** = firmware paging root (you replace tables in [OS-4](OS-4-paging-study.md)) |

**What this milestone builds / fills:**

- **COM1** UART programmed (`serial::init_115200`) — I/O ports `0x3F8` region, not yet tied to page tables.
- **Kernel blob** copied byte-for-byte to **`kernelPhys`**.
- **Handoff path** (inline asm after `ExitBootServices`): valid **`RSP`**, then **`call`** to code at **`kernelPhys`**.

**CPU registers at kernel entry (SysV AMD64 handoff):**

```
RSP  ← stackTop (16-byte aligned)
RDI  ← mmapPhys
RSI  ← mmapSize (descriptor bytes used)
RDX  ← descSize
RCX  ← kernelPhys
R8   ← kernelPages
R9   ← stackPhys
RIP  → first instruction of kernel (linked VMA 0, executing at physical kernelPhys in identity fashion)
```

**Relations / paths:**

```text
  UEFI BootServices ──allocate/copy──► kernel blob + stack + mmap buffer (RAM)
       │
       ▼
  ExitBootServices(mapKey) ──► firmware tables still active; no more BS calls
       │
       ▼
  asm: mov RSP; fill RDI..R9; call *entry ──► kernel_entry() ──► serial ──► HELLO INIT
```

Until [OS-4](OS-4-paging-study.md), **linear ≈ physical** for the regions you touch; **`kLinear`** / **`CR3`** switch are not in play yet.

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

In other words: “bare metal” is handled by firmware; our job is to use the *interfaces firmware gives us*.

Stack in this context is CPU state + a region of RAM.
- RSP (stack pointer) is a CPU register. It holds the address of the current top of the stack.
- The stack itself lives in normal main memory (RAM). UEFI allocates physical pages for it (e.g. stackPhys + stackPages*4096).
- When you set stackTop and then run call, the CPU uses RSP to do stack operations (push/pop/call/return), and those stack operations read/write the RAM pages backing that address range.
So: stack = RAM region, and where your CPU is using that stack = controlled by RSP.

## Step 1: Enter UEFI and initialize COM1 serial

## What is COM1 serial?

**UART** (**Universal Asynchronous Receiver/Transmitter**) is the hardware that turns **parallel bytes** from the CPU into a **serial bit stream** on a wire (and the reverse), using a chosen **baud rate** and **framing**—see [`doc/uart/README.md`](doc/uart/README.md) for a longer definition and [`doc/uart/PC16550D.pdf`](doc/uart/PC16550D.pdf) for the chip-level register map.

COM1 is a classic serial port connected to a simple UART chip (commonly an UART16550-compatible device). Under QEMU, -serial stdio routes COM1 output to the QEMU terminal/stdout, so you don’t need physical wiring. On hardware, COM1 would correspond to the physical serial port’s pins (or the USB-to-serial adapter’s UART), and you’d connect with a serial terminal to see output.

- It maps to I/O ports near `0x3F8` on x86_64 systems.
- A UART lets us send characters (bytes) one at a time over two wires, which QEMU/OVMF forwards to the “serial stdio” console.
- Because it does not require a runtime library, it is ideal for early boot debugging.

### Who turns log bytes into letters on the screen? (UART vs framebuffer)

This tutorial’s **log path does not use a framebuffer, bitmap fonts, or a pixel engine inside the kernel.** The code only programs the **UART** and sends **small integer byte values** (ASCII) out of **I/O ports**. No glyph tables or VRAM writes are involved in `serial::write_char`.

| Layer | Who does what |
| --- | --- |
| **Guest (your OS / UEFI app)** | Writes bytes to the UART (`outb` to `0x3F8` …). That is the **entire** “display” contract for this milestone. |
| **QEMU** (`-serial stdio`) | Takes emulated UART output and copies those bytes to the **host** process’s **stdout** (or a pty). Still **no** guest-side fonts. |
| **Host terminal** | **xterm**, **Alacritty**, the IDE’s **integrated terminal**, **Windows Terminal**, etc. interpret the byte stream, pick a **font**, shape **glyphs**, handle **Unicode** if configured, and **paint pixels** on your monitor. That rendering is **host OS + terminal emulator** software, not your kernel. |
| **Physical hardware** | The UART chip (or USB–serial bridge) shifts bits on a cable; a **serial terminal** on another machine (**`minicom`**, **`screen`**, PuTTY, etc.) or a **BMC serial-over-LAN** session receives bytes and draws text the same way—**receiver-side** UI. The motherboard does not “draw HELLO INIT” to your monitor over HDMI from this path. |

**If you wanted graphics from the machine itself**, you would use a **different** stack: e.g. **UEFI GOP** (framebuffer pointer from firmware), **kernel drawing** into a linear framebuffer, or **VGA text mode**—each of those *does* involve memory you map and pixels you control. Those are **out of scope** for the UART-only log used in OS-1–OS-8; they show up in later bring-up (see [OS-99-plan.md](OS-99-plan.md) roadmap) if you add a real console or shell on a framebuffer.

**Summary:** **`HELLO INIT` and `STAGE n:` lines appear in your terminal because the bytes reached a program that knows how to render text.** On QEMU that is almost always **QEMU → stdout → your terminal emulator**. On real metal it is **UART → cable/USB → serial receiver → same idea.**

In this tutorial:

- [`src-os/common/serial.hpp`](src-os/common/serial.hpp) implements the UART driver using port I/O (`outb`/`inb`) and polls UART status before sending each character.
- Both the UEFI app and the kernel include and use the same `serial.hpp`, so the serial “truth log” continues across the UEFI -> kernel boundary.

### Is COM1 only for **output**? Is `serial` mandatory?

**In this codebase, yes—serial is used only for outgoing bytes (logging).** The header exposes **`init_115200`**, **`write_char`**, and **`write_str`**; there is **no** `read_char` or receive path. The UART could receive data (and raise IRQs via `IER`), but this tutorial **never reads** from COM1 for input, shell, or protocol—it only **transmits** ASCII to the host (QEMU `-serial stdio` or a physical serial cable).

| Question | Answer |
| --- | --- |
| **Needed only for messages?** | **Yes**, here: COM1 is your **observability** channel (`UEFI: …`, `HELLO INIT`, `STAGE n: …`). It is not used to load the kernel, configure paging, or drive interrupts. |
| **Can you drop `serial` if you don’t want output?** | **You can remove** `serial::init_115200` and every `write_*` call and still have a **valid** boot and kernel run—there is **no architectural requirement** that a PC or QEMU execute UART code for the OS to work. You would lose **easy** confirmation of progress. |
| **Is it “absolutely important”?** | **Not for correctness of the machine model** (CPU, memory, IRQs work without it). **Yes for this book’s workflow**: milestones are proven by **serial markers**; without them you need another debug path (**GDB** with QEMU `-s`, **QEMU `debugcon`**, **in-memory ring buffer**, **UEFI GOP** text, etc.). |

So: **serial is optional for a minimal OS**, **essential for the tutorial’s teachable trace** unless you replace it with another visibility mechanism.

### What `outb` / `inb` are, and what a **port** is

In [`serial.hpp`](src-os/common/serial.hpp) you will see:

```cpp
static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
```

| Term | Meaning |
| --- | --- |
| **`outb`** | **“OUT byte.”** An **x86** machine instruction: write an **8-bit value** to an **I/O port**. The GNU assembler / Clang inline-assembly mnemonic is `outb` (AT&T syntax). It is **not** universal across all CPU architectures—ARM/RISC-V use different mechanisms (e.g. **MMIO**: load/store to physical addresses). On x86, **port I/O** is a **separate address space** from **RAM** (you do not “store” to `0x3F8` in memory; you **`outb`** to port `0x3F8`). |
| **`inb`** | **“IN byte.”** Read an 8-bit value **from** a port (used in the same header to poll the UART line status before sending). |
| **`port` (parameter)** | A **16-bit I/O port number** on the x86 ISA. It is **not** looked up at runtime from a registry: **hardware** (or the **chipset/QEMU model**) decodes the port number and routes the access to the correct device (UART, PIC, PIT, …). |
| **Where port values come from** | **Convention + documentation.** For **COM1**, the **base** port **`0x3F8`** is the classic IBM PC assignment (UART registers at `0x3F8` … `0x3FF`). You find it in **UART16550 datasheets**, **Intel/AMD system programming manuals** (I/O instruction summary), and OSDev wiki pages (e.g. [Serial ports](https://wiki.osdev.org/Serial_Ports)). The code defines **`COM1_BASE = 0x3F8`** and offsets like **`REG_THR = COM1_BASE + 0`**. |

**Summary:** `outb` is the **standard x86 way** (in this ISA) to talk to legacy devices behind **I/O ports**. The **`port`** argument is **which** device register you are selecting; **`0x3F8`** is fixed by **PC hardware compatibility**, not by a C++ constant you “discover” at runtime unless you parse ACPI/PCI for non-standard boards.

### COM1: what values `REG_THR` and `REG_LSR` are (and where offsets come from)

In [`serial.hpp`](src-os/common/serial.hpp):

```cpp
static constexpr uint16_t COM1_BASE = 0x3F8;
static constexpr uint16_t REG_THR = COM1_BASE + 0;
static constexpr uint16_t REG_LSR = COM1_BASE + 5;
```

**Numeric results (compile-time integer math):**

| Name | Expression | Result (hex) | Decimal (for intuition) |
| --- | --- | --- | --- |
| **`REG_THR`** | `0x3F8 + 0` | **`0x3F8`** | 1016 |
| **`REG_LSR`** | `0x3F8 + 5` | **`0x3FD`** | 1021 |

**How you get it:** ordinary **unsigned hex addition**. `0x3F8` is the **base I/O port** for COM1; **`+ 0`** and **`+ 5`** are **fixed offsets in bytes** from that base to specific **UART registers** defined by the **8250/16550** programming model. The CPU does not compute a formula at runtime beyond what the compiler folds into constants—**`constexpr`** means these names are just readable aliases for the final port numbers **`0x3F8`** and **`0x3FD`**.

**Where the register map is documented:**

- **Device level:** **UART 16550** (or **8250**) **datasheet / programmer’s guide** — lists each register as an offset from the chip’s base (DLAB bit changes meaning of some ports; this code follows the usual layout).
- **OS bring-up:** [OSDev — Serial ports](https://wiki.osdev.org/Serial_Ports) (table of offsets: RBR/THR @ +0, LSR @ +5, LCR @ +3, …).
- **PC mapping:** IBM PC compatibles map **COM1** to base **`0x3F8`**, **COM2** to **`0x2F8`**, etc.—so “COM1” **is** “UART at `0x3F8`” for legacy I/O.

**Meaning in one line:** **`REG_THR`** is the **transmit holding** port (write a character to send); **`REG_LSR`** is the **line status** port (read bit 5 to see if the transmitter can accept another byte).

### `write_char`: why **`0x20`** selects **bit 5**, and why **THR** can be “full”

From [`serial.hpp`](src-os/common/serial.hpp):

```cpp
static inline void write_char(char c) {
    while ((inb(REG_LSR) & 0x20u) == 0) {
        asm volatile("pause");
    }
    outb(REG_THR, static_cast<uint8_t>(c));
}
```

**From `'H'` to the byte in `outb`:** The parameter **`c`** has type **`char`**. In C++, **character literals are already integers**: a narrow literal like **`'H'`** has type **`char`**, and its **value** is the **numeric code** for that character in the **execution character set** (on typical PC toolchains, basic Latin letters use **ASCII**, so **`'H'`** is **72** = **`0x48`**). There is no separate “symbol” type in the executable—only bytes. When **`write_str`** loops over **`"Hello"`**, each **`write_char(*s)`** passes a **`char`** that is **already** one of those small integer values (e.g. **72, 101, 108, …**). **`static_cast<uint8_t>(c)`** does **not** run a special runtime step that “converts letters to ASCII”; it only **adjusts the type** to **unsigned 8-bit** (same bit pattern for normal ASCII range) so **`outb`** receives a **`uint8_t`**. The UART hardware sees **one 8-bit value** per **`outb`**; what your **terminal** shows as text is **host-side** interpretation of those bytes.

**Who maps `serial::write_char('H')` in [`init.cpp`](src-os/kernel/init.cpp)?** The **compiler** does, **when it compiles** that file—not the UART driver and not a “font” in the kernel. Each call passes a **`char`** that is **already** the chosen code value; for **`'H'`** … **`'T'`** and **`'\n'`**, the compiler emits the same rule as for any other literal. Your **OS does not contain an ASCII lookup table** for those letters; the numeric values are **fixed in the object code** (you can confirm with **`objdump -d`** on the kernel: you will see **immediate** operands or register loads matching **0x48** for **`'H'`**, etc., on an ASCII execution character set). Changing the **source** to **`write_char(72)`** would send the **same** byte to the UART if **72** is the code for **`'H'`** in your execution character set.

**Why `0x20` means “bit 5”:** In hexadecimal, **`0x20 = 32 = 2^5`**. As an 8-bit mask that is **`0b0010_0000`**: only **one** bit is set, at position **5** when bits are numbered **0** (least significant) through **7**. The expression **`inb(REG_LSR) & 0x20u`** keeps **only** that bit; the loop runs while it is **0**. On the **16550**, **LSR bit 5** is **THRE** — *Transmitter Holding Register Empty* (wording in datasheet: transmitter can accept another byte). When bit 5 is **1**, it is safe to **`outb(REG_THR, …)`** again.

**Why can THR “not be empty”?** The UART does **not** send all bits instantly. It shifts data out on the wire at the **baud rate** (each byte is several bit-times). Until the **holding register / transmit path** is ready for the **next** CPU write, **THRE** stays **0** and you must **wait**. That is **normal**: the **serial line is slower than the CPU**, not because the CPU is “wrong,” but because **async serial is slow** compared to instruction execution.

**Does CPU microcode “make it empty”?** **No.** **Emptying** the THR and advancing transmission is done by the **UART chip’s own logic** (shift registers, baud-rate generator, state machine). The CPU only **polls** **LSR** and **writes** **THR**. The **`pause`** instruction is a **hint** to the CPU (reduce power / improve ordering in tight spin loops on x86); it does **not** talk to the UART.

### `write_str`: what `for (; s && *s; ++s)` means

```cpp
static inline void write_str(const char* s) {
    for (; s && *s; ++s) {
        write_char(*s);
    }
}
```

A `for` loop has three parts: **`for (init; condition; step)`**. Here the **init** is **empty** (`;` right away): the pointer **`s`** is already the function argument—there is nothing to initialize before the first test.

| Part | Code | Role |
| --- | --- | --- |
| **Condition** | **`s && *s`** | Keep looping only while both are true: **`s`** is not a **null pointer**, and **`*s`** (the current character) is not **`'\0'`**. |
| **Step** | **`++s`** | Move the pointer to the **next** character in the string. |

**Why `s && *s`?**

- **`s`** — If the caller passed **`nullptr`**, dereferencing would be **undefined behavior**. The test **`s`** is false for **`nullptr`**, so the loop body **never runs** and we never evaluate **`*s`** (short-circuit **&&**).
- **`*s`** — C strings end with a **NUL** terminator **`'\0'`** (numeric **0**). When `*s` is **`'\0'`**, the condition is false and the loop **stops** after the last real character—**without** sending the terminator byte to the UART (usually you do **not** want a binary **0** on the serial log as a visible character).

Equivalent style: **`while (s && *s) { write_char(*s++); }`**.

### `init_115200()`: why those hex values (they are **bit patterns**, not a magic “enable” opcode)

The UART is controlled by **hardware register bit fields**. Each `outb(port, value)` sends an **8-bit mask**; “enable” usually means **setting specific bits to 1** according to the **16550** documentation—not a single abstract “on” switch.

```cpp
static inline void init_115200() {
    outb(REG_IER, 0x00);
    outb(REG_LCR, 0x80);
    outb(REG_DLL, 0x10);
    outb(REG_DLM, 0x00);
    outb(REG_LCR, 0x03);
    outb(REG_FCR, 0x07);
}
```

| Step | Register | Value | Bits (what it means) |
| --- | --- | --- | --- |
| 1 | **IER** | `0x00` | All interrupt-enable bits **cleared** (bits 0–3 = 0): no RX-ready, TX-empty, line-status, or modem-status IRQs. **Polling-only** use matches `write_char` (which spins on **LSR**). |
| 2 | **LCR** | `0x80` | **`0x80 = 0b1000_0000`**: bit **7** (**DLAB**) = **1**. When DLAB=1, ports **`+0` and `+1`** are **DLL** / **DLM** (baud **divisor**), not THR/RBR and IER. |
| 3 | **DLL / DLM** | `0x10`, `0x00` | 16-bit divisor **N = 0x0010 = 16** (little-endian: low byte first). Baud rate is **f_UART / (16 × N)** where **f_UART** is the UART’s reference clock (often **1.8432 MHz** on classic PC COM hardware). **Other divisors** give other baud rates (e.g. with **1.8432 MHz**, **N = 1** → **115200** baud; **N = 12** → **9600** baud). **Note:** the usual textbook value for **115200** at **1.8432 MHz** is **DLL = `0x01`**, DLM = `0x00`. This tree uses **DLL = `0x10`** (N = 16); on strict **1.8432 MHz** silicon that would be **7200** baud—**QEMU** `-serial stdio` still shows output, but on **physical UARTs** set **DLL/DLM** to match your **crystal** and desired baud. |
| 4 | **LCR** | `0x03` | **`0x03 = 0b0000_0011`**: DLAB=**0** (back to normal THR/IER mapping). Bits **0–1** = **11** → **8 data bits**. Bit **2** = **0** → **1 stop bit**. Bits **3–4** = **00** → **no parity**. That is **8N1** framing. |
| 5 | **FCR** | `0x07` | **`0x07 = 0b0000_0111`**: bit **0** = **1** → **FIFOs enabled**; bits **1–2** = **1** → **clear** receive and transmit FIFOs (self-clearing strobes in hardware). |

So nothing is “named `ENABLE` in hex”; each value is a **mask** chosen so the UART’s **control logic** enters the desired mode (no IRQs, divisor programmed, 8N1, FIFO on).

**What other values can you use? (typical alternatives)**

- **LCR** (when DLAB=0): change **word length** (5/6/7/8 bits), **stop bits** (1 or 2), **parity** (even/odd/stick/mark/none) by different bit combinations—see 16550 datasheet **Line Control Register** table.
- **IER**: set bits **0–3** to **1** to enable interrupts (e.g. `0x01` = “data available”) if you switch from polling to IRQ-driven I/O.
- **FCR**: `0x00` = FIFOs **off** (8250-like); other values change **RX trigger level** (bits 6–7) on 16550A+.
- **Baud**: any **16-bit** divisor via DLL/DLM (while DLAB=1); pick **N** from your **clock** and target baud.

**Authoritative references:** UART **16550** family datasheet (register/bit definitions)—local copy: [`doc/uart/PC16550D.pdf`](doc/uart/PC16550D.pdf) with index [`doc/uart/README.md`](doc/uart/README.md); [OSDev serial](https://wiki.osdev.org/Serial_Ports); Intel’s Super I/O / LPC documentation on PC COM ports.

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

### What “firmware” means in `SystemTable` (`firmwareVendor`, `firmwareRevision`, …)

In [`src-os/uefi/efi_main.cpp`](src-os/uefi/efi_main.cpp), **`struct SystemTable`** mirrors the **UEFI `EFI_SYSTEM_TABLE`** layout (only the fields we need are declared). Here **“firmware”** means **platform firmware**, not your kernel:

| Field | Meaning |
| --- | --- |
| **`firmwareVendor`** | Pointer to a **UTF-16** string naming **who built** the UEFI implementation (e.g. **EDK II / OVMF** in QEMU, or **AMI**, **Insyde**, etc. on a PC). |
| **`firmwareRevision`** | A **version number** for that firmware build. |
| **`boot` / `runtime`** | Pointers to **tables of function pointers** implemented **inside** the firmware (`BootServices`, `RuntimeServices`). When you call **`allocatePages`**, you are calling **code** that lives in the firmware’s image. |
| **`conIn` / `conOut` / `stdErr`** | Optional **protocol** objects for **text console** I/O (this tutorial mostly uses **serial** instead). |

So **“firmware”** = the **UEFI** program that runs **before** you exit boot services (in QEMU: **OVMF** in the flash image). It is **not** the OS kernel or a Linux driver—it is the **board/VM boot environment** that loads **`BOOTX64.EFI`** and passes **`systemTable`**.

### Where `boot` is “defined”

`boot` is **not** a free variable in the project; it is a **member** of **`struct SystemTable`** in [`src-os/uefi/efi_main.cpp`](src-os/uefi/efi_main.cpp):

```cpp
struct SystemTable : Table {
    // … firmwareVendor, console protocols, runtime …
    BootServices* boot;
};
```

The **C++ type** (`BootServices*`) is defined in the same file (the `struct BootServices` layout with `allocatePages`, `getMemoryMap`, `exitBootServices`, …). The **pointer value** inside **`systemTable->boot`** is **not** assigned in your source: **UEFI firmware** builds the real **`EFI_SYSTEM_TABLE`** in memory and passes **`systemTable`** into **`efi_main`**. So **`boot`** points to the firmware’s **Boot Services** function table (in RAM), which you then use like **`bs->allocatePages(...)`**.

### Where `allocatePages` / `getMemoryMap` are **implemented** (not in `src-os/`)

They are **not** defined in this tutorial tree. **`bs->allocatePages`** and **`bs->getMemoryMap`** are **function pointers** to code **inside platform firmware**:

| What you have in the repo | What it is |
| --- | --- |
| **`using AllocatePagesFn = …`** and **`struct BootServices`** | Your **minimal** description of the table layout so the C++ compiler can **call** through the pointer. |
| **Actual machine code** for **`AllocatePages`**, **`GetMemoryMap`**, **`ExitBootServices`**, … | Linked into **OVMF** (QEMU) or the board vendor’s UEFI image (**AMI**, **Insyde**, …). |

**Where to read real source (open firmware):**

- **EDK II** (Tianocore) — the open-source UEFI implementation **OVMF** is built from. Boot service implementations live under packages such as **`MdeModulePkg`** (e.g. memory services). Starting points people use when grepping the tree: search for **`CoreAllocatePages`** / **`AllocatePages`** and **`GetMemoryMap`** in the EDK II GitHub: [https://github.com/tianocore/edk2](https://github.com/tianocore/edk2).
- **Locally:** only if you clone **edk2** and build OVMF yourself; there is **no** `.c` file for these inside **`os_book/src-os/`**.

So: **definition** = firmware image; **your project** only **calls** them through **`systemTable->boot`**.

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

### What `kernelPhys` and `stackPhys` **mean** (the unsigned integer values)

In [`efi_main.cpp`](src-os/uefi/efi_main.cpp) both are **`uint64_t`** (not C **`unsigned int`**): they store **physical addresses** in **bytes**, as **non-negative** integers.

| Variable | Role |
| --- | --- |
| **`kernelPhys`** | After **`allocatePages`**, the **base physical address** of the **first** page of the allocation that will hold the **copied kernel** (aligned to a **4 KiB** boundary). **`0`** before the call is only a placeholder—firmware **overwrites** it via **`&kernelPhys`**. |
| **`stackPhys`** | Same idea: **base physical address** of the **stack** allocation (**16 KiB** in this file). Used with **`stackTop = stackPhys + …`** so **`RSP`** points into that region. |

**Why unsigned / wide integer?** **Addresses** are not negative; on **x86_64** you need enough bits for **64-bit physical** addresses (even if hardware uses fewer bits in practice). The value is **not** a “small counter”—it is **where in RAM** those pages live (e.g. `0x12345000`), until firmware assigns it.

**Not virtual memory:** at this stage in the tutorial you are still using **identity-style** or firmware mappings; **`kernelPhys`** is the **physical** base you copy to and later jump to.

Line-by-line explanation:

1. `kernelPhys = 0;`: will be filled with the physical base address returned by firmware.
2. `kernelPages = .../4096`: computes how many 4KiB pages are needed for the raw kernel blob.
3. `if (kernelPages == 0) kernelPages = 1;`: defensive clamp (shouldn’t happen, but avoids 0-page allocations).
4. `allocatePages(... ANY_PAGES ...)`: requests physical memory.
5. `MemoryType::LOADER_CODE`: marks the allocation as code-like for the memory map contract.
6. `&kernelPhys`: output parameter receives the physical base.
7. Copy loop: writes each byte of the kernel raw blob (`kernel_blob[i]`) into RAM at `kernelPhys + i`.

### If you change `AllocateType` and/or `MemoryType`

UEFI **`AllocatePages`** is specified in the **UEFI spec** (Boot Services). The tutorial uses:

- **`AllocateType::ANY_PAGES`** — firmware may place the region at **any** suitable physical address; **`kernelPhys`** is an **output** (you pass **`&kernelPhys`**, initially `0`).

| `AllocateType` (typical) | Effect if you switch |
| --- | --- |
| **`ANY_PAGES`** | Easiest: “give me *N* pages somewhere.” |
| **`MAX_ADDRESS`** | Allocate **below** a limit: the **`Memory`** parameter is also used as **input** (max physical address); firmware picks a base **≤** that. Wrong setup → **`EFI_INVALID_PARAMETER`** or failure. |
| **`ADDRESS`** | Allocate at an **exact** physical address: **`Memory`** must be the **desired base on entry**. If those pages are not free or the address is invalid → **`EFI_NOT_FOUND`** / **`EFI_OUT_OF_RESOURCES`**. |

**`MemoryType`** does **not** usually change *whether* you can store bytes there—it tells firmware how to **label** the region in the **memory map** (`GetMemoryMap`) and documents **intent** (code vs data vs firmware-owned, …).

| `MemoryType` | Effect if you change the kernel allocation |
| --- | --- |
| **`LOADER_CODE`** | **Correct intent** for memory that will hold **executable** kernel bytes. |
| **`LOADER_DATA`** | Still **allocates RAM**; the map will say **data**, not **code**. Your kernel may still run if you jump there, but you have **mis-labeled** executable memory—confusing for debugging or for any later component that trusts the memory map for **NX/W^X**-style decisions. |
| **`BOOT_SERVICES_*` / `RUNTIME_*` / `CONVENTIONAL_MEMORY`** | **Wrong** for normal loader allocations: may **fail** (`EFI_INVALID_PARAMETER`) or produce **inconsistent** maps—**avoid** unless you know exactly what your firmware allows. |

**Practical rule:** keep **`ANY_PAGES`** unless you need a **fixed** physical layout; keep **`LOADER_CODE`** for the kernel image and **`LOADER_DATA`** for stack / mmap scratch, as in this file.

In step 4 we have the explanation of memory map contract.

C++ note: what `reinterpret_cast<unsigned char*>(kernelPhys)` does

- `kernelPhys` is a physical address (an integer).
- `reinterpret_cast<unsigned char*>(kernelPhys)` tells the compiler: “treat that address as a pointer to a byte array”.
- Once that cast exists, `dst[i] = ...` becomes a normal “write one byte at address (kernelPhys + i)” operation.

Deeper explanation:

UEFI loads your kernel as a *raw binary* by copying bytes into allocated physical pages. After `ExitBootServices`, the kernel code is just bytes in RAM; there’s no further “loading” step from firmware.

### `EFIAPI`, Microsoft x64 vs Linux ABI, and where `kernel_blob` lives

**Why firmware calls use the Microsoft x64 ABI (`EFIAPI`)**

In [`src-os/uefi/efi_main.cpp`](src-os/uefi/efi_main.cpp) you will see:

```cpp
#define EFIAPI __attribute__((ms_abi))
```

The **UEFI specification** (Volume 2, platform bindings) for **x86_64** defines that **EFI boot services and runtime services** use the **Microsoft x64 calling convention** — **not** the **System V AMD64** ABI used by typical **Linux ELF** userspace.

- **`EFIAPI`** on **`efi_main`** and on **function pointer types** (`AllocatePagesFn`, `GetMemoryMapFn`, …) tells Clang to generate **`call`** sequences that match what **firmware** expects (register args, shadow space rules, etc.).
- **This project** builds the UEFI app with **`--target=x86_64-windows-gnu`**, which already matches the **PE/COFF + MS ABI** world for the whole UEFI image.
- **When you call through `bs->allocatePages`**, the **callee** is firmware code; the pointer type must be **`EFIAPI*`** so the **call site** matches the ABI of the table in the **SystemTable**.

**Is there a “Linux-specific ABI” here?** **Not for UEFI entry.** Linux **userspace** on x86_64 normally uses **SysV AMD64** (different from MS). The **kernel** you jump to (`kernel_entry`) is documented in this tutorial as using **`extern "C"`** and the **handoff asm** sets **`rdi`…`r9`** — that is the **SysV** argument convention. So you **cross an ABI boundary** at the **`call *entry`** after `ExitBootServices`: **UEFI = MS**, **your kernel** = **SysV** (by convention in this tree).

**Where is the kernel blob before `allocatePages`?**

- **`kernel_blob[]`** comes from **`kernel_blob.inc`**: it is **static data** linked **inside** **`BOOTX64.EFI`** (a **`.data` / `.rdata` style** region in the PE image — **not** the runtime stack).
- **After** `allocatePages`, the **copy loop** writes those bytes into **freshly allocated** pages at **`kernelPhys`** (`MemoryType::LOADER_CODE`). That RAM is **where execution eventually runs** (`reinterpret_cast` kernel entry to `kernelPhys`). The **blob in the PE file** is only the **source**; the **running** kernel is the **copy** in that allocated range.

**Are there many “stacks” in the machine?**

**Yes.** There is **no single global stack** for the whole computer. Typical examples:

- **Firmware / UEFI** uses its own stacks during boot.
- **Your** EFI app uses whatever stack the firmware set when it called **`efi_main`**.
- This **`allocatePages`** block allocates a **separate** **16 KiB** region (`LOADER_DATA`) and **`mov` to `RSP`** before calling the kernel — that becomes **the kernel’s** stack for the tutorial.
- Later, **other threads** or **interrupt stacks** would add **more** stacks.

So **stacks are regions** you choose; **not** one vendor-defined “the stack” for all RAM.

**How large can the stack be?**

In this file **`stackPages = 4`** → **16 KiB** (fixed). In general there is **no single maximum** in the spec: **as large as** `allocatePages` can succeed with available **conventional** memory, **firmware limits**, and your **safety** margin. **Too small** → overflow; **too large** → wastes boot-time RAM. **64 KiB–1 MiB** is common for early kernels; **this tutorial keeps it tiny**.

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

### `stackPhys` vs `stackTop` (physical base vs physical top)

Both refer to the **same** contiguous stack region in RAM; they name **opposite ends**:

| Name | Typical meaning here | Role |
| --- | --- | --- |
| **`stackPhys`** | **Low** physical address: start of the allocation returned by **`allocatePages`** (first byte of the first stack page). | Identifies **where the block begins**; passed to **`kernel_entry`** in **`r9`** so the kernel knows the extent of the stack region. |
| **`stackTop`** | **High** physical address: end of the region ( **`stackPhys + stackPages × 4096`**, then 16-byte aligned). | **x86_64 stacks grow toward lower addresses** (`push` / `call` decrease **`rsp`**). **`rsp`** must start at the **high** end so later operations stay **inside** `[stackPhys, stackTop)` and do not run off the **bottom**. |

So: **base = bottom of the allocation**; **top = where `rsp` starts** before the first **`push`**. This is **not** FIFO vs LIFO—the mmap buffer is unrelated; the stack is a **LIFO** structure in **how the CPU uses `rsp`**, not in how the region was allocated.

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

### What `GetMemoryMap` actually does

**Firmware** keeps an internal description of **physical memory**: which ranges are usable **conventional RAM**, which are **reserved**, **firmware code/data**, **ACPI reclaim**, **MMIO**, and so on. **`GetMemoryMap`** copies that picture into **your** buffer as an array of **`EFI_MEMORY_DESCRIPTOR`** records (type, start address, page count, attributes—see the struct in [`efi_main.cpp`](src-os/uefi/efi_main.cpp)). Your **kernel** later uses that array to build page tables without mapping over firmware or reserved regions.

**What is a memory map descriptor?** One **`EFI_MEMORY_DESCRIPTOR`** is one record for one **contiguous physical range**. In this tutorial struct, each descriptor includes:

- **`Type`**: what kind of region it is (usable RAM, reserved, ACPI, MMIO, etc.).
- **`PhysicalStart`**: first physical address of that region.
- **`NumberOfPages`**: region length in 4 KiB pages.
- **`Attribute`**: firmware-defined flags/properties for that region.

So the full memory map is not one giant struct; it is an **array** of descriptors laid out back-to-back in the buffer returned by `GetMemoryMap`.

Why pages? Are they “hardware cells” on the memory board?

- **Pages are mainly an OS/CPU-MMU management unit**, not a literal motherboard construction unit.
- On x86_64, 4 KiB is the standard base page size used by page tables and by UEFI `NumberOfPages`.
- Page granularity lets firmware and kernels describe ranges, set permissions, isolate code/data, and allocate/free memory in predictable chunks.
- Physical DRAM is internally organized as channels/ranks/banks/rows/columns/cells; those are controller/electrical details. A software page is an **address-space abstraction** over that hardware, not a fixed permanent “cell block”.

### Why the locals look like “only zeros”

The parameters are **in/out**. Initializing **`mmapSize`**, **`mapKey`**, **`descSize`**, and **`descVersion`** to **zero** does **not** mean the map is empty or useless—it means:

- **`mmapSize = 0`** together with **`nullptr`** for the map pointer: *“I have **no** descriptor buffer yet; tell me how many **bytes** I must allocate.”* The firmware typically returns **`EFI_BUFFER_TOO_SMALL`** and, per the UEFI spec, updates **`*mmapSize`** to the **required buffer size** (and usually sets **`*descSize`** and **`*descVersion`** so you know each descriptor’s size and format). **`mapKey`** may also be updated on this probe call depending on the implementation.

So after the **first** call, those variables are **no longer** “just zero” where it matters: **`mmapSize`** in particular holds the **size** you use in Step 5 to allocate **`mmapPages`**.

The **second** call (`GetMemoryMap(fill)`) passes a **real pointer** **`mmapPtr`**; firmware **writes** the full array of descriptors into that memory and updates **`mmapSize`**, **`mapKey`**, etc. to match the **filled** snapshot. **`ExitBootServices(imageHandle, mapKey)`** then checks that this **`mapKey`** still matches that snapshot—hence the two-call pattern.

**Summary:** Zeros are the **starting** state for a **size probe**; the **firmware** fills in sizes and, on the second call, the **map contents**. The tutorial’s `(void)` on the return value ignores success/failure codes; production code should check **`EFI_STATUS`** (e.g. **`EFI_BUFFER_TOO_SMALL`** on the probe).

What is `UINTN`?

In UEFI, `UINTN` is an **unsigned integer type whose width matches the platform’s native word size** (pointer-size).

What does “UEFI shim” mean?

- **`shim` is not an abbreviation.** It is a normal English engineering word meaning a **thin adapter layer** placed between two systems.
- In this project, the UEFI shim is [`src-os/uefi/efi_main.cpp`](src-os/uefi/efi_main.cpp): a small bridge between **UEFI firmware world** and **your kernel world**.
- It exists because those worlds have different contracts (entrypoint/calling convention, table types, boot-service APIs, and handoff data). The shim translates that boundary once, then jumps to `kernel_entry`.
- Concretely here, the shim: initializes serial, allocates pages for kernel/stack/map buffer, gets the memory map + `mapKey`, calls `ExitBootServices`, sets the kernel stack/register arguments, and transfers control.

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

### Who says `KernelEntry` must have exactly six arguments — and can you change it?

**UEFI does not.** After **`ExitBootServices`**, firmware does not define how your kernel entry is called. The **six** parameters are **this project’s contract** between the shim ([`efi_main.cpp`](src-os/uefi/efi_main.cpp)) and the kernel ([`kernel_entry` in `init.cpp`](src-os/kernel/init.cpp)): mmap location/size, descriptor size, kernel image location/size, stack base.

**Why six fits naturally on x86_64:** With **`extern "C"`** and the **System V AMD64** calling convention, the **first six** integer/pointer arguments are passed in **`rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`** — exactly the registers set by the **`mov`** instructions above. So “six scalars” is a **convenient** match (no stack arguments for this handoff). The **meaning** of each argument (which is an address vs a size) is **not** fixed by the CPU; it is whatever **`kernel_entry`** declares and the shim loads.

**Can you change it?** **Yes**, but you must update **together**:

- the **`kernel_entry(...)`** prototype in **`init.cpp`**,
- the **`KernelEntry`** typedef and the **`asm`** block in **`efi_main.cpp`** (register assignments and any extra setup),
- and any tables in this chapter that describe the handoff.

If you need **more than six** integer/pointer parameters under SysV, **seventh and later** arguments are passed **on the stack** according to the ABI — you would extend the handoff or pass **one pointer** to a struct in **`rdi`** instead. Fewer than six is fine if both sides agree.

### Are these “constants,” “memory layout,” or **addresses**?

The **`mov`** instructions only load **64-bit integers** into **`rsp`** and **`rdi`…`r9`**. The CPU does **not** know whether a value is “an address,” “a size,” or “a constant”—it is all just bits. The **meaning** comes from the **SysV AMD64 handoff contract** (and from your C types in `KernelEntry`).

| Operand | Typical meaning in this tutorial | Address? |
| --- | --- | --- |
| **`stackTop`** (`st` → **`rsp`**) | **Physical** address of the **top** of the stack region (aligned). **`rsp`** must hold a valid stack pointer value. | **Yes** (points into RAM). |
| **`mmapPhys`** → **`rdi`** | **Physical** base of the UEFI memory-map **descriptor buffer**. | **Yes**. |
| **`mmapSize`** → **`rsi`** | **Byte length** of the filled descriptor array (from **`GetMemoryMap`**), **not** an address. | **No** (scalar size). |
| **`descSize`** → **`rdx`** | **Size of one** `EFI_MEMORY_DESCRIPTOR` in bytes—**not** an address. | **No** (scalar). |
| **`kernelPhys`** → **`rcx`** | **Physical** base where the **kernel image** was copied. | **Yes**. |
| **`kernelPages`** → **`r8`** | **Page count** for the kernel allocation—**not** an address. | **No** (integer). |
| **`stackPhys`** → **`r9`** | **Physical** base of the **stack** allocation. | **Yes**. |
| **`entry`** → **`call`**) | **Address of the first instruction** to run in the kernel image. Here **`reinterpret_cast<KernelEntry>(kernelPhys)`** uses the **same numeric value** as **`kernelPhys`** because the kernel is linked at **VMA 0** and the entry is at offset 0. | **Yes** (code location; equals **`kernelPhys`** in this layout). |

So you are **not** “defining the whole memory layout” in one asm block—you are **setting the stack pointer** and passing **six scalar arguments** (some **physical addresses**, some **sizes/counts**) per the **calling convention**. A disassembler listing only shows **`mov reg, reg`** / **`mov reg, imm`** after register allocation; it cannot tell “this is a size” unless you read the **C** side and the **`kernel_entry`** prototype.

**Does assembly show “constant” vs “address”?** **No special opcode:** every value might have been computed in C (`stackTop = stackPhys + …`). In the final asm you only see **register-to-register** moves of whatever values the compiler put in **`r`** operands.

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

### What `for (;;)` means, and why a **loop** (not one `hlt`)

**`for (;;)`** is C/C++ idiom for an **infinite loop**: there is **no** `init`, `condition` is always **true** (empty `;`), **no** `step` — same as **`while (true)`** or **`for (; true; )`**.

| Piece | Why it is there |
| --- | --- |
| **`cli`** | Clears **IF** in **`RFLAGS`** (maskable **external** interrupts off at the CPU). Reduces the chance the CPU wakes from **`hlt`** for ordinary IRQs. (**NMIs**, **SMI**, debug events can still break halt—firmware-dependent.) |
| **`hlt`** | **Halt** instruction: CPU stops executing until the next interrupt / reset-class event. Saves power vs spinning. |
| **`for (;;)` around it** | **`hlt` can return**: when the CPU resumes (e.g. **NMI**, or **IF** got set again by something), execution **continues at the next instruction**. The **loop** sends the CPU **back** to **`cli; hlt`** so you never “fall through” past the halt block into **whatever bytes follow** in memory (undefined behavior in a minimal kernel). |

**If you wrote only** `asm volatile("cli; hlt");` **once (no loop):** after any **return** from **`hlt`**, the function would hit the **closing `}`** of **`kernel_entry`** and **“return”** — with **no** valid return address for a freestanding kernel entry, that is **not** a defined shutdown path. The **infinite loop** is the usual **“park the CPU here forever”** pattern.

Line-by-line explanation:

1. `extern "C" ... kernel_entry()`: predictable entry symbol, matches how the loader calls it.
2. `serial::init_115200();`: initializes COM1 on the kernel side as well.
3. `write_hello_init();`: prints `HELLO INIT`.
4. `for (;;) { cli; hlt }`: infinite halt loop after printing.

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

