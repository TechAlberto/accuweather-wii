#---------------------------------------------------------------------------------
# libwiiosk -- on-screen keyboard library for Wii homebrew
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPro/devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

# plain ar: gcc-ar needs the LTO plugin, which is not needed here
AR		:=	$(PREFIX)ar

TARGET		:=	libwiiosk.a
SOURCES		:=	source
INCLUDES	:=	include
BUILD		:=	build
LIBDIR		:=	lib

CFLAGS		:=	-g -O2 -Wall -Wextra -Wno-unused-parameter $(MACHDEP) \
			-I$(CURDIR)/$(INCLUDES) -I$(LIBOGC_INC)

CFILES		:=	$(wildcard $(SOURCES)/*.c) $(wildcard $(SOURCES)/dict/*.c)
OFILES		:=	$(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))
DEPENDS		:=	$(OFILES:.o=.d)

INSTALL_DIR	?=	$(PORTLIBS_PATH)/wii

.PHONY: all clean install example

all: $(LIBDIR)/$(TARGET)

$(LIBDIR)/$(TARGET): $(OFILES) | $(LIBDIR)
	@echo archiving ... $(notdir $@)
	@rm -f $@
	@$(AR) -rcs $@ $^

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

$(BUILD) $(LIBDIR):
	@mkdir -p $@

install: all
	@mkdir -p $(INSTALL_DIR)/include $(INSTALL_DIR)/lib
	cp $(INCLUDES)/wiiosk.h $(INCLUDES)/wiiosk_input_wii.h $(INSTALL_DIR)/include/
	cp $(LIBDIR)/$(TARGET) $(INSTALL_DIR)/lib/

example: all
	@$(MAKE) --no-print-directory -C example

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(LIBDIR)
	@$(MAKE) --no-print-directory -C example clean

-include $(DEPENDS)
