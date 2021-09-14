include ../common.mak

PROGRAM=$(LIB)/libpit$(SOEXT)

OBJS=threadudp.o mutex.o sys.o ptr.o debug.o script.o builtin.o list.o sock.o io.o loadfile.o util.o bytes.o ts.o yuv.o timeutc.o media.o xalloc.o endianness.o sim.o gps.o match.o vfs.o vfslocal.o filter.o telnet.o login.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -lpthread $(EXTLIBS)

BCPROGRAM=$(BC)/libpit.bc

BCOBJS=thread.bc mutex.bc sys.bc ptr.bc debug.bc script.bc builtin.bc list.bc sock.bc io.bc loadfile.bc util.bc bytes.bc ts.bc yuv.bc timeutc.bc media.bc xalloc.bc endianness.bc sim.bc gps.bc match.bc vfs.bc vfslocal.bc mailbox.bc filter.bc telnet.bc login.bc

$(BCPROGRAM): $(BCOBJS)
	$(EMCC) $(EMLDFLAGS) -o $(BCPROGRAM) $(BCOBJS)

bc: $(BCPROGRAM)

clean:
	rm -f $(PROGRAM) *.o $(BCPROGRAM) *.bc
