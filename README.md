# FreeRTOS on a Homemade RISC-V Emulator

A bare-metal FreeRTOS port running on a custom RV32IM emulator written in Rust — producing output **identical to QEMU**. This repo contains the firmware side: the test suite that validates the emulator's correctness.

```
========================================
  FreeRTOS Full Test Suite - RISC-V
========================================
[T01H] tick=50  high_cycles=...
[T02 ] tick=30  delay_300ms=1
[T04P] tick=40  ping=1
[T04G] tick=40  pong=1
...
[T20 ] tick=200 watchdog_beat=1
```

---

## What This Is

The emulator lives in a sibling Rust project (`../riscv`). This repo is the **firmware** that runs on top of it:

- FreeRTOS kernel (from the official [FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) repo)
- A bare-metal boot stub for RV32IM
- 20 tests covering essentially everything FreeRTOS does

If the output matches QEMU tick-for-tick, the emulator is correct. It does.

---

## Repository Layout

```
.
├── src/
│   ├── main.c            # 20-test suite (see below)
│   ├── boot.S            # Entry point: sets sp, mtvec, calls main
│   └── libc_minimal.c    # memset/memcpy/memcmp (no libc dependency)
├── include/
│   └── FreeRTOSConfig.h  # Kernel config (10 MHz, 100 Hz tick, CLINT)
├── link.ld               # Linker script (RAM @ 0x80000000, 32 MB)
├── Makefile
└── FreeRTOS-Kernel/      # Original upstream kernel source included in repo
```

---

## Test Suite

| ID   | What it validates |
|------|-------------------|
| T01  | Preemption by priority — high-prio busy loop starves low-prio |
| T02  | `vTaskDelay` — task wakes exactly after N ticks |
| T03  | `vTaskDelayUntil` — fixed period that doesn't drift |
| T04  | Binary semaphore ping/pong between two tasks |
| T05  | Counting semaphore — producer gives N tokens, consumer drains |
| T06  | Mutex — three tasks competing, only one inside at a time |
| T07  | Priority inheritance — LOW holds mutex, HIGH blocks, MED must not run |
| T08  | Queue producer/consumer with sequence verification |
| T09  | Queue receive with finite timeout |
| T10  | `vTaskSuspend` / `vTaskResume` — run count frozen during suspend |
| T11  | `vTaskPrioritySet` / `uxTaskPriorityGet` — live ready-list reorder |
| T12  | `vTaskDelete` — task runs exactly once and disappears |
| T13  | Software timer one-shot |
| T14  | Software timer auto-reload, stops after 4 fires |
| T15  | Software timer reset — fires only after resetter task stops |
| T16  | Heap stress — `pvPortMalloc`/`vPortFree` with out-of-order frees |
| T17  | Two independent queues — data must not cross between them |
| T18  | Tick counter monotonicity check |
| T19  | `xTaskGetCurrentTaskHandle` consistency across context switches |
| T20  | Watchdog — beats every 2 s, detects scheduler hangs |

---

## Memory Map

| Region | Address      | Size  |
|--------|-------------|-------|
| RAM    | `0x80000000` | 32 MB |
| CLINT  | `0x02000000` |       |
| UART   | `0x10000000` | 16550 |

Stack grows down from the top of RAM. FreeRTOS heap (heap_4) starts right after BSS.

---

## Building

### Prerequisites (WSL / Linux)

You need a RISC-V bare-metal GCC toolchain. On Ubuntu/Debian, the following packages are typically sufficient:

```bash
sudo apt update
sudo apt install gcc-riscv64-unknown-elf picolibc-riscv64-unknown-elf qemu-system-misc make
```

Verify:
```bash
riscv64-unknown-elf-gcc --version
qemu-system-riscv32 --version
```

### Build

```bash
make
```

### Run on the emulator

The emulator is a separate Rust project. From the emulator repo:

```bash
cargo run --release -- path/to/build/freertos.elf
```

Or adjust the `EMULATOR` variable in the Makefile to point to your setup and add a `run` recipe.

### Run on QEMU (reference)

```bash
qemu-system-riscv32 \
  -machine virt \
  -cpu rv32 \
  -nographic \
  -bios none \
  -kernel build/freertos.elf
```

> **Note:** QEMU's `virt` machine maps RAM to `0x80000000` and has a CLINT at `0x2000000` — matching the config here exactly.

---

## FreeRTOS Configuration Highlights

See `include/FreeRTOSConfig.h`:

| Parameter | Value | Reason |
|-----------|-------|--------|
| `configCPU_CLOCK_HZ` | 10 000 000 | Matches emulator's emulated clock |
| `configTICK_RATE_HZ` | 100 | 10 ms ticks |
| `configTOTAL_HEAP_SIZE` | 1 MB | Comfortably fits all 20 tests |
| `configUSE_MUTEXES` | 1 | Required for T06/T07 (priority inheritance) |
| `configUSE_TIMERS` | 1 | Required for T13/T14/T15 |
| CLINT MTIME | `0x0200BFF8` | Standard RISC-V CLINT layout |
| CLINT MTIMECMP | `0x02004000` | Standard RISC-V CLINT layout |

---

## Known Limitations / Notes

- **BSS not explicitly zeroed in boot.S** — relies on the emulator (and QEMU's `virt`) initializing RAM to zero. If porting to real hardware, add a BSS clear loop before `call main`.
- **No FPU** — `rv32im` only, no `f`/`d` extensions. Don't link floating-point code.
- **`make run`** just prints the ELF path — hooking it to the emulator is left to each person's local setup since the emulator lives outside this repo.
- **picolibc include path** is hardcoded in the Makefile (`/usr/lib/picolibc/riscv64-unknown-elf/include`). If your distro puts it elsewhere, adjust `CFLAGS` accordingly.

---

## Port Used

`FreeRTOS-Kernel/portable/GCC/RISC-V` with the `RISCV_MTIME_CLINT_no_extensions` chip extension (no compressed instructions, no FPU CSRs).

---

## License

FreeRTOS is MIT licensed. See `FreeRTOS-Kernel/LICENSE.md`. The files in `src/` and `include/` are also MIT licensed.