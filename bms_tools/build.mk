# HS-D011 TLSR8251 command-line build driver.
#
# This keeps the existing Telink Eclipse 825x_ble_sample C ABI/link contract,
# while selecting the startup SRAM profile for the actual TLSR8251 device.

REPO_ROOT ?= .
SDK_DIR ?= tc_ble_single_sdk
PROJ_DIR := $(SDK_DIR)/project/tlsr_tc32/B85
PROJ_LIB_DIR := $(SDK_DIR)/proj_lib
LINKER_FILE := $(PROJ_DIR)/boot.link

BUILD_DIR ?= $(PROJ_DIR)/825x_ble_sample_cli
OBJ_DIR := $(BUILD_DIR)/obj
GEN_DIR := $(BUILD_DIR)/gen

CC := tc32-elf-gcc
LD := tc32-elf-ld
OBJCOPY := tc32-elf-objcopy
OBJDUMP := tc32-elf-objdump
SIZE := tc32-elf-size

ELF := $(BUILD_DIR)/825x_ble_sample.elf
RAW_BIN := $(BUILD_DIR)/825x_ble_sample.raw.bin
LST := $(GEN_DIR)/825x_ble_sample.lst
MAP := $(GEN_DIR)/825x_ble_sample.map

INCLUDES := \
	-I"$(PROJ_DIR)" \
	-I"$(SDK_DIR)" \
	-I"$(SDK_DIR)/vendor/common" \
	-I"$(SDK_DIR)/common" \
	-I"$(SDK_DIR)/drivers/B85"

# __PROJECT_8258_BLE_SAMPLE__ is the SDK application-selection macro used by
# vendor/common/default_config.h. It is not the silicon SRAM-size selector.
# CHIP_TYPE_825x remains the correct B85-family compile target.
DEFINES := \
	-D__PROJECT_8258_BLE_SAMPLE__=1 \
	-DCHIP_TYPE=CHIP_TYPE_825x

# Exact compiler-side options used by the existing 825x_ble_sample config.
CFLAGS_BASE := \
	-ffunction-sections -fdata-sections \
	-Wall -O2 \
	-fpack-struct -fshort-enums -finline-small-functions \
	-std=gnu99 -fshort-wchar -fms-extensions

# HS-D011 is populated with TLSR8251F512. In cstartup_825x.S the startup macro
# is the SRAM-end selector:
#   8251 -> 0x840000 + 32 KiB = 0x848000
#   8253 -> 0x840000 + 48 KiB = 0x84C000
#   8258 -> 0x840000 + 64 KiB = 0x850000
# The old Eclipse profile used MCU_STARTUP_8258, which placed the main stack at
# 0x850000. That is outside TLSR8251's 32 KiB SRAM. Use the actual 8251 profile.
AFLAGS_BASE := -DMCU_STARTUP_8251

CFLAGS := $(CFLAGS_BASE) $(INCLUDES) $(DEFINES)
AFLAGS := $(AFLAGS_BASE)

LDFLAGS := --gc-sections -L"$(PROJ_LIB_DIR)" -T "$(LINKER_FILE)"
LIBS := -llt_825x -llt_general_stack

-include $(BUILD_DIR)/sources.mk

.PHONY: all size

all: $(ELF) $(RAW_BIN) $(LST) size

$(ELF): $(OBJS)
	@echo Linking: $@
	$(LD) $(LDFLAGS) -Map="$(MAP)" -o"$(ELF)" $(OBJS) $(LIBS)

$(RAW_BIN): $(ELF)
	@echo Objcopy: $@
	$(OBJCOPY) -v -O binary $(ELF) $(RAW_BIN)

$(LST): $(ELF)
	@echo Listing: $@
	$(OBJDUMP) -x -l -S $(ELF) > "$(LST)"

size: $(ELF)
	@echo Size:
	$(SIZE) -t $(ELF)
