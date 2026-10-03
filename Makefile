CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

WAYLAND_SCANNER ?= $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
PKGS = wayland-client cairo pango pangocairo xkbcommon

PKG_CFLAGS = $(shell pkg-config --cflags $(PKGS))
PKG_LIBS = $(shell pkg-config --libs $(PKGS)) -lrt

GEN_SOURCES = protocols/wlr-layer-shell-unstable-v1-protocol.c \
              protocols/xdg-shell-protocol.c
GEN_HEADERS = protocols/wlr-layer-shell-unstable-v1-client-protocol.h \
              protocols/xdg-shell-client-protocol.h

SOURCES = main.c input.c render.c $(GEN_SOURCES)
OBJECTS = $(SOURCES:.c=.o)
TARGET = wayhud

all: $(TARGET)

protocols/%-client-protocol.h: protocols/%.xml
	$(WAYLAND_SCANNER) client-header $< $@

protocols/%-protocol.c: protocols/%.xml
	$(WAYLAND_SCANNER) private-code $< $@

%.o: %.c $(GEN_HEADERS)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -I. -Iprotocols -c $< -o $@

$(TARGET): $(GEN_HEADERS) $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) $(PKG_LIBS) -o $@

clean:
	rm -f $(TARGET) $(OBJECTS) $(GEN_SOURCES) $(GEN_HEADERS)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

install-suid: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 4755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install install-suid uninstall
