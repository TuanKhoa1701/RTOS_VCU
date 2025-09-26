# ===========================
# Project & toolchain
# ===========================
BUILDDIR      := Tools
TARGET_NAME   := os_test
TARGET        := $(BUILDDIR)/$(TARGET_NAME)

CROSS         := arm-none-eabi-
CC            := $(CROSS)gcc
AS            := $(CROSS)gcc
OBJCOPY       := $(CROSS)objcopy
OBJDUMP       := $(CROSS)objdump
SIZE          := $(CROSS)size

# ===========================
# MCU / CMSIS / SPL
# ===========================
CPUFLAGS      := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
DEFINES       := -DSTM32F10X_MD -DUSE_STDPERIPH_DRIVER
INCLUDES      := -ICMSIS -ISPL/inc -IOS/inc -IConfig -Iapp

# ===========================
# C/ASM/LD flags
# ===========================
CFLAGS_COMMON := -O2 -g -Wall -Wextra -Wno-unused-parameter \
                 -ffreestanding -fno-builtin \
                 -ffunction-sections -fdata-sections \
                 -MMD -MP
CFLAGS        := $(CPUFLAGS) $(DEFINES) $(INCLUDES) $(CFLAGS_COMMON)

ASFLAGS       := $(CPUFLAGS) $(DEFINES) $(INCLUDES) -x assembler-with-cpp

LDSCRIPT      := Linker/stm32f103.ld
LDFLAGS       := -T$(LDSCRIPT) -nostartfiles -nostdlib -static \
                 -Wl,--gc-sections -Wl,-Map=$(TARGET).map
# Nhẹ ký: dùng newlib-nano + stub syscalls "nosys"
LDFLAGS      += -specs=nano.specs -specs=nosys.specs
LDLIBS        := -Wl,--start-group -lc -lm -lgcc -Wl,--end-group

# ===========================
# Sources
# ===========================
SRCS_C := \
  main.c \
  app/App_Task.c \
  OS/src/os_kernel.c \
  OS/src/os_port.c \
  $(wildcard SPL/src/*.c)

SRCS_S := \
  Startup/startup_stm32f103.s \
  OS/src/os_port_asm.s

# ===========================
# Objects / Deps
# ===========================
OBJS_C := $(patsubst %.c,$(BUILDDIR)/%.o,$(SRCS_C))
OBJS_S := $(patsubst %.s,$(BUILDDIR)/%.o,$(SRCS_S))
OBJS   := $(OBJS_C) $(OBJS_S)
DEPS   := $(OBJS_C:.o=.d)

# Ensure build dir exists
$(shell mkdir -p $(BUILDDIR))

# ===========================
# Default goal
# ===========================
all: $(TARGET).bin size

# Compile C
$(BUILDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble .s (no preproc)
$(BUILDDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) $(CPUFLAGS) -c $< -o $@

# (Optional) Assemble .S (with preproc) — phòng khi bạn đổi đuôi file
$(BUILDDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# Link
$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(CPUFLAGS) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

# BIN/HEX/MAP
$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

size: $(TARGET).elf
	$(SIZE) --format=berkeley $<

# Disasm listing
list: $(TARGET).elf
	$(OBJDUMP) -d -S $< > $(TARGET).list

.PHONY: flash
flash: $(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program $(TARGET).elf verify reset exit"
clean:
	rm -rf $(BUILDDIR) $(TARGET).elf $(TARGET).bin $(TARGET).hex $(TARGET).map $(TARGET).list

.PHONY: all clean flash size list
-include $(DEPS)
