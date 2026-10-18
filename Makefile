# LUC - Linux / macOS build
#   make                 console build (core + net with https via OpenSSL)
#   make WINDOW=1        + window library (SDL2)
#   make TLS=0           without OpenSSL (no https/wss)
#   sudo make install    -> $(PREFIX)/bin/luc + $(PREFIX)/share/luc/{luc_modules,packages}
CC      ?= cc
PREFIX  ?= /usr/local
CFLAGS  ?= -O2
CFLAGS  += -std=gnu99
LDLIBS  := -lm
TLS     ?= 1
WINDOW  ?= 0
SRC     := src/luc_core.c src/luc_libs.c src/luc_trans.c
HDR     := src/luc.h src/luc_posix_console.h

ifeq ($(TLS),1)
  LDLIBS += -lssl -lcrypto
else
  CFLAGS += -DLUC_NO_TLS
endif
ifeq ($(WINDOW),1)
  CFLAGS += -DLUC_WINDOW $(shell sdl2-config --cflags)
  LDLIBS += $(shell sdl2-config --libs) -ldl
endif

luc: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

test: luc
	./luc -e 'print("luc ok: " + (1 + 2))'
	cd demos/libs_libaries && ../../luc use.luc && ../../luc command.luc

install: luc
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/luc/packages
	install -m 755 luc $(DESTDIR)$(PREFIX)/bin/luc
	cp -r luc_modules $(DESTDIR)$(PREFIX)/share/luc/
	cp packages/*.lucpkg $(DESTDIR)$(PREFIX)/share/luc/packages/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/luc
	rm -rf $(DESTDIR)$(PREFIX)/share/luc

clean:
	rm -f luc

.PHONY: test install uninstall clean
