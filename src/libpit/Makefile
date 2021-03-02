include ../common.mak

PROGRAM=$(LIB)/libpit$(SOEXT)

OBJS=threadudp.o mutex.o sys.o ptr.o debug.o script.o builtin.o list.o sock.o io.o loadfile.o util.o bytes.o ts.o yuv.o timeutc.o media.o xalloc.o endianness.o sim.o gps.o match.o vfs.o vfslocal.o filter.o telnet.o login.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -lpthread $(EXTLIBS)

clean:
	rm -f $(PROGRAM) *.o
