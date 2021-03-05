include ../common.mak

PROGRAM=$(LIB)/libnmea$(SOEXT)

OBJS=libnmea.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) $(OBJS)
