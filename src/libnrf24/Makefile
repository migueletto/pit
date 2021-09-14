include ../common.mak

PROGRAM=$(LIB)/libnrf24.so

OBJS=libnrf24.o nrf24.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
