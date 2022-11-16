# selune - selections manager
# Copyright (C) 2022 ArcNyxx
# see LICENCE file for licensing information

.POSIX:

include config.mk

SRC = selune.c
OBJ = $(SRC:.c=.o)

all: selune

$(OBJ): config.mk

.c.o:
	$(CC) $(CFLAGS) -c $<

selune: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

clean:
	rm -f selune $(OBJ) selune-$(VERSION).tar.gz

dist: clean
	mkdir -p selune-$(VERSION)
	cp -R README LICENCE Makefile config.mk selune.1 $(SRC) \
		selune-$(VERSION)
	tar -cf - selune-$(VERSION) | gzip -c > selune-$(VERSION).tar.gz
	rm -rf selune-$(VERSION)

install: all
	mkdir -p $(PREFIX)/bin $(MANPREFIX)/man1
	cp -f selune $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/selune
	sed 's/VERSION/$(VERSION)/g' < selune.1 > $(MANPREFIX)/man1/selune.1
	chmod 644 $(MANPREFIX)/man1/selune.1

uninstall:
	rm -f $(PREFIX)/bin/selune $(MANPREFIX)/man1/selune.1

.PHONY: all clean dist install uninstall
