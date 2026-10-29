ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

PREFIX  := $(DEVKITPPC)/bin/powerpc-eabi-
CC      := $(PREFIX)gcc
AR      := $(PREFIX)ar
NM      := $(PREFIX)nm
OBJCOPY := $(PREFIX)objcopy
LIBOGC  ?= $(DEVKITPRO)/libogc

MBEDTLS := vendor/mbedtls
MINIZ   := vendor/miniz
# Inflate-only miniz with stdio; zlib-style names off so apps that link zlib are unaffected.
MZFLAGS := -DMINIZ_NO_TIME -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_APIS \
           -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
ARCH    := -DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float
CFLAGS  := -O2 -Wall -Wextra $(ARCH) -Iinclude -Iconfig -I$(MBEDTLS)/include -I$(MINIZ) -I$(LIBOGC)/include $(MZFLAGS) \
           -DMBEDTLS_CONFIG_FILE=\"mbedtls_wiiupdate.h\"
TLSFLAGS := -Os $(ARCH) -Iconfig -I$(MBEDTLS)/include -I$(MBEDTLS)/library -I$(LIBOGC)/include \
           -DMBEDTLS_CONFIG_FILE=\"mbedtls_wiiupdate.h\"

SRCS     := $(wildcard source/*.c)
OBJS     := $(patsubst source/%.c,build/%.o,$(SRCS))
TLS_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
TLS_OBJS := $(patsubst $(MBEDTLS)/library/%.c,build/mbedtls/%.o,$(TLS_SRCS))
MZ_OBJS  := build/miniz.o
LIB      := lib/libwiiupdate.a

all: $(LIB)

# mbedTLS and miniz are linked into the archive under a private prefix
# (mbedtls_* -> wiiupdate_mbedtls_*, mz_* -> wiiupdate_mz_*), so apps that
# bring their own copies, built with a different configuration, link against
# both without clashes.
$(LIB): $(OBJS) $(TLS_OBJS) $(MZ_OBJS)
	@mkdir -p lib build/renamed
	@$(NM) $^ | awk '$$NF ~ /^(mbedtls_|psa_|tf_psa_|mz_|tdefl_|tinfl_|miniz_)/ { print $$NF " wiiupdate_" $$NF }' | sort -u > build/renamed.syms
	@for o in $^; do $(OBJCOPY) --redefine-syms=build/renamed.syms $$o build/renamed/$$(echo $$o | tr / _); done
	@rm -f $@
	$(AR) rcs $@ build/renamed/*.o

build/%.o: source/%.c include/wiiupdate.h source/wiiupdate_internal.h source/wiiupdate_certs.h config/mbedtls_wiiupdate.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/mbedtls/%.o: $(MBEDTLS)/library/%.c config/mbedtls_wiiupdate.h
	@mkdir -p build/mbedtls
	@$(CC) $(TLSFLAGS) -c $< -o $@

build/miniz.o: $(MINIZ)/miniz.c $(MINIZ)/miniz.h
	@mkdir -p build
	@$(CC) -O2 $(ARCH) $(MZFLAGS) -I$(MINIZ) -Wno-pragmas -w -c $< -o $@

install: $(LIB)
	@mkdir -p $(DESTDIR)$(DEVKITPRO)/portlibs/ppc/lib $(DESTDIR)$(DEVKITPRO)/portlibs/ppc/include
	cp $(LIB) $(DESTDIR)$(DEVKITPRO)/portlibs/ppc/lib/
	cp include/wiiupdate.h $(DESTDIR)$(DEVKITPRO)/portlibs/ppc/include/

clean:
	rm -rf build lib

.PHONY: all install clean
