include ../common.mak

PROGRAM=$(LIB)/libbt$(SOEXT)

OBJS=libbt.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit -lbluetooth

clean:
	rm -f $(PROGRAM) *.o
