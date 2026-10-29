# Builds example.dol against the library in the parent folder.
include $(DEVKITPPC)/wii_rules

LIBOGC_INC := $(DEVKITPRO)/libogc/include
LIBOGC_LIB := $(DEVKITPRO)/libogc/lib/wii
CFLAGS  := -O2 -Wall $(MACHDEP) -I../include -I$(LIBOGC_INC) $(if $(AUTO),-DEXAMPLE_AUTO)
LDFLAGS := $(MACHDEP) -L../lib -L$(LIBOGC_LIB)
LIBS    := -lwiiupdate -lfat -lwiiuse -lbte -logc -lm

example.dol: example.elf
example.elf: main.c ../lib/libwiiupdate.a
	$(CC) $(CFLAGS) main.c $(LDFLAGS) $(LIBS) -o $@

../lib/libwiiupdate.a:
	$(MAKE) -C ..

clean:
	rm -f example.elf example.dol

.PHONY: clean
