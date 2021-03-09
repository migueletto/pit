include ../common.mak

PROGRAM=$(LIB)/libnrf24$(SOEXT)

OBJS=libnrf24.o wrapper.o
CPPFLAGS=$(CFLAGS)

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit -L/usr/local/lib -lrf24 -lstdc++

clean:
	rm -f $(PROGRAM) *.o
