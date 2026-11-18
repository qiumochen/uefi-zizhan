ARCH              = arm64
TARGET            = aarch64-linux-gnu
CC                = clang
CFLAGS            = --target=$(TARGET) -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions
LD                ?= ld.lld
OBJCOPY           ?= llvm-objcopy

all: BootShim.elf BootShim.bin

BootShim.bin: BootShim.elf
	$(OBJCOPY) -O binary $< $@

BootShim.elf: BootShim.o BootShimC.o BootShim.ld
	$(LD) -T BootShim.ld -o $@ BootShim.o BootShimC.o

BootShim.o: BootShim.S
	$(CC) ${CFLAGS} -c $< -o $@ -DREQUIRES_KERNEL_HEADER=$(REQUIRES_KERNEL_HEADER) -DFD_BASE=$(FD_BASE) -DFD_SIZE=$(FD_SIZE)

BootShimC.o: BootShimC.c
	$(CC) ${CFLAGS} -c $< -o $@

clean:
	rm -f BootShim.elf BootShim.bin BootShim.o BootShimC.o

BootShim.S:
