# Compiler & linker
ASM           = nasm
LIN           = ld
CC            = gcc

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage

KERNEL_FOLDER = $(SOURCE_FOLDER)/kernel
USER_FOLDER   = $(SOURCE_FOLDER)/user
USER_LIB      = $(USER_FOLDER)/lib
SHELL_FOLDER  = $(USER_FOLDER)/shell
CMD_FOLDER    = $(USER_FOLDER)/cmd
APPS_FOLDER   = $(USER_FOLDER)/apps
STRING_SRC    = $(SOURCE_FOLDER)/stdlib/string.c

# storage.bin = 4 MiB EXT2 + 4 MiB swap space (lihat src/header/memory/swap.h)
DISK_SIZE     = 8M

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(KERNEL_FOLDER)/linker.ld -melf_i386
QEMU_FLAGS    =

# Uji swap dengan RAM kecil tanpa mengubah kode: make run SYSTEM_MEMORY_MB=6
ifneq ($(SYSTEM_MEMORY_MB),)
CFLAGS       += -DSYSTEM_MEMORY_MB=$(SYSTEM_MEMORY_MB)
QEMU_FLAGS   += -m $(SYSTEM_MEMORY_MB)
endif

# Semua source kernel diambil otomatis dari src/kernel (subfolder cpu, driver, filesystem, memory, process)
KERNEL_C_SRC   = $(shell find $(KERNEL_FOLDER) -name '*.c') $(STRING_SRC)
KERNEL_ASM_SRC = $(shell find $(KERNEL_FOLDER) -name '*.s')

# Program user di src/user/apps (masing-masing satu file .c + crt0.s)
APP_LIST      = clock hello rhythm badapple
# Perintah terpisah (bonus 4.3.6) di src/user/cmd
CMD_LIST      = cat cp echo find grep ls mkdir mv rm clear help

.PHONY: run all build clean kernel iso disk inserter \
	user-shell user-hello user-clock user-rhythm user-badapple user-commands \
	insert-shell insert-hello insert-clock insert-bin insert-rhythm insert-commands insert-badapple

# 'disk' hanya membuat storage.bin jika belum ada (isi disk lama tidak ditimpa)
run: all disk insert-shell insert-hello insert-clock insert-bin insert-commands insert-rhythm insert-badapple
	@qemu-system-i386 -s -rtc base=localtime $(QEMU_FLAGS) \
		-audiodev pa,id=snd0 -machine pcspk-audiodev=snd0 \
		-drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk \
		-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
all: build
build: iso
clean:
	rm -rf *.o *.iso $(OUTPUT_FOLDER)

kernel:
	@mkdir -p $(OUTPUT_FOLDER)
	@rm -f $(OUTPUT_FOLDER)/*.o
	@for src in $(KERNEL_ASM_SRC); do \
		$(ASM) $(AFLAGS) $$src -o $(OUTPUT_FOLDER)/$$(basename $$src .s).o || exit 1; \
	done
	@for src in $(KERNEL_C_SRC); do \
		$(CC) $(CFLAGS) $$src -o $(OUTPUT_FOLDER)/$$(basename $$src .c).o || exit 1; \
	done
	@$(LIN) $(LFLAGS) $(OUTPUT_FOLDER)/*.o -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...
	@rm -f *.o

iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp other/menu.lst              $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage -R \
		-b boot/grub/grub1 \
		-no-emul-boot \
		-boot-load-size 4 \
		-A os \
		-input-charset utf8 \
		-quiet \
		-boot-info-table \
		-o $(OUTPUT_FOLDER)/$(ISO_NAME).iso \
		$(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/

# Buat storage.bin jika belum ada. Disk lama (4 MiB) diperbesar ke 8 MiB tanpa menghapus isi EXT2,
# karena 4 MiB terakhir dipakai sebagai swap space.
disk:
	@mkdir -p $(OUTPUT_FOLDER)
	@if [ ! -f $(OUTPUT_FOLDER)/$(DISK_NAME).bin ]; then \
		qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin $(DISK_SIZE); \
	else \
		truncate -s '>$(DISK_SIZE)' $(OUTPUT_FOLDER)/$(DISK_NAME).bin; \
	fi

inserter:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(CC) -Wno-builtin-declaration-mismatch -g -I$(SOURCE_FOLDER) \
		$(STRING_SRC) \
		$(KERNEL_FOLDER)/filesystem/ext2.c \
		$(SOURCE_FOLDER)/external/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

# ---------------------------------- shell ----------------------------------

user-shell:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(USER_LIB)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -fno-pie $(SHELL_FOLDER)/shell.c -o shell.o
	@$(CC)  $(CFLAGS) -fno-pie $(SHELL_FOLDER)/shell_cmd.c -o shell_cmd.o
	@$(CC)  $(CFLAGS) -fno-pie $(SHELL_FOLDER)/shell_path.c -o shell_path.o
	@$(CC)  $(CFLAGS) -fno-pie $(STRING_SRC) -o string.o
	@$(LIN) -T $(USER_LIB)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o shell.o shell_cmd.o shell_path.o string.o -o $(OUTPUT_FOLDER)/shell
	@echo Linking object shell object files and generate flat binary...
	@$(LIN) -T $(USER_LIB)/user-linker.ld -melf_i386 --oformat=elf32-i386 \
		crt0.o shell.o shell_cmd.o shell_path.o string.o -o $(OUTPUT_FOLDER)/shell_elf
	@size --target=binary $(OUTPUT_FOLDER)/shell
	@rm -f *.o

insert-shell: inserter user-shell
	@echo Inserting shell into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter -f shell 1 $(DISK_NAME).bin

# ----------------------------- aplikasi (apps) -----------------------------
# Pola yang sama untuk semua aplikasi: crt0.s + <app>.c + string.c -> bin/<app> (flat) dan bin/<app>_elf (debug)

define build_app
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(USER_LIB)/crt0.s -o crt0.o
	@$(CC)  $(CFLAGS) -fno-pie $(APPS_FOLDER)/$(1).c -o $(1).o
	@$(CC)  $(CFLAGS) -fno-pie $(STRING_SRC) -o string.o
	@$(LIN) -T $(USER_LIB)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o $(1).o string.o -o $(OUTPUT_FOLDER)/$(1)
	@$(LIN) -T $(USER_LIB)/user-linker.ld -melf_i386 --oformat=elf32-i386 \
		crt0.o $(1).o string.o -o $(OUTPUT_FOLDER)/$(1)_elf
	@echo Linking $(1) object files and generate flat binary...
	@size --target=binary $(OUTPUT_FOLDER)/$(1)
	@rm -f *.o
endef

user-hello:
	$(call build_app,hello)

user-clock:
	$(call build_app,clock)

user-rhythm:
	$(call build_app,rhythm)

user-badapple:
	$(call build_app,badapple)

insert-hello: inserter user-hello
	@echo Inserting hello into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter -f hello 1 $(DISK_NAME).bin

insert-clock: inserter user-clock
	@echo Inserting clock into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter -f clock 1 $(DISK_NAME).bin

insert-rhythm: inserter user-rhythm
	@echo Inserting rhythm into root directory...
	@cd $(OUTPUT_FOLDER); ./inserter -f rhythm 1 $(DISK_NAME).bin

insert-bin: inserter user-clock user-hello user-rhythm user-badapple
	@echo Inserting programs into /bin...
	@cd $(OUTPUT_FOLDER); ./inserter -d bin 1 $(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); for app in $(APP_LIST); do ./inserter -f $$app /bin $(DISK_NAME).bin || exit 1; done

# ------------------------- perintah terpisah (4.3.6) -------------------------

user-commands:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(USER_LIB)/crt0-cmd.s -o crt0-cmd.o
	@$(CC)  $(CFLAGS) -fno-pie $(USER_LIB)/cmd_runtime.c -o cmd_runtime.o
	@$(CC)  $(CFLAGS) -fno-pie $(SHELL_FOLDER)/shell_cmd.c -o shell_cmd.o
	@$(CC)  $(CFLAGS) -fno-pie $(STRING_SRC) -o string.o
	@for cmd in $(CMD_LIST); do \
		$(CC) $(CFLAGS) -fno-pie $(CMD_FOLDER)/$$cmd.c -o cmd-$$cmd.o || exit 1; \
		$(LIN) -T $(USER_LIB)/cmd-linker.ld -melf_i386 --oformat=binary \
			crt0-cmd.o cmd-$$cmd.o cmd_runtime.o shell_cmd.o string.o -o $(OUTPUT_FOLDER)/$$cmd || exit 1; \
		$(LIN) -T $(USER_LIB)/cmd-linker.ld -melf_i386 --oformat=elf32-i386 \
			crt0-cmd.o cmd-$$cmd.o cmd_runtime.o shell_cmd.o string.o -o $(OUTPUT_FOLDER)/$${cmd}_elf || exit 1; \
	done
	@echo Linking separated shell commands: $(CMD_LIST)
	@rm -f *.o

insert-commands: inserter user-commands insert-bin
	@echo Inserting separated shell commands into /bin...
	@cd $(OUTPUT_FOLDER); for cmd in $(CMD_LIST); do ./inserter -f $$cmd /bin $(DISK_NAME).bin || exit 1; done

# --------------------------- data video badapple ---------------------------

insert-badapple: inserter user-badapple
	@echo Splitting badapple.dat into 32768-byte ext2 chunks and inserting into /badapple...
	@rm -f $(OUTPUT_FOLDER)/ba*.dat
	@dd if=$(APPS_FOLDER)/badapple.dat of=$(OUTPUT_FOLDER)/ba1.dat bs=32768 count=1 status=none
	@dd if=$(APPS_FOLDER)/badapple.dat of=$(OUTPUT_FOLDER)/ba2.dat bs=32768 skip=1 count=1 status=none
	@dd if=$(APPS_FOLDER)/badapple.dat of=$(OUTPUT_FOLDER)/ba3.dat bs=32768 skip=2 count=1 status=none
	@dd if=$(APPS_FOLDER)/badapple.dat of=$(OUTPUT_FOLDER)/ba4.dat bs=32768 skip=3 count=1 status=none
	@dd if=$(APPS_FOLDER)/badapple.dat of=$(OUTPUT_FOLDER)/ba5.dat bs=32768 skip=4 count=1 status=none
	@cd $(OUTPUT_FOLDER) && ./inserter -d badapple 1 $(DISK_NAME).bin && \
	for f in ba1.dat ba2.dat ba3.dat ba4.dat ba5.dat; do \
		if [ -s $$f ]; then \
			./inserter -f $$f /badapple $(DISK_NAME).bin || exit 1; \
		fi; \
	done
