# MIRC-Client (GTK 3, C11)
#   make                 bauen
#   make install         nach $(PREFIX) installieren (Standard /usr/local)
#   make deb             Debian/Ubuntu-Paket bauen
#   Windows: siehe packaging/windows/build-windows.sh (MSYS2 MINGW64)

PREFIX  ?= /usr/local
DESTDIR ?=
PKGS     = gtk+-3.0 libsoup-3.0 json-glib-1.0
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

SRC = src/main.c src/util.c src/config.c src/session.c src/conn.c src/ui.c src/dialogs.c src/commands.c src/avatar.c
OBJ = $(SRC:.c=.o)
BIN = mirc

ifeq ($(OS),Windows_NT)
  BIN := mirc.exe
  LDFLAGS += -mwindows
  OBJ += packaging/windows/mirc.res.o
endif

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/mirc.h
	$(CC) $(CFLAGS) -c -o $@ $<

packaging/windows/mirc.res.o: packaging/windows/mirc.rc packaging/windows/mirc.ico
	windres $< -O coff -o $@

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 packaging/linux/mirc.desktop $(DESTDIR)$(PREFIX)/share/applications/chat.mirc.Client.desktop
	install -Dm644 packaging/mirc.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/mirc.svg

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN) $(DESTDIR)$(PREFIX)/share/applications/chat.mirc.Client.desktop $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/mirc.svg

deb: $(BIN)
	./packaging/linux/build-deb.sh

clean:
	rm -f $(OBJ) $(BIN) mirc mirc.exe
	rm -rf build

.PHONY: all install uninstall deb clean
