#---------------------------------------------------------------------------------
# Uncanny Cat Golf 3DS
#   make        -> UncannyCatGolf.3dsx
#   make cia    -> UncannyCatGolf.cia (needs makerom and bannertool on PATH)
# Run through ./build.sh, which first converts the game's files into gen/ (romfs/).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	UncannyCatGolf
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	source
ROMFS		:=	romfs
BANNER		?=	1

APP_TITLE		:=	Uncanny Cat Golf
APP_DESCRIPTION	:=	3DS port of Uncanny Cat Golf by the Canny Crew
APP_AUTHOR		:=	SH2K and the Canny Crew
ICON			:=	gen/meta/icon.png

ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH)
PROFILE	?=	0
CFLAGS	+=	$(INCLUDE) -D__3DS__ -DUCG_PROFILE=$(PROFILE)
# the game version the port follows (first line of tools/ported-source.txt), checked against the data folder
GAME_VERSION	:=	$(shell sed -n '1s/^version //p' $(TOPDIR)/tools/ported-source.txt)
ifeq ($(GAME_VERSION),)
$(error no "version" line in tools/ported-source.txt)
endif
CFLAGS	+=	-DUCG_GAME_VERSION='"$(GAME_VERSION)"'

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map),--gc-sections

LIBS	:= -lcitro2d -lcitro3d -lvorbisidec -logg -lz -lctru -lm
LIBDIRS	:= $(CTRULIB) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

export LD	:=	$(CC)
export OFILES	:=	$(CFILES:.c=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXFLAGS	+=	--smdh=$(CURDIR)/$(TARGET).smdh --romfs=$(CURDIR)/$(ROMFS)
export APP_ICON		:=	$(TOPDIR)/$(ICON)

.PHONY: all clean cia

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia gen/meta/banner.bnr gen/meta/icon.icn

gen/meta/banner.bnr: Banner$(BANNER)/banner.png Banner$(BANNER)/banner.wav
	bannertool makebanner -i Banner$(BANNER)/banner.png -a Banner$(BANNER)/banner.wav -o $@

gen/meta/icon.icn: $(ICON)
	bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(ICON) -o $@

cia: all gen/meta/banner.bnr gen/meta/icon.icn
	makerom -f cia -o $(TARGET).cia -target t -exefslogo -elf $(TARGET).elf -rsf meta/cia.rsf -icon gen/meta/icon.icn -banner gen/meta/banner.bnr
	@echo built $(TARGET).cia

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
