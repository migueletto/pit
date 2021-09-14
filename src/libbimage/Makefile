include ../common.mak

PROGRAM=$(LIB)/libbimage$(SOEXT)

OBJS=libbimage.o

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit -lm

BCPROGRAM=$(BC)/libbimage.bc

BCOBJS=libbimage.bc

bc: $(BCPROGRAM)

$(BCPROGRAM): $(BCOBJS)
	$(EMCC) $(EMLDFLAGS) -o $(BCPROGRAM) $(BCOBJS)

clean:
	rm -f $(PROGRAM) $(BCPROGRAM) *.o *.bc
