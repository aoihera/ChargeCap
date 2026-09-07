#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
TARGET   := ChargeCap
BUILD    := build
OUTDIR   := out
SOURCES  := source
INCLUDES := ../common/include

APP_TITLE   := ChargeCap
APP_AUTHOR  := ChargeCap
APP_VERSION := 1.0.0
NO_ICON     := 1

#---------------------------------------------------------------------------------
# libultrahand is vendored (no submodules). Run scripts/vendor-libultrahand.sh
# once if this directory is empty.
#---------------------------------------------------------------------------------
ifeq ($(wildcard $(TOPDIR)/lib/libultrahand/ultrahand.mk),)
$(error libultrahand is missing. Run ./scripts/vendor-libultrahand.sh from the repo root)
endif
include $(TOPDIR)/lib/libultrahand/ultrahand.mk

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
DEFINES := -DAPP_VERSION="\"$(APP_VERSION)\""

ARCH := -march=armv8-a+simd+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := -Wall -Os \
	-ffunction-sections -fdata-sections \
	-fomit-frame-pointer -fuse-linker-plugin \
	-finline-small-functions -fno-strict-aliasing \
	-frename-registers -falign-functions=16 \
	$(ARCH) $(DEFINES)

CFLAGS += $(INCLUDE) -D__SWITCH__

# Scope theme.ini / wallpaper.rgba / lang to this overlay.
export MSYS2_ARG_CONV_EXCL := -DUI_OVERRIDE_PATH
UI_OVERRIDE_PATH := /config/chargecap/
CFLAGS += -DUI_OVERRIDE_PATH="\"$(UI_OVERRIDE_PATH)\""

CXXFLAGS := $(CFLAGS) -std=c++23 \
	-fno-exceptions -fno-rtti \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffast-math -Wno-dangling-else

ASFLAGS := $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(notdir $*.map) \
	-Wl,--gc-sections -Wl,--as-needed

LIBS := -lcurl -lz -lmbedtls -lmbedx509 -lmbedcrypto -lnx

LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(OUTDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	-I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@[ -d $(OUTDIR) ] || mkdir -p $(OUTDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTDIR)

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).ovl

$(OUTPUT).ovl: $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ --nacp=$(OUTPUT).nacp
	@printf 'ULTR' >> $@
	@echo "built ... $(notdir $(OUTPUT).ovl)"

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
