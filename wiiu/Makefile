#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

#-------------------------------------------------------------------------------
# APP_NAME etc. describe the app on the Wii U Menu (.wuhb)
#-------------------------------------------------------------------------------
APP_NAME	:= PictoChat
APP_SHORTNAME	:= PictoChat
APP_AUTHOR	:= quatric
APP_ICON	:= $(TOPDIR)/meta/icon.png
APP_TV_SPLASH	:= $(TOPDIR)/meta/splash-tv.png
APP_DRC_SPLASH	:= $(TOPDIR)/meta/splash-drc.png

include $(DEVKITPRO)/wut/share/wut_rules

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

#-------------------------------------------------------------------------------
# options for code generation
#-------------------------------------------------------------------------------
CFLAGS	:=	-g -O2 -Wall -Wno-format-truncation -ffunction-sections $(MACHDEP) $(INCLUDE) -D__WIIU__ -D__WUT__ \
		-DAPP_VERSION=\"$(VERSION)\" -DMBEDTLS_CONFIG_FILE=\"mbedtls_wiiu.h\" \
		-I$(PORTLIBS_PATH)/ppc/include/freetype2

CXXFLAGS	:= $(CFLAGS)

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-g $(ARCH) $(RPXSPECS) -Wl,-Map,$(notdir $*.map)

LIBS	:=	-lmbedtls -lSDL2 -lfreetype -lpng16 -lturbojpeg -lwebp -lsharpyuv -lbrotlidec -lbrotlicommon -lbz2 -lz \
		-lwut -lm

AUTOTEST	?=	0
ifeq ($(AUTOTEST),1)
 CFLAGS += -DAUTOTEST
endif

#-------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level
# containing include and lib
#-------------------------------------------------------------------------------
LIBDIRS	:= $(TOPDIR)/portlibs/wiiu $(PORTLIBS) $(WUT_ROOT)

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
	@rm -fr $(BUILD) $(TARGET).wuhb $(TARGET).rpx $(TARGET).elf dist

#-------------------------------------------------------------------------------
# SD card package: wiiu/apps/PictoChat/PictoChat.wuhb, for Aroma
dist: $(BUILD)
	@rm -rf dist && mkdir -p dist/wiiu/apps/PictoChat
	@cp $(TARGET).wuhb dist/wiiu/apps/PictoChat/
	@cd dist && zip -qr PictoChat-WUP.zip wiiu
	@echo built dist/PictoChat-WUP.zip

#-------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#-------------------------------------------------------------------------------
# main targets
#-------------------------------------------------------------------------------
all	:	$(OUTPUT).wuhb

$(OUTPUT).wuhb	:	$(OUTPUT).rpx
$(OUTPUT).rpx	:	$(OUTPUT).elf
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
