#-----------------------------------------------------------------------------
# SPlayer-Next — Nintendo Switch (nx) native port
#
# Only depends on devkitPro's devkitA64 toolchain + libnx.
# No portlibs (SDL2 / SDL_mixer / SDL_ttf) are required: rendering, text and
# audio decoding are implemented in-tree (software canvas + stb_truetype +
# dr_libs + libnx audout).
#
# Build:     make -j$(nproc)
# Output:    build/SPlayer-Next.nro
#-----------------------------------------------------------------------------

DEVKITPRO ?= /opt/devkitpro
DEVKITA64 := $(DEVKITPRO)/devkitA64
LIBNX     := $(DEVKITPRO)/libnx
TOOLS     := $(DEVKITPRO)/tools/bin

#-----------------------------------------------------------------------------
# Application metadata (embedded into the NACP)
#-----------------------------------------------------------------------------

TARGET      := SPlayer-Next
APP_TITLE   := SPlayer-Next
APP_AUTHOR  := Everything-Godot
APP_VERSION := 1.2.1

BUILD    := build
SOURCES  := source
INCLUDES := include
EXTERNAL := external
ROMFS    := romfs

#-----------------------------------------------------------------------------
# Toolchain
#-----------------------------------------------------------------------------

CC  := $(DEVKITA64)/bin/aarch64-none-elf-gcc
CXX := $(DEVKITA64)/bin/aarch64-none-elf-g++
LD  := $(CXX)

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

DEFINES := -D__SWITCH__ -D__NX__

INCLUDE := -I$(INCLUDES) -I$(EXTERNAL) -I$(LIBNX)/include

CFLAGS   := -O2 -g -Wall -Wno-unused-parameter -Wno-unused-function \
            $(ARCH) $(DEFINES) $(INCLUDE)
CXXFLAGS := $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions

LDFLAGS := -specs=$(LIBNX)/switch.specs -g $(ARCH) \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map

LIBS := -L$(LIBNX)/lib -lnx

#-----------------------------------------------------------------------------
# Sources
#-----------------------------------------------------------------------------

CPPFILES := $(filter-out $(SOURCES)/host_preview.cpp, \
              $(shell find $(SOURCES) -name '*.cpp' | sort))
CFILES   := $(shell find $(SOURCES) -name '*.c' | sort)

OFILES := $(addprefix $(BUILD)/,$(CPPFILES:.cpp=.o)) \
          $(addprefix $(BUILD)/,$(CFILES:.c=.o))

DEPFILES := $(OFILES:.o=.d)

#-----------------------------------------------------------------------------
# Targets
#-----------------------------------------------------------------------------

.PHONY: all clean nro nacp

all: $(BUILD)/$(TARGET).nro

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	@mkdir -p $(BUILD)
	$(LD) $(OFILES) $(LDFLAGS) $(LIBS) -o $@

$(BUILD)/$(TARGET).nacp:
	@mkdir -p $(BUILD)
	$(TOOLS)/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(BUILD)/$(TARGET).nro: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nacp
	$(TOOLS)/elf2nro $< $@ --nacp=$(BUILD)/$(TARGET).nacp --romfs=$(ROMFS)
	@echo "built: $@"

nro: $(BUILD)/$(TARGET).nro
nacp: $(BUILD)/$(TARGET).nacp

clean:
	@rm -rf $(BUILD)

-include $(DEPFILES)
