# UART documentation (8250 / 16450 / 16550 family)

This folder holds **vendor-style** material for the same UART family used by PC **COM** ports and by [`src-os/common/serial.hpp`](../../src-os/common/serial.hpp).

## What is a UART?

**UART** stands for **Universal Asynchronous Receiver/Transmitter**. It is a **digital circuit** (today usually a small chip or a block inside a Super I/O / SoC) that:

- **Serializes** bytes from the CPU into a **timed bit stream** on a wire (often **TX** / **RX** plus ground), and **deserializes** incoming bits back into bytes.
- Is **asynchronous**: sender and receiver agree on **baud rate** and **framing** (data bits, parity, stop bits)—there is no separate clock line in the minimal two-wire setup.
- Is **universal** in the sense of a **general-purpose** async serial engine, as opposed to a protocol-specific controller.

The PC’s legacy **COM** ports are driven by a **UART** compatible with the **8250 → 16450 → 16550** line; the OS talks to it via **I/O ports** (`outb`/`inb` on x86), not by memory-mapped framebuffer. See also [OS-1 — UEFI boot and serial](../../OS-1-HelloInit-UEFI-boot-and-serial-study.md).

## Included in this repo

| File | Description | Source |
| --- | --- | --- |
| **`PC16550D.pdf`** | **National Semiconductor PC16550D** — Universal Asynchronous Receiver/Transmitter **with FIFOs** (full register descriptions, electricals, timing). | Mirrored from the Stanford Pintos course materials: `https://www.scs.stanford.edu/10wi-cs140/pintos/specs/pc16550d.pdf` (retrieved for offline use; copyright remains with the original publisher). |

The **16550** is the usual reference for “PC serial port” programming today: it is **backward compatible** at the register level with the older **8250** and **16450** for the core registers (THR, RBR, IER, IIR, LCR, MCR, LSR, MSR, scratch). **FIFO control (FCR)** and **enhanced** features appear with the 16550; the **8250** had **no** transmit/receive FIFOs.

## 8250 and 16450 — why there is no second PDF here

Original **Intel 8250** and **16450** datasheets are often **not** redistributed on a stable public HTTPS URL suitable for automated mirroring. For the **same register programming model** as in this tutorial, use:

- **`PC16550D.pdf` in this folder** — treat **chapter / register tables** as authoritative for **bit definitions** on COM1 (`0x3F8` base).
- **8250 / 16450** — core layout matches the first registers; differences are mainly **FIFO absence** (8250/8250A) and **bugs** fixed in later stepping (historical).

## Official / stable URLs (download locally if you need a second copy)

| Document | Typical location |
| --- | --- |
| **TI / National PC16550D** (same family as above) | Texas Instruments product folder: search **PC16550D** or **SNLS378** on [ti.com](https://www.ti.com/) (may require a browser; some CDNs block plain `curl`). |
| **Programming guides (text, not chip PDFs)** | [OSDev — Serial Ports](https://wiki.osdev.org/Serial_Ports), [Wikibooks — Serial Programming / 8250 UART](https://en.wikibooks.org/wiki/Serial_Programming/8250_UART_Programming). |

## See also (book)

- [OS-1 — UEFI boot and serial](../../OS-1-HelloInit-UEFI-boot-and-serial-study.md) — `outb`/`inb`, COM1 base, `init_115200()`.
