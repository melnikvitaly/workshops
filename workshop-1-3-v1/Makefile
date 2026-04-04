SHELL   := bash.exe
OBJDUMP := ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-objdump.exe
ELF     := .pio/build/esp32-s3-devkitc-1/firmware.elf
ASM_OUT := tmp/firmware.asm

disasm:
	mkdir -p tmp
	$(OBJDUMP) -d -S --demangle $(ELF) > $(ASM_OUT)
	@echo "Done: $(ASM_OUT)"
