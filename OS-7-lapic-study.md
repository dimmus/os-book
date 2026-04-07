# OS-7 — Stage 4 Local APIC timer (`src-os/` tutorial tree)

This page walks through **milestone 4** after [OS-6](OS-6-irq-pic-study.md): **stop using the PIC/PIT path** for ticks, **map the Local APIC MMIO page**, enable the APIC in the **Spurious Interrupt Vector Register (SVR)**, arm the **APIC timer** in **periodic** mode on a **new IDT vector (34)**, acknowledge interrupts with **LAPIC EOI** (MMIO write), and prove the path on **COM1**.

We extend the boot story:

`… -> STAGE 3: done -> STAGE 4: begin -> PIC fully masked -> LAPIC MMIO -> SVR -> timer armed -> sti -> three LAPIC ticks -> mask LVT -> STAGE 4: done -> cli; hlt`

## What we had before (after OS-6)

1. **PIC** remapped to vectors **32–47**, **PIT** on **IRQ0**, **IDT[32]** → **`isr_irq32`**, **PIC EOI** (`outb(0x20, 0x20)`).
2. Stage 1 paging maps **kernel**, **stack**, **mmap** scratch, and **upper-half aliases** — but **not** device MMIO unless we add it.

The **Local APIC** is controlled through **memory-mapped registers** at the **default physical base `0xFEE00000`** (first 4KiB of the APIC window). Firmware may not list that range as “conventional RAM” in the UEFI memory map, so **your** page tables must map it explicitly if you run with **`CR3`** pointing at your tables.

## Concepts: APIC, LAPIC, vectors, IDT gates, EOIs, SVR, and MMIO

This section ties together terms used above. Where we already defined something in an earlier chapter, you get a **short reminder** and a **link**—read those pages when a term is new.

### Interrupt **vector** (0–255)

A **vector** is the **index** the CPU uses to choose an **IDT** entry when an event occurs (exception, hardware interrupt, or `int n`).

**Reminder:** [OS-5](OS-5-idt-study.md) explains the **IDT** as a table in memory and why **vector 3** is #BP. **Rule of thumb:** vectors **0–31** are mostly **CPU exceptions**; **32–255** are available for **device IRQs** and custom uses once controllers are programmed (your PIC remap in OS-6 starts external IRQs at **32**).

### **IDT gate** (descriptor)

An **IDT gate** is one **16-byte** (64-bit mode) descriptor that tells the CPU: handler **RIP** (split across fields), **code segment selector**, **gate type** (e.g. interrupt vs trap), **IST**, etc. **`lidt`** loads the IDT **base + limit**; each **vector** indexes one gate.

**Reminder:** trap vs interrupt gate types (`0x8F` vs `0x8E`) and **`IF`** behavior are discussed under *Trap gate vs interrupt gate* in [OS-5](OS-5-idt-study.md). OS-6/OS-7 use **`0x8E`** for IRQ/timer paths so the handler runs with **`IF`** cleared automatically.

### **APIC** vs legacy **PIC**

- **APIC** (**Advanced Programmable Interrupt Controller**) is Intel’s newer interrupt architecture: per-CPU **Local APIC**, system **I/O APIC** for routing external IRQs, and (on modern systems) **MSI/MSI-X** from PCI devices. It scales to **SMP** and avoids the limitations of a single shared 8259 pair.
- The legacy **8259 PIC** (**Programmable Interrupt Controller**) is the **two-cascade** chip pair you programmed in [OS-6](OS-6-irq-pic-study.md) (**`pic_remap_and_mask`**, **IRQ0** = PIT). QEMU’s **PC** model still exposes it; OS-7 **masks it fully** so timer delivery for this milestone goes through the **LAPIC** instead.

### **Local APIC (LAPIC)**

The **Local APIC** is the **per-logical-processor** interrupt controller built into the processor. It receives interrupts from the **system bus** (including those forwarded from an **I/O APIC** or from other LAPICs), delivers them to the core using an **IDT vector**, and provides **local** facilities: **timer**, **performance counters**, **error** interrupt, **spurious** vector handling, **IPI**s, etc.

In this tutorial you only need: **enable** the LAPIC (**SVR**), program the **APIC timer** (**LVT** + divide + initial count), and **EOI** correctly.

### **I/O APIC** (not used in OS-7)

The **I/O APIC** sits between **external IRQ lines** (or their modern equivalents) and the **LAPIC**s. OS-7 does **not** program it; external devices are a **later** topic (see *Next milestone hint* at the end of this file).

### **APIC MMIO** (memory-mapped LAPIC)

**APIC MMIO** means the LAPIC’s control registers appear as **memory-mapped I/O**: a **physical page** (default **`0xFEE00000`**) contains **32-bit** register windows at fixed **offsets** (e.g. **EOI `0xB0`**, **SVR `0xF0`**, **LVT timer `0x320`**). Loads/stores go to **MMIO**, not DRAM.

Because you use **your own page tables** ([OS-4](OS-4-paging-study.md)), that page must be **mapped** under **`CR3`** or any access **#PF**s—same idea as mapping device MMIO in a real kernel.

### **x2APIC** vs **xAPIC** (MMIO)

- **xAPIC** (this chapter): LAPIC is accessed via **MMIO** at the base from **IA32_APIC_BASE** MSR (often **`0xFEE00000`**).
- **x2APIC**: LAPIC can be accessed via **MSRs** instead (no MMIO decode for most registers), useful when the physical window is awkward or for virtualization. OS-7 stays on **MMIO** for QEMU clarity.

### **SVR** — Spurious Interrupt Vector Register

The **SVR** (offset **`0xF0`** from the LAPIC base) does two jobs at once:

1. **Bit 8 — APIC Software Enable:** must be **set** so the local APIC is **on** and timer/interrupt delivery behaves as documented.
2. **Bits 7:0 — spurious vector:** vector used if the CPU must take a **spurious interrupt** (a documented edge case). This tutorial writes **`0x1FF`**: enable bit + vector **`0xFF`** for the low byte.

Until the LAPIC is enabled via **SVR**, relying on the **APIC timer** is unreliable.

### **Local APIC timer**

The **LAPIC timer** is a **per-CPU** countdown/timer driven from the **bus clock** (through a **divide** register). It does **not** use the **PIT** (`0x40–0x43`); you program **LVT Timer** (which **vector** to deliver, **one-shot** vs **periodic**, **mask**), **initial count**, and optionally **divide**.

**Why use it after OS-6?** OS-6 proved IRQs using **PIT → PIC → vector 32**. OS-7 proves you can move the **time source** to the **modern** path: **LAPIC timer → vector 34**, with **LAPIC EOI**, which is closer to what bare-metal kernels do on APIC-based systems.

### **LVT** (Local Vector Table)

The **LVT** is a group of **LAPIC registers**—one per **local interrupt source** (timer, **LINT0/1** pins, **error**, **performance monitor**, etc.). Each entry selects the **IDT vector** to deliver, whether the entry is **masked**, and (for the timer) **one-shot** vs **periodic**, etc. This chapter only programs **LVT Timer** (`0x320`).

### **PIC EOI** (End of Interrupt, 8259)

**PIC EOI** tells the **8259** “this IRQ is finished.” For the **master**, that is typically **`outb(0x20, 0x20)`** on command port **`0x20`**; if the IRQ came from the **slave** PIC, software must **EOI both** (slave first, then master)—see [OS-6](OS-6-irq-pic-study.md) and Skift’s `DualPic::ack`.

**Purpose:** the PIC will not assert the **same** line again (or will re-arm edge logic) in a well-defined way. **Wrong or missing EOI** → stuck or repeated bogus IRQs.

### **LAPIC EOI**

**LAPIC EOI** acknowledges the interrupt **inside the Local APIC**: write **`0`** to the **EOI** register (**offset `0xB0`**). That clears the **in-service** state for the **highest-priority** interrupt being serviced (per Intel rules).

**Purpose:** same *idea* as PIC EOI—tell the controller the handler completed—but the **mechanism is MMIO**, not **`outb`**.

### Why mixing PIC EOI and LAPIC EOI is wrong

After you service an interrupt that was delivered **only** via the **LAPIC** (e.g. **APIC timer**), sending **PIC EOI** does **nothing useful** to the LAPIC (and vice versa). Always **EOI the unit that delivered the interrupt** to the core. OS-7’s timer uses **only** **LAPIC EOI**; the PIC is **masked** so the PIT path does not compete.

---

**Quick cross-reference**

| Term | Introduced / deep dive |
| --- | --- |
| IDT, vector, trap vs interrupt gate | [OS-5](OS-5-idt-study.md) |
| PIC remap, PIT, IRQ0, PIC EOI, vector 32 | [OS-6](OS-6-irq-pic-study.md) |
| Paging + MMIO mapping | [OS-4](OS-4-paging-study.md) |

## Stage 4 goal

Milestone 4 is:

1. **Map** physical page **`0xFEE00000`** (identity and upper-half alias, same pattern as Stage 1).
2. **Mask the PIC completely** (`0xFF` on both master and slave data ports) so **IRQ0** no longer delivers **vector 32** — the PIT may still run, but it does not reach the CPU through the PIC.
3. **Enable the local APIC** by setting **SVR** (`offset 0xF0`): **bit 8** = APIC software enable, **bits 7–0** = spurious vector (this tutorial uses **`0xFF`** combined as **`0x1FF`**).
4. Program the **APIC timer**:
   - **Divide Configuration Register** (`0x3E0`): **`3`** → divide by **16** (Intel encoding; see SDM “Divide Configuration Register”).
   - **LVT Timer** (`0x320`): **vector 34**, **periodic** (**bit 17**), **unmasked** (clear **bit 16**).
   - **Initial Count** (`0x380`): a **large** value so ticks are slow enough for serial (here **`0x80000`**; tune for your VM).
5. Install **IDT[34]** as a **64-bit interrupt gate** (`0x8E`) to **`kLinear(&isr_lapic_timer)`** (new assembly stub in `lapic_entry.S`).
6. **`sti`**, **`hlt`** until **three** interrupts, **`cli`**, then **mask** the LVT timer (**bit 16** in `0x320`) so the halt loop does not keep printing.
7. In **`lapic_timer_dispatch`**, send **LAPIC EOI**: write **`0`** to **`0xB0`** (not the PIC command port).

### PIC EOI vs LAPIC EOI

*(Longer explanations: **§ Concepts** — **PIC EOI** and **LAPIC EOI**.)*

| Source | Acknowledge |
| --- | --- |
| 8259 PIC (OS-6) | `outb(0x20, 0x20)` (master; slave first if needed) |
| Local APIC | MMIO write **`0`** to **EOI** at offset **`0xB0`** |

Mixing them is a common bug: after moving the timer to the LAPIC, **PIC EOI** is **wrong** for that interrupt.

### Why vector **34** (not **32**)

**32** was the **PIC IRQ0** vector after remap. Keeping **LAPIC** on **34** makes logs and debugging clearer: **32** = legacy path, **34** = **APIC timer** path. (You could reuse **32** once the PIC is fully masked; this chapter keeps them distinct.)

## Why each step is needed

Below, each item uses the same pattern: **Next we should …** (action) **for / to …** (goal) **— because without that, …** (what goes wrong).

### 1. Map the Local APIC MMIO page in Stage 1

**Next we should** add a **present, writable** mapping for physical page **`0xFEE00000`** (and the upper-half alias) **to** make **`lapic_mmio_read` / `lapic_mmio_write`** legal under **our** `CR3`.

**Because without that,** any load or store to LAPIC registers **page-faults** ([OS-4](OS-4-paging-study.md): the CPU only knows about addresses that appear in **your** page tables; MMIO is not “automatically” mapped).

### 2. Mask the PIC completely (master and slave)

**Next we should** write **`0xFF`** to both PIC **data** ports (**`0x21`** and **`0xA1`**) **to** block **all** legacy IRQ lines from the 8259 pair.

**Because without that,** **IRQ0** from the **PIT** can still be forwarded as **vector 32** ([OS-6](OS-6-irq-pic-study.md)). You would see **two** time sources at once (PIC path + LAPIC path), confusing logs, double counting, or spurious **vector 32** deliveries while you are trying to validate **vector 34**.

### 3. Enable the LAPIC (write **SVR** with bit 8 set)

**Next we should** set **SVR** so the **APIC Software Enable** bit is **on** **to** turn the **local APIC** into a fully specified, usable state for timer delivery.

**Because without that,** the LAPIC may not service the timer interrupt path reliably; behavior is undefined or “dead” depending on silicon/QEMU.

### 4. Set the timer **divide**, **LVT Timer**, and **Initial Count**

**Next we should** program **divide** (how fast the counter ticks), **LVT Timer** (which **vector**, **periodic** vs one-shot, **unmasked**), and **Initial Count** **to** produce periodic interrupts at a **rate** you can observe on serial.

**Because without that,** either no interrupt is requested (**masked LVT**, zero count, wrong mode), or interrupts fire too fast to read, or the wrong **vector** is selected so **IDT** dispatch fails or hits the wrong stub.

### 5. Install **IDT[34]** and link **`isr_lapic_timer`** (`lapic_entry.S`)

**Next we should** fill **IDT gate 34** with **`kLinear(&isr_lapic_timer)`** and keep the **SS** patch/`iretq` discipline from [OS-5](OS-5-idt-study.md) **to** give the CPU a valid **RIP** when the LAPIC delivers **vector 34**.

**Because without that,** delivery to vector 34 causes a **bad gate** (wrong handler address, unmapped code), **double fault**, or **`iretq`** failure if the exception frame or **SS** is inconsistent with the boot GDT.

### 6. **`sti`**, then **`hlt`** until three interrupts

**Next we should** set **`IF = 1`** and **sleep** in **`hlt`** **to** allow **external / local** interrupts to run while the main line is idle.

**Because without that,** interrupts are **masked at the CPU**; the LAPIC timer may still **signal**, but the core will **not** enter the handler (`sti` missing), or you **busy-spin** and never give a clean “wait for IRQ” story.

### 7. **LAPIC EOI** in `lapic_timer_dispatch` (write **`0`** to **`0xB0`**)

**Next we should** acknowledge the interrupt **in the LAPIC** **to** clear **in-service** state for that interrupt so **another** edge/periodic delivery can occur.

**Because without that,** the LAPIC can **stall** further timer interrupts (or behave as if the previous one never completed), so you stop seeing ticks after the first.

### 8. **`cli`**, then **mask** the LVT timer (optional but done here)

**Next we should** clear **`IF`** and set the **LVT Timer mask** bit **to** stop new timer IRQs before entering the final **`cli; hlt`** loop.

**Because without that,** **`hlt`** would wake on every tick forever and your **“STAGE 4: done”** line would be followed by endless **irq** prints (or the CPU would never rest quietly in the halt loop).

### 9. Final **`for (;;){ cli; hlt; }`**

**Next we should** stay in a **known-stopped** state **to** end the demo predictably after **`STAGE 4: done`**.

**Because without that,** execution would fall through into **undefined** code paths (there is nothing after Stage 4 in this minimal kernel).

### Optional: read LAPIC **version** register (`0x30`) once

**Next we may** read **VER** (and discard) **to** prove the **MMIO mapping** hits real LAPIC decode **before** relying on **SVR**/**timer** programming.

**Because without a working map,** that read **#PF**s first—giving an earlier failure than a mysterious timer hang.

## Where this is implemented

Files (under `src-os/kernel/` unless noted):

- `init.cpp` — **`kLapicMmioPhys`**, **`mapPageDual`** for the LAPIC page in **Stage 1**, **`pic_mask_all`**, **`lapic_mmio_read` / `lapic_mmio_write`**, Stage 4 serial markers, **`lapic_timer_dispatch`**, LVT mask after three ticks.
- `lapic_entry.S` — **`isr_lapic_timer`**: same GPR discipline and **saved `SS`** patch as OS-5/OS-6, calls **`lapic_timer_dispatch`**, **`iretq`**.
- `Makefile` — links **`lapic_entry.o`**.

## Changed and new files

| Change | Path |
| --- | --- |
| **New** | [`src-os/kernel/lapic_entry.S`](src-os/kernel/lapic_entry.S) — `isr_lapic_timer` |
| **Changed** | [`src-os/kernel/init.cpp`](src-os/kernel/init.cpp) — map `0xFEE00000` in Stage 1, `pic_mask_all`, LAPIC timer + IDT gate 34, **`kLapicMmioPhys`**, Stage 4 serial |
| **Changed** | [`src-os/kernel/Makefile`](src-os/kernel/Makefile) — link `lapic_entry.o` |

## Memory and CPU state snapshot (Stage 4)

**Depends on:** [OS-4](OS-4-paging-study.md) (**must** map LAPIC page under **`CR3`**), [OS-6](OS-6-irq-pic-study.md) (PIC fully masked so IRQ0 does not compete).

**What lives where:**

```text
  Physical 0xFEE00000  ──►  4 KiB LAPIC register window (MMIO, not DRAM)
       ▲
       │  PTE in your PT (identity + upper-half alias like other Stage 1 maps)
       │
  CR3 ──► PML4 → … → PTE[P=1] for LAPIC frame
```

**LAPIC registers (tutorial uses offsets from base):**

| Offset | Register | Role |
| --- | --- | --- |
| `0xF0` | **SVR** | APIC enable (bit 8) + spurious vector |
| `0x320` | **LVT Timer** | Vector **34**, periodic, unmask |
| `0x380` | **Initial Count** | Timer reload |
| `0x3E0` | **Divide** | Count rate |
| `0xB0` | **EOI** | Write **0** to ack interrupt |

**Interrupt path (timer):**

```text
LAPIC timer fires ──► local delivery ──► vector 34 ──► IDT[34] ──► isr_lapic_timer
                                                              │
                                                              ▼
                                                    lapic_timer_dispatch
                                                              │
                                                              ▼
                    LAPIC EOI: store 0 to MMIO [base+0xB0]   (NOT outb PIC)
                                                              │
                                                              ▼
                                                         iretq
```

**CPU registers:** **`CR3`** unchanged from Stage 1; **`IDTR`** gains gate **34**; **`RFLAGS.IF`** via **`sti`** for delivery. **PIC** mask **`0xFF`** on both controllers so **vector 32** path is idle.

Skift in this repo wires timers through its HAL init ([`arch.cpp`](src-os-skift/skift/src/kernel/hjert/x86_64/arch.cpp) uses **`DualPic`** + **`Pit`**); a production tree often adds **x2APIC** or **APIC MMIO** drivers elsewhere. This tutorial stays with **MMIO at `0xFEE00000`**, which matches **QEMU’s default PC** model well enough for teaching.

## Step 0: map the LAPIC page in Stage 1

Without this mapping, any **`mov`**/`store` to **`0xFEE00000`** under your **`CR3`** will **#PF**.

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
static constexpr uint64_t kLapicMmioPhys = 0xFEE00000ull;
// … after other identity mappings:
mapPageDual(kLapicMmioPhys, kLapicMmioPhys, PTE_WRITABLE);
// … inside mapUpperHalfAliases:
mapPageDual(kLapicMmioPhys + UPPER_HALF, kLapicMmioPhys, PTE_WRITABLE);
```

#### Why consult **IA32_APIC_BASE** (`MSR 0x1B`)?

The tutorial hard-codes **`kLapicMmioPhys = 0xFEE00000`** because **QEMU’s PC model** matches that **default** for the **BSP**. On a **physical** machine, firmware may still use that base for the **local APIC**, but the **authoritative** place the CPU records “where is the LAPIC MMIO window?” is **not** a fixed constant in your source—it is **MSR `0x1B`**, **`IA32_APIC_BASE`**.

- **What it is:** A **64-bit model-specific register** read with **`rdmsr`** / written with **`wrmsr`** (ring 0). It tells you the **physical base address** of the **APIC register block** (the page you must map for **xAPIC MMIO** access) and control bits such as **APIC global enable** and (on later CPUs) **x2APIC** enable.
- **Why that matters for you:** Your **`mapPageDual(..., kLapicMmioPhys, ...)`** must map the **same** physical page the CPU is decoding for LAPIC. If the MSR’s base field were ever **not** `0xFEE00000`, mapping only `0xFEE00000` would **#PF** or touch the **wrong** device.
- **What we skip in `src-os/`:** Reading **`0x1B`** at runtime and parsing the base from bits **51:12** (see Intel SDM). For the tutorial, assuming **`0xFEE00000`** is enough in QEMU; for **bare metal**, add **`rdmsr`** early and derive **`kLapicMmioPhys`** from the MSR.

#### Where to find the definition (search examples)

Use these **queries** in the **Intel® 64 and IA-32 Architectures Software Developer’s Manual** (PDF or HTML from [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)):

| Search for | Where it usually appears |
| --- | --- |
| `IA32_APIC_BASE` | **Volume 4** (*Model-Specific Registers*), MSR listings; also **Volume 3A** (*System Programming Guide*), chapter on the **Local APIC** / APIC overview. |
| `MSR 01BH` or `01Bh` | Same MSR tables (hex **`0x1B`**). |

On **[OSDev Wiki](https://wiki.osdev.org/)**, search:

- **`IA32_APIC_BASE`** — short article on the MSR and typical field layout.
- **`APIC`** or **`Local APIC`** — overview; follow links to MMIO base.

#### Example: read the MSR on Linux (host PC)

If you want to **see** the value on real hardware (requires **`msr`** module and root):

```bash
sudo modprobe msr
sudo rdmsr 0x1b
```

`rdmsr` prints **EDX:EAX** (64-bit value). Decode the **APIC base** field using the **Intel SDM** bit diagram for **`IA32_APIC_BASE`** (physical address of the **4KiB**-aligned MMIO window). On many **BSP** systems the result matches **`0xFEE00000`**; if it does not, your kernel must map **that** physical page, not the tutorial constant.

**QEMU:** you can still run `rdmsr` **inside** the guest if you expose MSRs appropriately; for the tutorial kernel, trusting **`0xFEE00000`** matches typical BSP behavior.

## Step 1: mask the PIC

### Code (excerpt)

```cpp
static void pic_mask_all() {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}
```

Call this **before** arming the LAPIC timer so you do not receive **spurious** legacy IRQs on **vector 32** while testing **vector 34**.

## Step 2: MMIO accessors

APIC registers are **32-bit** locations at **fixed offsets** from the base. This tutorial uses simple **`volatile uint32_t*`** loads/stores.

```cpp
static inline void lapic_mmio_write(uintptr_t reg, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(kLapicMmioPhys + reg) = value;
}
```

### What the one-liner means

1. **`kLapicMmioPhys + reg`** — **Integer** addition: the **physical address** of the **LAPIC base** plus the **register offset** (e.g. **`0xF0`** for SVR). This is the **physical address** of one 32-bit register window. (We already mapped that page in Stage 1, so this address is also a **valid linear** address in our identity mapping.)

2. **`reinterpret_cast<volatile uint32_t*>(…)`** — Treat that address as a **pointer** to **`uint32_t`**, marked **`volatile`**. **`reinterpret_cast`** is required because **`kLapicMmioPhys + reg`** has no inherent type (it is just a number); we **assert** “this bit pattern is the address of a 32-bit device register.”

3. **`*… = value`** — **Store** a 32-bit value to that address. The CPU performs a **memory write** to **MMIO**: the chipset/LAPIC decodes the address and updates the register—**not** a normal RAM write.

4. **`volatile`** — Tells the compiler: **every** read/write to this location must **actually happen** in **program order** relative to other **`volatile`** accesses, and must not be **eliminated** (e.g. “you already wrote 5 earlier”) or **cached in a CPU register** as if it were ordinary RAM. **MMIO side effects** (interrupt controller state) depend on those exact stores happening.

Without **`volatile`**, an optimizing compiler might **merge**, **drop**, or **reorder** accesses to what looks like “just memory,” breaking hardware programming sequences.

### Why “simple `volatile uint32_t*`”

- **Width:** In **xAPIC MMIO** mode, these LAPIC control registers are specified for **32-bit** aligned access; **`uint32_t`** matches that contract.
- **No wrapper:** One pointer cast + store—easy to read in a tutorial.
- **Single CPU / early boot:** We do not discuss **cache coherency with other cores**, **DMA**, or **lock** bus cycles here.

### What a “not simple” / production-style approach often adds

| Topic | Why it gets more complex than this snippet |
| --- | --- |
| **Memory type / caching** | MMIO regions must be **uncached** (or strongly ordered) in the **CPU’s view** (MTRRs, PAT, or equivalent). Wrong attributes → **garbage reads**, **missing writes**, or **device mis-behavior**. The tutorial assumes your mapping behaves like **UC** / WC as firmware left it; a real kernel **pins** MMIO with the right PAT/MTRR setup. |
| **Barriers and ordering** | On **SMP**, you may need **explicit fences** or **lock**-prefixed instructions so APIC writes are visible to **other CPUs** or ordered w.r.t. **IPI**s. `volatile` is **not** a full SMP memory model; Linux-style `writel`/`readl` often include **mb()** / **wmb()** where the architecture requires it. |
| **Abstract MMIO helpers** | Kernels wrap `*(volatile uint32_t*)` in **`readl`/`writel`**, **`mmio_write32`**, or a **`struct`** with **`volatile uint32_t`** members at **fixed offsets**—same idea, but **one place** for endianness, barriers, and **tracing**. |
| **Virtual vs physical** | Here the **linear** address **equals** the **physical** identity mapping. Real kernels use a **kernel virtual** address (e.g. **`ioremap(phys)`**) and store **that** pointer—same `volatile` idea, different numeric address. |
| **x2APIC** | No MMIO pointer at all: access is via **`rdmsr`/`wrmsr`** on MSR numbers (see **§ Why consult IA32_APIC_BASE**). The “accessor” is **MSR read/write**, not `uint32_t*`. |
| **Error handling / paranoia** | Production code might **assert** alignment, **range-check** `reg`, or use **static** offset enums to avoid typos. |

So: **`*reinterpret_cast<volatile uint32_t*>(kLapicMmioPhys + reg) = value`** means “**perform one 32-bit MMIO store** to the LAPIC register at **base + offset**,” and **simple** means we skip the **full** kernel MMIO stack until you need **SMP**, **remapping**, or **x2APIC**.

## Step 3: SVR — enable the local APIC

```cpp
lapic_mmio_write(0xF0, 0x1FF);
```

Until **bit 8** is set, many LAPIC features (including sensible timer behavior) are not fully active.

## Step 4: timer divide, LVT, initial count

```cpp
lapic_mmio_write(0x3E0, 3);                     // divide by 16
lapic_mmio_write(0x320, 34u | (1u << 17));      // vector 34, periodic
lapic_mmio_write(0x380, 0x80000);               // initial count
```

**Periodic** (**bit 17**): when the current count reaches zero, it reloads from **Initial Count** and fires again.

## Step 5: IDT gate and assembly stub

Same rules as OS-5/OS-6: handler address **`kLinear(&isr_lapic_timer)`**, **`movw $0x10, 32(%rsp)`** before **`iretq`**.

## Step 6: EOI in C

```cpp
extern "C" void lapic_timer_dispatch(void) {
    lapic_mmio_write(0xB0, 0);
    // … bump counter, print …
}
```

## Step 7: mask the LVT when finished

```cpp
lapic_mmio_write(0x320, 1u << 16); // mask bit only
```

This stops periodic timer interrupts before the final **`cli; hlt`** loop (optional but keeps the serial log quiet).

## Order of operations in `kernel_entry` (Stage 4 only)

After **`write_stage3_done()`**:

1. **`write_stage4_begin()`**
2. **`pic_mask_all()`** → **`write_stage4_pic_masked()`**
3. **`lapic_mmio_read(0x30)`** (version; result discarded) → **`write_stage4_lapic_mmio()`**
4. **`lapic_mmio_write(SVR, 0x1FF)`** → **`write_stage4_svr()`**
5. Divide, LVT, Initial Count → **`write_stage4_timer_arm()`**
6. **`idtSetGate(..., 34, …, 0x8E)`**, **`idtLoad`**
7. **`write_stage4_sti()`**, **`g_lapic_irq_count = 0`**, **`sti`**, **`while (count < 3) hlt`**, **`cli`**
8. **Mask LVT**
9. **`write_stage4_done()`**
10. Final **`for (;;) { cli; hlt; }`**

## Expected serial output (QEMU)

After the OS-6 lines:

- `STAGE 4: begin`
- `STAGE 4: pic mask`
- `STAGE 4: lapic mmio`
- `STAGE 4: svr`
- `STAGE 4: timer`
- `STAGE 4: sti`
- `STAGE 4: irq 1`
- `STAGE 4: irq 2`
- `STAGE 4: irq 3`
- `STAGE 4: done`

## Troubleshooting

- **Hang after `STAGE 4: sti`**: LAPIC page **not mapped** (check **`kLapicMmioPhys`** mapping and **`CR3`**), **SVR** not enabled, **LVT** still masked, or **Initial Count** too large for your patience (lower **`0x80000`**).
- **Triple fault**: bad **IDT[34]** or missing **`SS`** patch in **`lapic_entry.S`** (same class of bug as OS-5).
- **Vectors on 32 instead of 34**: **PIC not fully masked** or spurious legacy path.
- **Stale image**: `make clean && make build` from **`src-os/`** so **`kernel_blob.inc`** matches **`kernel.raw`**.

## Next milestone hint

The `src-os/` tutorial continues with **[OS-8-page-fault-study.md](OS-8-page-fault-study.md)** (#PF, **`CR2`**, error-code stack, **`iretq`**). After that, **I/O APIC**, **MSI/MSI-X**, and **x2APIC** remain the natural hardware topics; the constant pieces are **IDT discipline**, **correct EOI**, **page tables for every access**, and **honest stack layouts per vector**.
