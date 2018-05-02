# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# use linker garbage collection, if requested
ifeq ($(call TOBOOL,$(USE_LINKER_GC)),true)
GLOBAL_LDFLAGS += --gc-sections
endif

ifneq (,$(EXTRA_BUILDRULES))
-include $(EXTRA_BUILDRULES)
endif

# Generate an input linker script to define as symbols all the
# variables set in makefiles that the linker script needs to use.
LINKER_SCRIPT_VARS := KERNEL_BASE SMP_MAX_CPUS BOOT_HEADER_SIZE
DEFSYM_SCRIPT := $(BUILDDIR)/kernel-vars.ld
$(DEFSYM_SCRIPT): FORCE
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)($(foreach var,$(LINKER_SCRIPT_VARS),\
			    echo 'PROVIDE_HIDDEN($(var) = $($(var)));';)\
		 ) > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)
GENERATED += $(DEFSYM_SCRIPT)

$(OUTLKELF): kernel/kernel.ld $(DEFSYM_SCRIPT) $(ALLMODULE_OBJS) $(EXTRA_OBJS)
	$(call BUILDECHO,linking $@)
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) $(KERNEL_LDFLAGS) -T $^ -o $@
# enable/disable the size output based on a combination of ENABLE_BUILD_LISTFILES
# and QUIET
ifeq ($(call TOBOOL,$(ENABLE_BUILD_LISTFILES)),true)
ifeq ($(call TOBOOL,$(QUIET)),false)
	$(NOECHO)$(SIZE) $@
endif
endif

# Tell the linker to record all the relocations it applied.
KERNEL_LDFLAGS += --emit-relocs

# Use the --emit-relocs records to extract the fixups needed to relocate
# the kernel at boot.
OUTLKELF_FIXUPS := $(BUILDDIR)/$(LKNAME)-fixups.inc
$(OUTLKELF_FIXUPS): scripts/gen-kaslr-fixups.sh $(OUTLKELF)
	$(call BUILDECHO,extracting relocations into $@)
	$(NOECHO)$(SHELLEXEC) $^ '$(READELF)' '$(OBJDUMP)' $@
GENERATED += $(OUTLKELF_FIXUPS)

# Canned sequence to convert an ELF file to a raw binary.
define elf2bin-commands
	$(call BUILDECHO,generating image $@)
	$(NOECHO)$(OBJCOPY) -O binary $< $@
endef

# Extract the raw binary image of the kernel proper.
OUTLKELF_RAW := $(OUTLKELF).bin
$(OUTLKELF_RAW): $(OUTLKELF); $(elf2bin-commands)

OUTLKELF_IMAGE_ASM := kernel/arch/$(ARCH)/image.S
OUTLKELF_IMAGE_OBJ := $(BUILDDIR)/$(LKNAME).image.o
ALLOBJS += $(OUTLKELF_IMAGE_OBJ)
KERNEL_DEFINES += \
    BOOT_HEADER_SIZE=$(BOOT_HEADER_SIZE) \
    KERNEL_IMAGE='"$(OUTLKELF_RAW)"' \

# Assemble the kernel image along with boot headers and relocation fixup code.
# TODO(mcgrathr): Reuse compile.mk $(MODULE_ASMOBJS) commands here somehow.
$(OUTLKELF_IMAGE_OBJ): $(OUTLKELF_IMAGE_ASM) $(OUTLKELF_FIXUPS) $(OUTLKELF_RAW)
	@$(MKDIR)
	$(call BUILDECHO, assembling $<)
	$(NOECHO)$(CC) $(GLOBAL_OPTFLAGS)  \
	    $(GLOBAL_COMPILEFLAGS) $(KERNEL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) \
	    $(GLOBAL_ASMFLAGS) $(KERNEL_ASMFLAGS) $(ARCH_ASMFLAGS) \
	    $(GLOBAL_INCLUDES) $(KERNEL_INCLUDES) -I$(BUILDDIR) \
	    -c $< -MD -MP -MT $@ -MF $(@:.o=.d) -o $@

# Now link the final load image, using --just-symbols to let image.S refer
# to symbols defined in the kernel proper.
$(OUTLKELF_IMAGE): $(OUTLKELF_IMAGE_OBJ) $(OUTLKELF) $(DEFSYM_SCRIPT) \
		   kernel/image.ld
	$(call BUILDECHO,linking $@)
	@$(MKDIR)
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) --build-id=none \
		       -o $@ -T kernel/image.ld --just-symbols $(OUTLKELF) \
		       $(DEFSYM_SCRIPT) $(OUTLKELF_IMAGE_OBJ)

# Finally, extract the raw binary of the kernel load image.
$(OUTLKBIN): $(OUTLKELF_IMAGE); $(elf2bin-commands)

$(OUTLKELF)-gdb.py: scripts/$(LKNAME).elf-gdb.py
	$(call BUILDECHO, generating $@)
	@$(MKDIR)
	$(NOECHO)cp -f $< $@
EXTRA_BUILDDEPS += $(OUTLKELF)-gdb.py

# print some information about the build
#$(BUILDDIR)/srcfiles.txt:
#	@echo generating $@
#	$(NOECHO)echo $(sort $(ALLSRCS)) | tr ' ' '\n' > $@
#
#.PHONY: $(BUILDDIR)/srcfiles.txt
#GENERATED += $(BUILDDIR)/srcfiles.txt
#
#$(BUILDDIR)/include-paths.txt:
#	@echo generating $@
#	$(NOECHO)echo $(subst -I,,$(sort $(KERNEL_INCLUDES))) | tr ' ' '\n' > $@
#
#.PHONY: $(BUILDDIR)/include-paths.txt
#GENERATED += $(BUILDDIR)/include-paths.txt
#
#.PHONY: $(BUILDDIR)/user-include-paths.txt
#GENERATED += $(BUILDDIR)/user-include-paths.txt

# debug info rules

$(BUILDDIR)/%.dump: $(BUILDDIR)/%
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(OBJDUMP) -x $< > $@

$(BUILDDIR)/%.lst: $(BUILDDIR)/%
	$(call BUILDECHO,generating listing $@)
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -d $< | $(CPPFILT) > $@

$(BUILDDIR)/%.debug.lst: $(BUILDDIR)/%
	$(call BUILDECHO,generating debug listing $@)
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -S $< | $(CPPFILT) > $@

$(BUILDDIR)/%.strip: $(BUILDDIR)/%
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(STRIP) $< $@

$(BUILDDIR)/%.sym: $(BUILDDIR)/%
	$(call BUILDECHO,generating symbols $@)
	$(NOECHO)$(OBJDUMP) -t $< | $(CPPFILT) > $@

$(BUILDDIR)/%.sym.sorted: $(BUILDDIR)/%
	$(call BUILDECHO,generating sorted symbols $@)
	$(NOECHO)$(OBJDUMP) -t $< | $(CPPFILT) | sort > $@

$(BUILDDIR)/%.size: $(BUILDDIR)/%
	$(call BUILDECHO,generating size map $@)
	$(NOECHO)$(NM) -S --size-sort $< > $@

$(BUILDDIR)/%.id: $(BUILDDIR)/%
	$(call BUILDECHO,generating id file $@)
	$(NOECHO)env READELF="$(READELF)" scripts/get-build-id $< > $@

# EXTRA_USER_MANIFEST_LINES is a space-separated list of
# </boot-relative-path>=<local-host-path> entries to add to USER_MANIFEST.
# This lets users add files to the bootfs via make without needing to edit the
# manifest or call zbi directly.
ifneq ($(EXTRA_USER_MANIFEST_LINES),)
USER_MANIFEST_LINES += $(EXTRA_USER_MANIFEST_LINES)
$(info EXTRA_USER_MANIFEST_LINES = $(EXTRA_USER_MANIFEST_LINES))
endif

# generate a new manifest and compare to see if it differs from the previous one
# USER_MANIFEST_DEBUG_INPUTS is a dependency here as the file name to put in
# the manifest must be computed *after* the input file is produced (to get the
# build id).
.PHONY: usermanifestfile
$(USER_MANIFEST): usermanifestfile $(USER_MANIFEST_DEBUG_INPUTS)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)echo $(USER_MANIFEST_LINES) | tr ' ' '\n' | sort > $@.tmp
	$(NOECHO)for f in $(USER_MANIFEST_DEBUG_INPUTS) ; do \
	  echo debug/$$(env READELF=$(READELF) $(SHELLEXEC) scripts/get-build-id $$f).debug=$$f >> $@.tmp ; \
	done
	$(NOECHO)$(call TESTANDREPLACEFILE,$@.tmp,$@)

GENERATED += $(USER_MANIFEST)

# Manifest Lines are bootfspath=buildpath
# Extract the part after the = for each line
# to generate dependencies
USER_MANIFEST_DEPS := $(foreach x,$(USER_MANIFEST_LINES),$(lastword $(subst =,$(SPACE),$(strip $(x)))))

.PHONY: user-manifest additional-bootdata
user-manifest: $(USER_MANIFEST) $(USER_MANIFEST_DEPS)
additional-bootdata: $(ADDITIONAL_BOOTDATA_ITEMS)

.PHONY: user-only
user-only: user-manifest
ifeq ($(call TOBOOL,$(ENABLE_BUILD_SYSROOT)),true)
user-only: sysroot
endif

.PHONY: kernel-only
kernel-only: kernel

$(USER_BOOTDATA): $(ZBI) $(USER_MANIFEST) $(USER_MANIFEST_DEPS) $(ADDITIONAL_BOOTDATA_ITEMS)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)$(ZBI) --target=boot -c -o $(USER_BOOTDATA) $(USER_MANIFEST) $(ADDITIONAL_BOOTDATA_ITEMS)

GENERATED += $(USER_BOOTDATA)

# build userspace filesystem image
$(USER_FS): $(USER_BOOTDATA)
	$(call BUILDECHO,generating $@)
	$(NOECHO)dd if=/dev/zero of=$@ bs=1048576 count=16
	$(NOECHO)dd if=$(USER_BOOTDATA) of=$@ conv=notrunc

# add the fs image to the clean list
GENERATED += $(USER_FS)
