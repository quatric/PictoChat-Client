#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	PictoChat
BUILD		:=	build
SOURCES		:=	source libinput-wii/source
DATA		:=	data
INCLUDES	:=	config

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

VERSION	?=	$(shell cat VERSION 2>/dev/null || echo 1.0.0)
export VERSION

CFLAGS	= -g -O2 -Wall -Wno-format-truncation $(MACHDEP) $(INCLUDE) -DAPP_VERSION=\"$(VERSION)\" \
	  -DMBEDTLS_CONFIG_FILE=\"mbedtls_wii.h\" -I$(PORTLIBS_PATH)/ppc/include/freetype2
CXXFLAGS	=	$(CFLAGS)

LDFLAGS	=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:=	-lmbedtls -lasnd -lfreetype -lpng16 -lturbojpeg -lwebp -lsharpyuv -lbrotlidec -lbrotlicommon -lbz2 -lz \
		-lfat -lwiikeyboard -lwiiuse -lbte -logc -lm

AUTOTEST	?=	0
ifeq ($(AUTOTEST),1)
 CFLAGS += -DAUTOTEST
endif

WITH_WIIDRC	?=	1
WITH_WUPC	?=	1
WITH_SICKSAXIS	?=	1
ifeq ($(WITH_WIIDRC),1)
 CFLAGS += -DHAVE_WIIDRC
 LIBS := -lwiidrc $(LIBS)
endif
ifeq ($(WITH_WUPC),1)
 CFLAGS += -DHAVE_WUPC
 LDFLAGS += -Wl,-wrap,wiiuse_accept
 LIBS := -lwupc $(LIBS)
endif
ifeq ($(WITH_SICKSAXIS),1)
 CFLAGS += -DHAVE_SICKSAXIS
 LIBS := -lsicksaxis $(LIBS)
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(CURDIR)/portlibs/ppc $(PORTLIBS) $(CURDIR)/libinput-wii

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.png $(dir)/*.woff $(dir)/*.ttf $(dir)/*.pcm)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:= -L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean dist

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol dist

#---------------------------------------------------------------------------------
run:
	wiiload $(TARGET).dol

#---------------------------------------------------------------------------------
# Homebrew Channel package: apps/PictoChat/{boot.dol,icon.png,meta.xml}
dist: $(BUILD)
	@mkdir -p dist/apps/PictoChat
	@cp $(TARGET).dol dist/apps/PictoChat/boot.dol
	@cp apps/PictoChat/icon.png dist/apps/PictoChat/
	@sed -e "s/@VERSION@/$(VERSION)/" -e "s/@DATE@/$$(date -u +%Y%m%d%H%M%S)/" apps/PictoChat/meta.xml > dist/apps/PictoChat/meta.xml
	@cd dist && rm -f PictoChat-Wii.zip && zip -qr PictoChat-Wii.zip apps
	@echo built dist/PictoChat-Wii.zip


#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

#---------------------------------------------------------------------------------
# This rule links in binary data with the .jpg extension
#---------------------------------------------------------------------------------
%.png.o	%_png.h :	%.png
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	$(bin2o)

%.woff.o	%_woff.h :	%.woff
	@echo $(notdir $<)
	$(bin2o)

%.ttf.o	%_ttf.h :	%.ttf
	@echo $(notdir $<)
	$(bin2o)

%.pcm.o	%_pcm.h :	%.pcm
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
