# ── Host detection ────────────────────────────────────────────────────────────
UNAME_S := $(shell uname -s)

ASM := nasm

ifeq ($(UNAME_S),Darwin)
    LLVM_PREFIX       := $(shell brew --prefix llvm 2>/dev/null)
    CC                := clang
    # ld.lld may live in the llvm keg or be installed separately via `brew install lld`
    LIN               := $(or $(wildcard $(LLVM_PREFIX)/bin/ld.lld),\
                              $(shell command -v ld.lld 2>/dev/null),\
                              ld.lld)
    ISO               := $(shell command -v mkisofs 2>/dev/null)
    HOST_CC           := clang
    # kernel + user programs must suppress SIMD to avoid Invalid Opcode (#6) in QEMU
    TARGET_ARCH_CFLAG := --target=i386-elf -m32 -march=i386 \
                         -mno-mmx -mno-sse -mno-sse2 -mno-avx -msoft-float
else
    CC                := gcc
    LIN               := ld
    ISO               := genisoimage
    HOST_CC           := gcc
    TARGET_ARCH_CFLAG := -m32
endif

# Optional per-machine overrides (local.mk is git-ignored)
-include local.mk

# ── Directories ───────────────────────────────────────────────────────────────
SOURCE_FOLDER := src
OUTPUT_FOLDER := bin
ISO_NAME      := OS2025
DISK_NAME     := storage

# ── Flags ─────────────────────────────────────────────────────────────────────
WARNING_CFLAG := -Wall -Wextra -Werror
DEBUG_CFLAG   := -fshort-wchar -g
# Compiler-only stripping flags (-nostartfiles/-nodefaultlibs are linker flags, moved to LFLAGS)
STRIP_CFLAG   := -nostdlib -fno-stack-protector -ffreestanding -fno-builtin
CFLAGS        := $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) \
                 $(TARGET_ARCH_CFLAG) -c -I$(SOURCE_FOLDER)
AFLAGS        := -f elf32 -g -F dwarf
LFLAGS        := -T $(SOURCE_FOLDER)/linker.ld -melf_i386

# ── Sources ───────────────────────────────────────────────────────────────────
# Every .c under src/ (any depth) is compiled into bin/<basename>.o.
# Adding a new source file needs no makefile change.
C_SOURCES := $(shell find $(SOURCE_FOLDER) -name '*.c')
C_OBJECTS := $(addprefix $(OUTPUT_FOLDER)/, $(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

# ── Phony targets ─────────────────────────────────────────────────────────────
.PHONY: all build run iso kernel disk clean check-tools

all: build
build: iso

run: all $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@qemu-system-i386 -s -S \
		-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
		-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

clean:
	rm -rf *.o *.iso \
		$(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*.d \
		$(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/kernel

# ── Toolchain check ───────────────────────────────────────────────────────────
check-tools:
	@for t in $(ASM) $(CC) $(LIN) $(ISO) qemu-system-i386; do \
	  command -v "$$t" >/dev/null 2>&1 || [ -x "$$t" ] || \
	    { echo "missing tool: '$$t'"; exit 1; }; \
	done; echo "toolchain OK"

# ── Disk image ────────────────────────────────────────────────────────────────
# Raw 4 MiB disk image attached to QEMU as an IDE HDD.
# Created on demand (make run), never wiped automatically.
disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

$(OUTPUT_FOLDER)/$(DISK_NAME).bin:
	@qemu-img create -f raw $@ 4M

# ── Compilation ───────────────────────────────────────────────────────────────
$(OUTPUT_FOLDER)/%.o: %.c
	@mkdir -p $(OUTPUT_FOLDER)
	@$(CC) $(CFLAGS) -MMD -MP $< -o $@

# Header dependencies (editing a .h triggers a rebuild)
-include $(C_OBJECTS:.o=.d)

# ── Link & assemble ───────────────────────────────────────────────────────────
kernel: $(C_OBJECTS)
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s \
		-o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/intsetup.s \
		-o $(OUTPUT_FOLDER)/intsetup.o
	@$(LIN) $(LFLAGS) $(OUTPUT_FOLDER)/*.o -o $(OUTPUT_FOLDER)/kernel
	@echo "Linking object files and generate elf32..."
	@rm -f *.o

# ── ISO ───────────────────────────────────────────────────────────────────────
iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel   $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1               $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst $(OUTPUT_FOLDER)/iso/boot/grub/
	@cd $(OUTPUT_FOLDER) && $(ISO) -R \
		-b boot/grub/grub1 \
		-no-emul-boot \
		-boot-load-size 4 \
		-A os \
		-input-charset utf8 \
		-quiet \
		-boot-info-table \
		-o $(ISO_NAME).iso \
		iso
	@rm -r $(OUTPUT_FOLDER)/iso/
