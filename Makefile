CROSS ?= x86_64-elf-
CC := $(CROSS)gcc
SHELL := /bin/bash

CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-m64 -mno-red-zone -mcmodel=kernel -mgeneral-regs-only \
	-Wall -Wextra -O2 -Ikernel/include

ASFLAGS := -ffreestanding -fno-pic -fno-pie -m64 -mno-red-zone
LDFLAGS := -T linker.ld -nostdlib -static -z max-page-size=0x1000 -no-pie -Wl,--build-id=none

RUST_DIR := rust
RUST_LIB := $(RUST_DIR)/target/x86_64-unknown-none/release/librustos.a

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
ISO_ROOT := $(BUILD_DIR)/iso
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ISO_IMAGE := $(BUILD_DIR)/waluos.iso
SCRIPT_DIR := scripts

C_SRCS := \
	kernel/src/core/kernel.c \
	kernel/src/core/console.c \
	kernel/src/core/machine.c \
	kernel/src/core/pmm.c \
	kernel/src/core/vmm.c \
	kernel/src/core/pic.c \
	kernel/src/core/pit.c \
	kernel/src/core/keyboard.c \
	kernel/src/core/tty.c \
	kernel/src/core/pty.c \
	kernel/src/core/session.c \
	kernel/src/core/editor.c \
	kernel/src/core/fs.c \
	kernel/src/core/storage.c \
	kernel/src/core/video.c \
	kernel/src/core/font8x8.c \
	kernel/src/core/shell.c \
	kernel/src/arch/x86_64/idt.c \
	kernel/src/lib/string.c

ASM_SRCS := \
	kernel/src/arch/x86_64/boot.S

OBJS := $(patsubst kernel/src/%, $(OBJ_DIR)/%, $(C_SRCS:.c=.o) $(ASM_SRCS:.S=.o))

.PHONY: all clean iso run run-headless rust userland test-userland \
	toolchain-bootstrap kernel-compile-check kernel-host-tests test-kernel \
	test boot-smoke package-artifacts install-storage install-image ci

all: iso

rust: $(RUST_LIB)

$(RUST_LIB): $(RUST_DIR)/Cargo.toml $(RUST_DIR)/src/lib.rs $(RUST_DIR)/.cargo/config.toml
	cd $(RUST_DIR) && cargo build --release --target x86_64-unknown-none

$(OBJ_DIR)/%.o: kernel/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: kernel/src/%.S
	mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS) $(RUST_LIB) linker.ld
	mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(RUST_LIB) -lgcc

iso: $(KERNEL_ELF) grub/grub.cfg
	mkdir -p $(ISO_ROOT)/boot/grub
	mkdir -p $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp grub/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkstandalone -O x86_64-efi -o $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI "boot/grub/grub.cfg=grub/grub.cfg"
	grub-mkrescue -o $(ISO_IMAGE) $(ISO_ROOT)

run: iso
	qemu-system-x86_64 -cdrom $(ISO_IMAGE) -m 256M

run-headless: iso
	qemu-system-x86_64 -cdrom $(ISO_IMAGE) -m 256M -serial mon:stdio -nographic

clean:
	rm -rf $(BUILD_DIR)
	cd $(RUST_DIR) && cargo clean

userland:
	$(MAKE) -C userland

test-userland:
	$(MAKE) -C userland test

toolchain-bootstrap:
	bash $(SCRIPT_DIR)/bootstrap_toolchain.sh

kernel-compile-check:
	bash $(SCRIPT_DIR)/kernel_compile_check.sh

kernel-host-tests:
	bash $(SCRIPT_DIR)/kernel_host_tests.sh

test-kernel: kernel-compile-check kernel-host-tests

test: test-kernel test-userland

boot-smoke:
	bash $(SCRIPT_DIR)/boot_smoke_test.sh

package-artifacts:
	bash $(SCRIPT_DIR)/package_artifacts.sh

install-storage: $(KERNEL_ELF)
	@if [[ -z "$(DEVICE)" && -z "$(IMAGE)" ]]; then \
		echo "set DEVICE=/dev/sdX or IMAGE=build/waluos-disk.img"; \
		exit 1; \
	fi
	bash $(SCRIPT_DIR)/install_storage.sh \
		$(if $(DEVICE),--device $(DEVICE),) \
		$(if $(IMAGE),--image $(IMAGE),) \
		$(if $(SIZE_MIB),--size-mib $(SIZE_MIB),) \
		$(if $(ESP_SIZE_MIB),--esp-size-mib $(ESP_SIZE_MIB),) \
		--kernel $(KERNEL_ELF) \
		$(if $(GRUB_CFG),--grub-config $(GRUB_CFG),) \
		$(if $(ROOT_LABEL),--root-label $(ROOT_LABEL),) \
		$(if $(EFI_LABEL),--efi-label $(EFI_LABEL),) \
		$(if $(FORCE),--force,) \
		$(if $(CONFIRM),--confirm $(CONFIRM),) \
		$(if $(YES),--yes,) \
		$(if $(SKIP_DRIVER_LOAD),--skip-driver-load,) \
		$(if $(DRY_RUN),--dry-run,)

install-image: $(KERNEL_ELF)
	bash $(SCRIPT_DIR)/install_storage.sh \
		--image $(if $(IMAGE),$(IMAGE),build/waluos-disk.img) \
		$(if $(SIZE_MIB),--size-mib $(SIZE_MIB),) \
		$(if $(ESP_SIZE_MIB),--esp-size-mib $(ESP_SIZE_MIB),) \
		--kernel $(KERNEL_ELF) \
		$(if $(GRUB_CFG),--grub-config $(GRUB_CFG),) \
		$(if $(ROOT_LABEL),--root-label $(ROOT_LABEL),) \
		$(if $(EFI_LABEL),--efi-label $(EFI_LABEL),) \
		$(if $(FORCE),--force,) \
		$(if $(CONFIRM),--confirm $(CONFIRM),) \
		$(if $(YES),--yes,) \
		$(if $(SKIP_DRIVER_LOAD),--skip-driver-load,) \
		$(if $(DRY_RUN),--dry-run,)

ci: test boot-smoke package-artifacts
