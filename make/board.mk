# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

ifeq ($(PLATFORM_USE_SHIM),true)

ifeq ($(PLATFORM_BOARD_NAME),)
$(error PLATFORM_BOARD_NAME not defined)
endif

BOARD_ZIRCON_BOOTIMAGE := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon-bootimage.bin

ifeq ($(TARGET),arm64)
include kernel/target/arm64/boot-shim/rules.mk
else
$(error PLATFORM_USE_SHIM not supported for target $(TARGET))
endif

# capture board specific variables for the build rules
$(BOARD_ZIRCON_BOOTIMAGE): BOARD_ZIRCON_BOOTIMAGE:=$(BOARD_ZIRCON_BOOTIMAGE)
$(BOARD_ZIRCON_BOOTIMAGE): BOOT_SHIM_BIN:=$(BOOT_SHIM_BIN)

# prepend shim to the zircon bootimage
$(BOARD_ZIRCON_BOOTIMAGE): $(ZIRCON_BOOTIMAGE) $(BOOT_SHIM_BIN)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)cat $(BOOT_SHIM_BIN) $(ZIRCON_BOOTIMAGE) > $@

GENERATED += $(BOARD_ZIRCON_BOOTIMAGE)
EXTRA_BUILDDEPS += $(BOARD_ZIRCON_BOOTIMAGE)

# A board's MDI bootdata, which the Fuchsia build puts in its boot images
# (GRT's zircon boots with it; this kernel does not read it).
ifneq ($(PLATFORM_MDI_SRCS),)
BOARD_MDI := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-mdi.bin
$(BOARD_MDI): PLATFORM_MDI_SRCS:=$(PLATFORM_MDI_SRCS)
$(BOARD_MDI): $(MDIGEN) $(PLATFORM_MDI_SRCS) $(MDI_INCLUDES)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)$(MDIGEN) -o $@ $(PLATFORM_MDI_SRCS)

# The board's kernel bootdata for the Fuchsia build: its platform ID and
# its MDI.
BOARD_KERNEL_BOOTDATA := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-kernel-bootdata.bin
$(BOARD_KERNEL_BOOTDATA): BOARD_MDI:=$(BOARD_MDI)
$(BOARD_KERNEL_BOOTDATA): BOARD_PLATFORM_ID:=--vid $(PLATFORM_VID) --pid $(PLATFORM_PID) --board $(PLATFORM_BOARD_NAME)
$(BOARD_KERNEL_BOOTDATA): $(MKBOOTFS) $(BOARD_MDI)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)$(MKBOOTFS) -o $@ $(BOARD_PLATFORM_ID) $(BOARD_MDI)

GENERATED += $(BOARD_MDI) $(BOARD_KERNEL_BOOTDATA)
EXTRA_BUILDDEPS += $(BOARD_MDI) $(BOARD_KERNEL_BOOTDATA)
endif

# clear variables that were passed in to us
PLATFORM_BOARD_NAME :=
PLATFORM_USE_SHIM :=
PLATFORM_MDI_SRCS :=
PLATFORM_VID :=
PLATFORM_PID :=

# clear variables we set here
BOARD_MDI :=
BOARD_KERNEL_BOOTDATA :=
BOARD_ZIRCON_BOOTIMAGE :=
BOARD_BOOT_SHIM_OPTS :=

endif # PLATFORM_USE_SHIM
