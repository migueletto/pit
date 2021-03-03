include ../common.mak

PROGRAM=$(LIB)/libi2c.so

OBJS=libi2c.o i2c.o

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
