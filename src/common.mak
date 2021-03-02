PIT=../..
LIB=$(PIT)/lib
BC=$(PIT)/bc
PRJ=$(PIT)/projects
SRC=$(PIT)/src
LIBPIT=$(SRC)/libpit

SYSNAME=pit
VERSION=1.0

UNAME := $(shell uname -o)

ifeq ($(UNAME),GNU/Linux)
EXTLIBS=-lrt -ldl
SOEXT=.so
LUAPLAT=linux
OSDEFS=-DLINUX -DSOEXT=\"$(SOEXT)\"
OS=Linux
endif

ifeq ($(UNAME),Msys)
EXTLIBS=-lwsock32 -lws2_32
SOEXT=.dll
LUAPLAT=mingw
OS=Windows
OSDEFS=-DWINDOWS -DSOEXT=\"$(SOEXT)\"
endif

HASBCM2835 := $(shell ls /usr/local/include/ | grep bcm2835.h)

ifeq ($(HASBCM2835),bcm2835.h)
HASBCM2835=yes
else
HASBCM2835=no
endif

HASVC := $(shell ls /opt/ | grep vc)
ifeq ($(HASVC),vc)
HASVC=yes
else
HASVC=no
endif

CC=gcc
CFLAGS=-Wall -g -fPIC -I$(LIBPIT) -DSYSTEM_NAME=\"$(SYSNAME)\" -DSYSTEM_VERSION=\"$(VERSION)\" -DSYSTEM_OS=\"$(OS)\" $(CUSTOMFLAGS) $(OSDEFS)
