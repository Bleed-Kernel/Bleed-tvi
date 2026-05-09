TARGET = tvi
VERSION = $(shell git describe --tags --always)
PREFIX ?= /usr/local
CC := gcc

SRCDIR = src
INCLUDEDIR = include
BUILDDIR = bin
OBJ_DIR = $(BUILDDIR)/obj
BIN_DIR = $(BUILDDIR)
LIB_DIR = sysroot/lib

BLIBC_REPO = https://github.com/Bleed-Kernel/blibc.git
BLIBC_DIR = external/blibc
BLIBC = $(LIB_DIR)/blibc.a
CRT0 = $(LIB_DIR)/start.o

SRCS = $(shell find $(SRCDIR) -name '*.c')
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

COMMON_CFLAGS = \
    -I$(INCLUDEDIR) \
    -Isysroot/include \
	-ffreestanding \
	-nostdinc \
	-fno-stack-protector \
    -DTVI_VERSION='"$(VERSION)"' \
    -DPREFIX='"$(PREFIX)"' \
    -std=c99 \
    -D_POSIX_C_SOURCE=200809L \
    -Wall \
    -Wextra \

LDFLAGS = \
    -nostartfiles \
    -nostdlib \
    -static \
    -L$(LIB_DIR)

all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): blibc $(OBJS)
	@echo '[linking into $@]'
	@mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(CRT0) $(OBJS) -l:blibc.a -lgcc

$(OBJ_DIR)/%.o: $(SRCDIR)/%.c | blibc
	@echo '[compiling $<]'
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

install: all
	@echo '[installing $(TARGET)]'
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp $(BIN_DIR)/$(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo '[installing manual]'
	@mkdir -p $(DESTDIR)$(PREFIX)/share/$(TARGET)
	@cp help.txt $(DESTDIR)$(PREFIX)/share/$(TARGET)

uninstall:
	rm -rf $(DESTDIR)$(PREFIX)/bin/$(TARGET) $(DESTDIR)$(PREFIX)/share/$(TARGET)

test: $(BIN_DIR)/$(TARGET)
	@echo '[running tests]'
	@$(BIN_DIR)/$(TARGET)

clean:
	@echo '[cleaning build artifacts]'
	rm -rf $(BUILDDIR)

distclean: clean
	@echo '[cleaning all generated files]'
	rm -rf sysroot external

blibc:
	@echo "[BLIBC] Preparing blibc"
	@if [ ! -d "$(BLIBC_DIR)" ]; then \
		git clone $(BLIBC_REPO) $(BLIBC_DIR); \
	fi
	$(MAKE) -C $(BLIBC_DIR)
	@echo "[BLIBC] Syncing sysroot"
	@mkdir -p sysroot
	@cp -r $(BLIBC_DIR)/sysroot/* sysroot/

.PHONY: all install uninstall test clean distclean blibc
