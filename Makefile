#---------------------------------------------------------------------------------
# AccuWeather for Wii - unofficial native port
#
#   make                 build the app
#   make AW_API_KEY=...  use a different AccuWeather developer key
#   make UI_AUTOCYCLE=1  cycle the four tabs automatically (screen captures)
#
# Third-party libraries live in libs/: libwiiinput is compiled straight into
# the app (its README recommends that so the HAVE_* options match), while
# mbedTLS and libwiiupdate are prebuilt into libs/ppc by tools/build_libs.sh.
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

TARGET      := accuweather-wii
BUILD       := build
SOURCES     := source libs/libwiiinput/source
DATA        := data
INCLUDES    := source config libs/libwiiinput/include libs/ppc/include \
               libs/libwiiosk/include

CFLAGS      = -g -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11 $(MACHDEP) $(INCLUDE)
CFLAGS      += -DHW_RVL
CFLAGS      += -I$(DEVKITPRO)/portlibs/ppc/include/freetype2
# The app's mbedTLS must use the same config as the libmbedtls.a built by
# tools/build_libs.sh, or structure sizes disagree at link time.
CFLAGS      += -DMBEDTLS_CONFIG_FILE='"mbedtls_wii.h"'

ifdef AW_API_KEY
CFLAGS      += -DAW_API_KEY='"$(AW_API_KEY)"'
endif
ifdef UI_AUTOCYCLE
CFLAGS      += -DUI_AUTOCYCLE
endif
ifdef UI_BOOTINFO
CFLAGS      += -DUI_BOOTINFO
endif
ifdef AW_TEST_SEARCH
CFLAGS      += -DAW_TEST_SEARCH
endif

CXXFLAGS    = $(CFLAGS)
LDFLAGS     = -g $(MACHDEP) -Wl,-Map,$(notdir $@).map

# User libraries first: mbedTLS backs the app's HTTPS client, libwiiupdate
# brings its own symbol-renamed copy, libwiiosk is the on-screen keyboard,
# libwiiinput needs libwiikeyboard, and libwiiupdate's installer needs libfat.
LIBS        := -lwiiupdate -lmbedtls -lwiiosk -lwiikeyboard -lfat \
               -lfreetype -lbrotlidec -lbrotlicommon -lpng16 -lz -lbz2 \
               -lwiiuse -lbte -logc -lm
LIBDIRS     := $(PORTLIBS) $(CURDIR)/libs/ppc

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export OFILES_BIN    := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES        := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD) \
                  -I$(LIBOGC_INC)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
                   -L$(LIBOGC_LIB)

export OUTPUT := $(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean run libs

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

run:
	wiiload $(OUTPUT).dol

# Rebuild mbedTLS + libwiiupdate + libwiiosk into libs/ppc (needs devkitPro's
# MSYS2 shell).
libs:
	bash tools/build_libs.sh

else

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

# bin2s writes each header as a side effect of building the matching .o, so the
# binary objects have to be built before anything that includes those headers.
# The headers themselves get an empty rule: on a clean tree make would otherwise
# stop with "No rule to make target 'logo_bin.h'".
$(OFILES_SOURCES) : $(OFILES_BIN)
$(HFILES) : ;

# Embedded binary resources: the four APK fonts and the wordmark texture.
%.ttf.o : %.ttf
	@echo $(notdir $<)
	@bin2s -a 32 -H $(DEPSDIR)/$(subst .,_,$(notdir $<)).h $< | $(AS) -o $@

%.bin.o : %.bin
	@echo $(notdir $<)
	@bin2s -a 32 -H $(DEPSDIR)/$(subst .,_,$(notdir $<)).h $< | $(AS) -o $@

# The fonts and texture are source files, not build products.  Without this,
# GNU make's built-in "%: %.o" rule thinks each is linked from its own .o.
%.ttf : ;
%.bin : ;

-include $(DEPSDIR)/*.d

endif
