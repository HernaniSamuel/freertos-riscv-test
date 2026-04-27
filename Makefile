# -----------------------------------------------------------------------
# Makefile — FreeRTOS RV32IM bare-metal build
#
# Usage:
#   make          — build the ELF
#   make run      — build and run on the emulator
#   make clean    — remove build artifacts
#
# Requires:
#   - riscv64-unknown-elf-gcc in WSL PATH
#   - cargo project at ../riscv (the emulator)
# -----------------------------------------------------------------------

# -----------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------

CC      = riscv64-unknown-elf-gcc
OBJDUMP = riscv64-unknown-elf-objdump

# -----------------------------------------------------------------------
# Paths
# -----------------------------------------------------------------------

KERNEL_DIR  = FreeRTOS-Kernel
PORT_DIR    = $(KERNEL_DIR)/portable/GCC/RISC-V
CHIP_DIR    = $(PORT_DIR)/chip_specific_extensions/RISCV_MTIME_CLINT_no_extensions

EMULATOR    = bash -c "cd ../riscv && cargo run --release --"

BUILD_DIR   = build
TARGET      = $(BUILD_DIR)/freertos.elf

# -----------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------

SRCS_C = \
    src/main.c \
    src/libc_minimal.c \
    $(KERNEL_DIR)/tasks.c \
    $(KERNEL_DIR)/queue.c \
    $(KERNEL_DIR)/list.c \
    $(KERNEL_DIR)/timers.c \
    $(KERNEL_DIR)/event_groups.c \
    $(KERNEL_DIR)/stream_buffer.c \
    $(KERNEL_DIR)/portable/MemMang/heap_4.c \
    $(PORT_DIR)/port.c

SRCS_ASM = \
    src/boot.S \
    $(PORT_DIR)/portASM.S

OBJS = $(patsubst %.c,  $(BUILD_DIR)/%.o, $(SRCS_C)) \
       $(patsubst %.S,  $(BUILD_DIR)/%.o, $(SRCS_ASM))

# -----------------------------------------------------------------------
# Flags
# -----------------------------------------------------------------------

ARCH_FLAGS = -march=rv32im_zicsr_zifencei -mabi=ilp32

INCLUDES = \
    -I./include \
    -I$(KERNEL_DIR)/include \
    -I$(PORT_DIR) \
    -I$(CHIP_DIR)

CFLAGS = \
    $(ARCH_FLAGS) \
    $(INCLUDES) \
    -I/usr/lib/picolibc/riscv64-unknown-elf/include \
    -nostdlib \
    -ffunction-sections \
    -fdata-sections \
    -g

ASMFLAGS = \
    $(ARCH_FLAGS) \
    $(INCLUDES) \
    -g

LDFLAGS = \
    $(ARCH_FLAGS) \
    -nostdlib \
    -T link.ld \
    -Wl,--gc-sections \
    -lgcc

# -----------------------------------------------------------------------
# Rules
# -----------------------------------------------------------------------

.PHONY: all run clean disasm

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo ""
	@echo "Built: $@"

# C sources
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly sources
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASMFLAGS) -c $< -o $@

run: all
	@echo "ELF gerado em $(TARGET)"

disasm: all
	$(OBJDUMP) -d $(TARGET)

clean:
	rm -rf $(BUILD_DIR)