VERSION ?= $(shell cat VERSION 2>/dev/null || echo "0.3.0")
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -DVERSION=\"$(VERSION)\"
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

BUILD_DIR ?= build
TARGET = wayhud
BIN = $(BUILD_DIR)/$(TARGET)

WAYLAND_SCANNER ?= $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
PKGS = wayland-client cairo pango pangocairo xkbcommon

PKG_CFLAGS = $(shell pkg-config --cflags $(PKGS))
PKG_LIBS = $(shell pkg-config --libs $(PKGS)) -lrt

PROTO_XMLS = protocols/wlr-layer-shell-unstable-v1.xml \
             protocols/xdg-shell.xml

PROTO_HEADERS = $(BUILD_DIR)/protocols/wlr-layer-shell-unstable-v1-client-protocol.h \
                $(BUILD_DIR)/protocols/xdg-shell-client-protocol.h
PROTO_SOURCES = $(BUILD_DIR)/protocols/wlr-layer-shell-unstable-v1-protocol.c \
                $(BUILD_DIR)/protocols/xdg-shell-protocol.c

SRC = src/main.c src/input.c src/render.c src/style.c src/filter.c
OBJ = $(SRC:src/%.c=$(BUILD_DIR)/obj/%.o) \
      $(PROTO_SOURCES:$(BUILD_DIR)/protocols/%.c=$(BUILD_DIR)/obj/%.o)

INCLUDES = -Isrc -I$(BUILD_DIR) -I$(BUILD_DIR)/protocols

.PRECIOUS: $(PROTO_SOURCES) $(PROTO_HEADERS)

all: $(BIN)

# Wayland protocol generators
$(BUILD_DIR)/protocols/%-client-protocol.h: protocols/%.xml
	@mkdir -p $(BUILD_DIR)/protocols
	$(WAYLAND_SCANNER) client-header $< $@

$(BUILD_DIR)/protocols/%-protocol.c: protocols/%.xml
	@mkdir -p $(BUILD_DIR)/protocols
	$(WAYLAND_SCANNER) private-code $< $@

# Source objects
$(BUILD_DIR)/obj/%.o: src/%.c $(PROTO_HEADERS)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(INCLUDES) -c $< -o $@

# Protocol objects
$(BUILD_DIR)/obj/%-protocol.o: $(BUILD_DIR)/protocols/%-protocol.c $(PROTO_HEADERS)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(INCLUDES) -c $< -o $@

$(BIN): $(PROTO_HEADERS) $(OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJ) $(PKG_LIBS) -o $@

proto: $(PROTO_HEADERS)

clean:
	rm -rf $(BUILD_DIR)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(TARGET)

install-suid: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 4755 $(BIN) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

lint: $(PROTO_HEADERS)
	hk check --all

format:
	hk fix --all

.PHONY: all clean install install-suid uninstall lint format proto
