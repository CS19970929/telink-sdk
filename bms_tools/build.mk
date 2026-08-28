# TLSR8251 BMS command-line build driver.
#
# This keeps the existing Telink Eclipse 825x_ble_sample C ABI/link contract,
# while selecting the startup SRAM profile for the actual TLSR8251 device and
# forwarding the board/AFE profile selected by bms_tools/bms.py.

REPO_ROOT ?= .
SDK_DIR ?= tc_ble_single_sdk
PROJ_DIR := $(SDK_DIR)/project/tlsr_tc32/B85
PROJ_LIB_DIR := $(SDK_DIR)/proj_lib
LINKER_FILE := $(PROJ_DIR)/boot.link

BUILD_DIR ?= $(PROJ_DIR)/825x_ble_sample_cli/legacy-309_sh367309
OBJ_DIR := $(BUILD_DIR)/obj
GEN_DIR := $(BUILD_DIR)/gen

# Defaults intentionally match bms_board.h and bms.py. bms.py passes these
# explicitly for every profile-aware build, so changing AFE/board never depends
# on editing a C header by hand.
BMS_BOARD_PROFILE ?= 2
BMS_AFE_MODEL ?= 1

# Production identity follows the selected AFE and the local build date.
# Examples: SH367309-20260828, SH3673510-20260828, MOCK-20260828.
# Use rebuild for release/test images so a new build day cannot reuse an older
# object that still contains yesterday's identity string.
PYTHON ?= python
BMS_BUILD_DATE ?= $(shell $(PYTHON) -c "from datetime import date; print(date.today().strftime('%Y%m%d'))")
ifeq ($(BMS_AFE_MODEL),0)
BMS_AFE_SERIAL_NAME := MOCK
else ifeq ($(BMS_AFE_MODEL),1)
BMS_AFE_SERIAL_NAME := SH367309
else ifeq ($(BMS_AFE_MODEL),2)
BMS_AFE_SERIAL_NAME := SH3673510
else
BMS_AFE_SERIAL_NAME := UNKNOWN
endif
BMS_BUILD_SERIAL ?= $(BMS_AFE_SERIAL_NAME)-$(BMS_BUILD_DATE)

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
	-DCHIP_TYPE=CHIP_TYPE_825x \
	-DBMS_BOARD_PROFILE=$(BMS_BOARD_PROFILE) \
	-DBMS_AFE_MODEL=$(BMS_AFE_MODEL) \
	-DBMS_BUILD_SERIAL=\"$(BMS_BUILD_SERIAL)\"

# Exact compiler-side options used by the existing 825x_ble_sample config.
CFLAGS_BASE := \
	-ffunction-sections -fdata-sections \
	-Wall -O2 \
	-fpack-struct -fshort-enums -finline-small-functions \
	-std=gnu99 -fshort-wchar -fms-extensions

# Both current BMS profiles use TLSR8251F512. In cstartup_825x.S the startup
# macro is the SRAM-end selector:
#   8251 -> 0x840000 + 32 KiB = 0x848000
#   8253 -> 0x840000 + 48 KiB = 0x84C000
#   8258 -> 0x840000 + 64 KiB = 0x850000
# Never select MCU_STARTUP_8258 for this target.
AFLAGS_BASE := -DMCU_STARTUP_8251

CFLAGS := $(CFLAGS_BASE) $(INCLUDES) $(DEFINES)
AFLAGS := $(AFLAGS_BASE)

LDFLAGS := --gc-sections -L"$(PROJ_LIB_DIR)" -T "$(LINKER_FILE)"
LIBS := -llt_825x -llt_general_stack

-include $(BUILD_DIR)/sources.mk

.PHONY: all size identity

all: identity $(ELF) $(RAW_BIN) $(LST) size

identity:
	@echo Build serial: $(BMS_BUILD_SERIAL)

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
