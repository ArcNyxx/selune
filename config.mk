# selune - selections manager
# Copyright (C) 2022 ArcNyxx
# see LICENCE file for licensing information

VERSION = 0.0.0

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

WPROFILE = -Wall -Wextra -Wstrict-prototypes -Wmissing-declarations -Wshadow \
-Wswitch-default -Wunreachable-code -Wcast-align -Wpointer-arith -Wcast-qual \
-Wbad-function-cast -Winline -Wundef -Wnested-externs -Wwrite-strings \
-Wno-unused-parameter -Wfloat-equal -Wpedantic
STD = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
LIB = -lxcb -lxcb-icccm

CFLAGS = $(WPROFILE) $(STD) -Os -g
LDFLAGS = $(LIB)
