include ../common.mak

PROGRAM=$(LIB)/libtimer$(SOEXT)

OBJS=libtimer.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
