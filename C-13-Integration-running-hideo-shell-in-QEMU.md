# C-13 — Integration: Running `hideo-shell` in QEMU

## Goal of this chapter
This is the final “stack integration test” for the course:

- bootloader (`opstart`) starts
- kernel (`Hjert`) initializes memory + scheduler + loads init from bootfs
- user runtime (`karm-sys`) starts and connects to services
- the chosen shell UI appears

Even if you have read all the code in previous chapters, integration bugs still happen. This chapter gives students a repeatable workflow to:

1. Build and run in QEMU.
2. Identify the last successful milestone from logs.
3. Map the failure back to the correct subsystem (loader vs init vs IPC vs user runtime).

## Build prerequisites (from Skift)
Skift’s repo documents toolchain requirements in:

- [`skift_sources/skift/doc/building.md`](skift_sources/skift/doc/building.md)

For Arch Linux, it includes:

- `clang`, `llvm`
- `nasm`
- `ninja`
- `qemu-full`
- `gptfdisk`, `mtools`
- `sdl2`

## Run command (recommended)
From `os_book/skift_sources/`:

```sh
make run-hideo-shell-release
```

`skift_sources/Makefile` invokes the Skift repo’s QEMU launch logic, and the QEMU command line is constructed by:

- [`skift_sources/skift/meta/plugins/start/runner.py`](skift_sources/skift/meta/plugins/start/runner.py)

## What to expect: boot milestones (log-driven)
When everything works, you should observe a rough ordering:

1. `opstart` starts, loads `file:/loader.json`
2. `opstart` loads the kernel ELF via Vaerk and prepares handover payload records
3. `Hjert` entry runs:
   - registers panic handler
   - validates handover payload
   - initializes memory allocator (`initMem`) and scheduler (`initSched`)
4. Kernel enters `enterUserspace(...)`:
   - maps init ELF programs into a new user `Space`
   - maps handover payload + stack
   - creates first user `Task` and enqueues it
5. User runtime enters `karm-sys`:
   - `Abi::SysV::init()`
   - sets hooks and IPC connection
   - async loop begins, and the shell starts.

If the UI does not appear, the “last milestone” is your guide:

- If you stop before `Hjert` init logs => handover payload/loader mapping issue.
- If you stop after `initMem/initSched` => init ELF mapping or task creation issue.
- If you stop in user runtime / IPC => capability/syscall/async waiting issues.

## Where QEMU debug configuration lives
Skift’s runner can enable QEMU logging and debug attach:

- `-d int,guest_errors,cpu_reset` for interrupt/guest error logging (when enabled in runner)
- `-s -S` to pause CPU at start for a debugger attach (when enabled in runner)

These are wired in:
- [`skift_sources/skift/meta/plugins/start/runner.py`](skift_sources/skift/meta/plugins/start/runner.py)

## Minimal “debug questions” students should ask
When something fails, students should narrow it down by answering:

- Did the loader successfully parse `loader.json` and select an entry?
- Did `Hjert.Core::init` validate the handover payload?
- Did the scheduler enqueue init (did you see the “enter userspace” stage)?
- Did the user runtime start async waiting (did `waitFor` resolve events)?

## Next step
Chapter `90` describes enhancement ideas students can turn into mini-projects:

- extend syscalls and object types
- add better filesystem/bootfs integration
- improve the VMM/page fault path

