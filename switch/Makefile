#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITPRO)/libnx/switch_rules

#-------------------------------------------------------------------------------
# APP_TITLE etc. describe the app in the Homebrew Menu (.nro)
#-------------------------------------------------------------------------------
APP_TITLE	:= PictoChat
APP_AUTHOR	:= quatric
APP_ICON	:= $(TOPDIR)/meta/icon.jpg

#-------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#-------------------------------------------------------------------------------
TARGET		:=	PictoChat
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	config

VERSION	?=	$(shell cat VERSION 2>/dev/null || echo 1.0.0)
export VERSION
APP_VERSION	:= $(VERSION)

#-------------------------------------------------------------------------------
# options for code generation
#-------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -O2 -Wall -Wno-format-truncation -ffunction-sections $(ARCH) $(INCLUDE) -D__SWITCH__ \
		-DAPP_VERSION=\"$(VERSION)\" -DMBEDTLS_CONFIG_FILE=\"mbedtls_switch.h\" \
		-I$(PORTLIBS_PATH)/switch/include/freetype2

CXXFLAGS	:= $(CFLAGS)

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# SDL2's own dependencies come after it; libwebp 1.0 (the portlibs one) has no libsharpyuv
LIBS	:=	-lmbedtls -lSDL2 -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lfreetype -lharfbuzz -lfreetype -lpng16 -lturbojpeg -lwebp -lbz2 -lz \
		-lnx -lm -lstdc++

AUTOTEST	?=	0
ifeq ($(AUTOTEST),1)
 CFLAGS += -DAUTOTEST
endif

#-------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level
# containing include and lib
#-------------------------------------------------------------------------------
LIBDIRS	:= $(TOPDIR)/portlibs/switch $(PORTLIBS) $(LIBNX)

#-------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#-------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#-------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.png $(dir)/*.woff $(dir)/*.ttf $(dir)/*.pcm)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all dist

#-------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#-------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf dist

#-------------------------------------------------------------------------------
# SD card package: switch/PictoChat/PictoChat.nro, for the Homebrew Menu
dist: $(BUILD)
	@rm -rf dist && mkdir -p dist/switch/PictoChat
	@cp $(TARGET).nro dist/switch/PictoChat/
	@cd dist && zip -qr PictoChat-Switch.zip switch
	@echo built dist/PictoChat-Switch.zip

#-------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#-------------------------------------------------------------------------------
# main targets
#-------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#-------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#-------------------------------------------------------------------------------
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

#-------------------------------------------------------------------------------
endif
#-------------------------------------------------------------------------------
