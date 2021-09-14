PIT=$(HOME)/work/pit
LIB=$(PIT)/lib
PRJ=$(PIT)/projects
SRC=$(PIT)/src
LIBPIT=$(SRC)/libpit

SYSNAME=pit
VERSION=1.0

MACHINE := $(shell uname -m)

ifeq ($(findstring arm,$(MACHINE)),arm)
SYS_CPU=1
SYS_SIZE=1
else ifeq ($(MACHINE),x86_64)
SYS_CPU=2
SYS_SIZE=2
else ifeq ($(MACHINE),x86_32)
SYS_CPU=2
SYS_SIZE=1
else ifeq ($(MACHINE),i686)
SYS_CPU=2
SYS_SIZE=1
else ifeq ($(MACHINE),i386)
SYS_CPU=2
SYS_SIZE=1
else
SYS_CPU=0
SYS_SIZE=0
endif

UNAME := $(shell uname -o)

ifeq ($(UNAME),GNU/Linux)
EXTLIBS=-lrt -ldl
SOEXT=.so
LUAPLAT=linux
OS=Linux
SYS_OS=1
OSDEFS=-DLINUX -DSOEXT=\"$(SOEXT)\"
else ifeq ($(UNAME),Msys)
EXTLIBS=-lwsock32 -lws2_32
SOEXT=.dll
LUAPLAT=mingw
OS=Windows
SYS_OS=2
OSDEFS=-DWINDOWS -DSOEXT=\"$(SOEXT)\"
else
SYS_OS=0
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

SYSDEFS=-DSYS_CPU=$(SYS_CPU) -DSYS_SIZE=$(SYS_SIZE) -DSYS_OS=$(SYS_OS)
CFLAGS=-Wall -fsigned-char -g -fPIC -I$(LIBPIT) -DSYSTEM_NAME=\"$(SYSNAME)\" -DSYSTEM_VERSION=\"$(VERSION)\" -DSYSTEM_OS=\"$(OS)\" $(CUSTOMFLAGS) $(SYSDEFS) $(OSDEFS)
