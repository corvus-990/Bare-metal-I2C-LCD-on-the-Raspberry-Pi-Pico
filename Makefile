# ============================================================
# Makefile - bare-metal build for the Raspberry Pi Pico (RP2040)
#
# Adapted from the Makefile of step 02_xipAndFlash in vxj9800's guide:
# https://github.com/vxj9800/bareMetalRP2040
#
#   make             build main.uf2
#   make clean       remove the build output
#   make distclean   also remove the downloaded dependencies
#
# Everything the build needs is inside this folder, so the folder can be
# anywhere. Run make from inside it, or use "make -C <folder>" from elsewhere.
# ============================================================

PROJECT   = main
LNKSCRIPT = link.ld

BUILDDIR      = build
BUILDBOOT2DIR = $(BUILDDIR)/boot2

TOOLCHAIN = arm-none-eabi-
GCC = $(TOOLCHAIN)gcc
DMP = $(TOOLCHAIN)objdump
CPY = $(TOOLCHAIN)objcopy

GCCFLAGS ?= -mcpu=cortex-m0plus -mthumb -O3
LNKFLAGS ?= -T $(LNKSCRIPT) -nostdlib

# ---------- Third-party code, downloaded on the first build ----------
# Each repository is pinned to an exact commit so the build is repeatable.
# After changing a commit here, run "make distclean" before building again.
DEPSDIR = deps

GUIDE_URL    = https://github.com/vxj9800/bareMetalRP2040
GUIDE_COMMIT = 8051b9edfb98a6cebd702eb160d87da27ecb7ea3
CRCPP_URL    = https://github.com/d-bahr/CRCpp
CRCPP_COMMIT = 6ea024a9140eedd34b399299c60089fdba42c048
UF2_URL      = https://github.com/microsoft/uf2
UF2_COMMIT   = b19ac9ad613e11a062e4d00845e2b80e6b0ad29f

# Boot stage 2 and the program that computes its checksum (from the guide)
BOOT2DIR = $(DEPSDIR)/bareMetalRP2040/02_xipAndFlash/boot2
BOOT2    = bootStage2QuadIO
COMPCRC  = compCrc32
CRCVALUE = crc

# Script that converts a .bin into a .uf2 (from Microsoft's UF2 repository)
UF2CONV = $(DEPSDIR)/uf2/utils/uf2conv.py

build: $(PROJECT).uf2

# ---------- Download the dependencies ----------
# Usage: $(call fetch,<url>,<commit>,<folder name inside deps/>)
define fetch
rm -rf $(DEPSDIR)/$(3)
git init -q $(DEPSDIR)/$(3)
git -C $(DEPSDIR)/$(3) fetch -q --depth 1 $(1) $(2)
git -C $(DEPSDIR)/$(3) checkout -q FETCH_HEAD
endef

$(DEPSDIR)/.fetched:
	mkdir -p $(DEPSDIR)
	$(call fetch,$(GUIDE_URL),$(GUIDE_COMMIT),bareMetalRP2040)
	$(call fetch,$(CRCPP_URL),$(CRCPP_COMMIT),CRCpp)
	$(call fetch,$(UF2_URL),$(UF2_COMMIT),uf2)
	touch $@

$(BUILDBOOT2DIR):
	mkdir -p $(BUILDBOOT2DIR)

# ---------- Compile boot stage 2 on its own ----------
$(BUILDBOOT2DIR)/$(BOOT2).elf: $(LNKSCRIPT) | $(DEPSDIR)/.fetched $(BUILDBOOT2DIR)
	$(GCC) $(BOOT2DIR)/$(BOOT2).c $(GCCFLAGS) $(LNKFLAGS) -o $@
	$(DMP) -hSD $(BUILDBOOT2DIR)/$(BOOT2).elf > $(BUILDBOOT2DIR)/$(BOOT2).objdump

# ---------- Compute its CRC32 (written to build/boot2/crc.c) ----------
$(BUILDBOOT2DIR)/$(CRCVALUE).c: $(BUILDBOOT2DIR)/$(BOOT2).elf
	$(CPY) -O binary $(BUILDBOOT2DIR)/$(BOOT2).elf $(BUILDBOOT2DIR)/$(BOOT2).bin
	g++ -std=c++17 -I $(DEPSDIR) $(BOOT2DIR)/$(COMPCRC).cpp -o $(BUILDBOOT2DIR)/$(COMPCRC).out
	./$(BUILDBOOT2DIR)/$(COMPCRC).out $(BUILDBOOT2DIR)/$(BOOT2).bin

# ---------- Compile and link the full firmware ----------
$(BUILDDIR)/$(PROJECT).elf: $(wildcard *.c) $(BUILDBOOT2DIR)/$(CRCVALUE).c $(LNKSCRIPT)
	$(GCC) $(wildcard *.c) $(BOOT2DIR)/$(BOOT2).c $(BUILDBOOT2DIR)/$(CRCVALUE).c $(GCCFLAGS) $(LNKFLAGS) -o $@
	$(DMP) -hSD $(BUILDDIR)/$(PROJECT).elf > $(BUILDDIR)/$(PROJECT).objdump

# ---------- Convert to UF2 ----------
$(BUILDDIR)/$(PROJECT).uf2: $(BUILDDIR)/$(PROJECT).elf
	$(CPY) -O binary $(BUILDDIR)/$(PROJECT).elf $(BUILDDIR)/$(PROJECT).bin
	python3 $(UF2CONV) -b 0x10000000 -f 0xe48bff56 -c $(BUILDDIR)/$(PROJECT).bin -o $@

# ---------- Copy the result next to the sources ----------
$(PROJECT).uf2: $(BUILDDIR)/$(PROJECT).uf2
	cp $(BUILDDIR)/$(PROJECT).uf2 ./$(PROJECT).uf2

clean:
	rm -rf $(BUILDDIR) $(PROJECT).uf2

distclean: clean
	rm -rf $(DEPSDIR)

.PHONY: build clean distclean
