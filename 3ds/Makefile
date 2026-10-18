#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files linked into the app
# INCLUDES is a list of directories containing header files
#---------------------------------------------------------------------------------
TARGET		:=	PictoChat
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	config

APP_TITLE	:=	PictoChat
APP_DESCRIPTION	:=	Chat on PictoChat Online (pict.chat)
APP_AUTHOR	:=	quatric
APP_ICON	:=	$(TOPDIR)/icon.png

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
VERSION	?=	$(shell cat $(TOPDIR)/VERSION 2>/dev/null || echo 1.0.0)
export VERSION

ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -O2 -Wall -Wno-format-truncation -mword-relocations -ffunction-sections \
		$(ARCH) $(INCLUDE) -D__3DS__ -DAPP_VERSION=\"$(VERSION)\" \
		-DMBEDTLS_CONFIG_FILE=\"mbedtls_3ds.h\"

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

AUTOTEST	?=	0
ifeq ($(AUTOTEST),1)
 CFLAGS += -DAUTOTEST
endif

PROFILE	?=	0
ifeq ($(PROFILE),1)
 CFLAGS += -DPROFILE
endif

LIBS	:=	-lmbedtls -lfreetype -lpng16 -lz -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(TOPDIR)/portlibs/3ds $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.png $(dir)/*.woff $(dir)/*.ttf $(dir)/*.pcm)))

export LD	:=	$(CC)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES	:=	$(CFILES:.c=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(TOPDIR)/portlibs/3ds/include/freetype2 \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXFLAGS	+=	--smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: $(BUILD) clean dist cia

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia $(TARGET).bnr $(TARGET).icn dist

#---------------------------------------------------------------------------------
run: $(BUILD)
	3dslink $(TARGET).3dsx

#---------------------------------------------------------------------------------
# Installable CIA (needs makerom and bannertool on the PATH)
cia: $(BUILD)
	@bannertool makebanner -i meta/banner.png -a meta/audio.wav -o $(TARGET).bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(APP_ICON) -o $(TARGET).icn
	@makerom -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf meta/app.rsf -target t -exefslogo -icon $(TARGET).icn -banner $(TARGET).bnr -major $(word 1,$(subst ., ,$(VERSION))) -minor $(or $(word 2,$(subst ., ,$(VERSION))),0) -micro $(or $(word 3,$(subst ., ,$(VERSION))),0)
	@echo built $(TARGET).cia

# For the Homebrew Launcher: 3ds/PictoChat/PictoChat.3dsx on the SD card,
# plus PictoChat.cia to install with FBI
dist: $(BUILD) cia
	@rm -rf dist && mkdir -p dist/3ds/PictoChat
	@cp $(TARGET).3dsx dist/3ds/PictoChat/
	@cd dist && zip -qr PictoChat-3DS.zip 3ds
	@cp $(TARGET).cia dist/PictoChat-3DS.cia
	@echo built dist/PictoChat-3DS.zip and dist/PictoChat-3DS.cia

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SOURCES) : $(HFILES)

#---------------------------------------------------------------------------------
# pictures, fonts and sounds are linked in as binary data
#---------------------------------------------------------------------------------
%.png.o	%_png.h :	%.png
	@echo $(notdir $<)
	@$(bin2o)

%.woff.o	%_woff.h :	%.woff
	@echo $(notdir $<)
	@$(bin2o)

%.ttf.o	%_ttf.h :	%.ttf
	@echo $(notdir $<)
	@$(bin2o)

%.pcm.o	%_pcm.h :	%.pcm
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
