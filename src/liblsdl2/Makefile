include ../common.mak

PROGRAM=$(LIB)/liblsdl2$(SOEXT)

OBJS=liblsdl2.o

ifeq ($(UNAME),Darwin)
LIBSDL=-framework SDL
endif

ifeq ($(UNAME),Msys)
#LIBSDL=-lSDL2 -lversion -limm32 -lgdi32 -lsetupapi -lwinmm -lole32 -loleaut32 -lSDL2_mixer
LIBSDL=-lSDL2 -lversion -limm32 -lgdi32 -lsetupapi -lwinmm -lole32 -loleaut32
endif

ifeq ($(UNAME),GNU/Linux)
#LIBSDL=-lSDL2 -lSDL2_mixer
LIBSDL=-lSDL2
endif

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit $(LIBSDL)

clean:
	rm -f $(PROGRAM) *.o
