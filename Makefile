# ===========================================================================
# BlexOS Build System – Framebuffer TTY Edition (1920x1080)
# ===========================================================================

CC         := i686-linux-gnu-gcc
AS         := nasm
LDFLAGS    := -T linker.ld -nostdlib -lgcc
CFLAGS     := -m32 -ffreestanding -fno-stack-protector -fno-builtin -O2 -w -std=gnu99 -I.

KERNEL_BIN  := mykernel.bin
INITRAMFS   := initramfs.cpio
# fb.c added; boot.s now emits Multiboot2
OBJS := fonts/ttf_render.o boot.o kernel.o fb.o commands/command_logic.o commands/stb_image_impl.o sata.o es1.o
MBEDIT_SRC  := initramfs/bin/mbedit.c
MBEDIT_BIN  := initramfs/bin/mbedit.brun
APPS_H = apps/appselector.h apps/terminal.h apps/reboot.h apps/shutdown.h

DONE    = @echo "  DONE    $@"
CC_LOG  = @echo "  CC      $<"
AS_LOG  = @echo "  AS      $<"
LD_LOG  = @echo "  LD      $(KERNEL_BIN)"

.PHONY: all clean run fix_time

all: fix_time $(MBEDIT_BIN) $(INITRAMFS) $(KERNEL_BIN)
	@echo "--------------------------------------------------"
	@echo "  // COMPILED FILES \\"
	@echo "  Binary: $(KERNEL_BIN)"
	@echo "  FS:     $(INITRAMFS)"
	@echo "--------------------------------------------------"

apps/%.h: apps/%.c
	@echo "  CC      $< -> $@"
	@(echo "/* Auto-generated from $< */"; \
	  echo "#ifndef APP_$$(echo $* | tr a-z A-Z)_H"; \
	  echo "#define APP_$$(echo $* | tr a-z A-Z)_H"; \
	  echo "void app_$*(void);"; \
	  echo "#endif") > $@

sata.o: sata.c
	$(CC) $(CFLAGS) -c sata.c -o sata.o
	@echo "  CC      sata.c"

commands/stb_image_impl.o: commands/stb_image_impl.c
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c commands/stb_image_impl.c \
		-o commands/stb_image_impl.o 2>.build_err \
		|| (cat .build_err && exit 1)

es1.o: es1.c
	$(CC) $(CFLAGS) -c es1.c -o es1.o
	@echo "  CC      es1.c"

$(MBEDIT_BIN): $(MBEDIT_SRC)
	$(CC_LOG)
	@$(CC) $(CFLAGS) $< -o $@ -nostdlib -lgcc 2>.build_err || (cat .build_err && exit 1)

$(INITRAMFS): $(MBEDIT_BIN)
	@echo "  PACK    initramfs (Raw CPIO)"
	@cd initramfs && find . | cpio -o -H newc --quiet > ../initramfs.cpio

boot.o: boot.s
	$(AS_LOG)
	@$(AS) -f elf32 boot.s -o boot.o

kernel.o: kernel.c fb.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c kernel.c -o kernel.o 2>.build_err || (cat .build_err && exit 1)

fb.o: fb.c fb.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c fb.c -o fb.o 2>.build_err || (cat .build_err && exit 1)

commands/command_logic.o: commands/command_logic.c
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c commands/command_logic.c -o commands/command_logic.o 2>.build_err || (cat .build_err && exit 1)

$(KERNEL_BIN): $(OBJS)
	$(LD_LOG)
	@$(CC) -m32 -static -nostdlib -fno-pic -fno-pie -T linker.ld -o $@ $(OBJS) -lgcc
	$(DONE)

fix_time:
	@touch kernel.c boot.s $(MBEDIT_SRC)

# QEMU: enable Multiboot2 with VGA framebuffer at 1920x1080
# QEMU: enable UEFI Multiboot2 with VGA framebuffer at 1920x1080
# QEMU: enable UEFI Multiboot2 with VGA framebuffer at 1920x1080
run: all
	@echo "  [ISO]   Preparing GRUB ISO..."
	@mkdir -p iso_root/boot/grub
	@cp $(KERNEL_BIN) iso_root/boot/
	@cp $(INITRAMFS) iso_root/
	@cp $(INITRAMFS) iso_root/boot/
	@echo 'insmod efi_gop' > iso_root/boot/grub/grub.cfg
	@echo 'insmod efi_uga' >> iso_root/boot/grub/grub.cfg
	@echo 'insmod video_bochs' >> iso_root/boot/grub/grub.cfg
	@echo 'insmod video_cirrus' >> iso_root/boot/grub/grub.cfg
	@echo 'set timeout=0' >> iso_root/boot/grub/grub.cfg
	@echo 'set default=0' >> iso_root/boot/grub/grub.cfg
	@echo 'set gfxmode=1920x1080x32' >> iso_root/boot/grub/grub.cfg
	@echo 'set gfxpayload=keep' >> iso_root/boot/grub/grub.cfg
	@echo 'menuentry "BlexOS" {' >> iso_root/boot/grub/grub.cfg
	@echo '    multiboot2 /boot/mykernel.bin' >> iso_root/boot/grub/grub.cfg
	@echo '    module2 /boot/initramfs.cpio' >> iso_root/boot/grub/grub.cfg
	@echo '    boot' >> iso_root/boot/grub/grub.cfg
	@echo '}' >> iso_root/boot/grub/grub.cfg
	@grub-mkrescue -o xlakos.iso iso_root
	@echo "  [QEMU]  Launching BlexOS in Modern 64-bit UEFI Mode..."
	@qemu-system-x86_64 -cdrom xlakos.iso -m 1G -vga std -display sdl

fonts/ttf_render.o: fonts/ttf_render.c fonts/ttf_render.h
	$(CC) $(CFLAGS) -c fonts/ttf_render.c -o fonts/ttf_render.o

clean:
	@echo "  [CLEAN]  Removing objects..."
	@rm -f $(OBJS) commands/*.o $(KERNEL_BIN) $(INITRAMFS) $(MBEDIT_BIN) .build_err
