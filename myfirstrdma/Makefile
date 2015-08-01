EXE = myfirstrdma

ARGCONFIG = ../argconfig

LDLIBS += -libverbs -lrdmacm
CFLAGS += -std=c99 -D_GNU_SOURCE

default: $(EXE)

$(EXE):argconfig.o suffix.o report.o

argconfig.o: $(ARGCONFIG)/argconfig.c $(ARGCONFIG)/argconfig.h $(ARGCONFIG)/suffix.h
	$(CC) -c $(CFLAGS) $(ARGCONFIG)/argconfig.c

suffix.o: $(ARGCONFIG)/suffix.c $(ARGCONFIG)/suffix.h
	$(CC) -c $(CFLAGS) $(ARGCONFIG)/suffix.c

report.o: $(ARGCONFIG)/report.c $(ARGCONFIG)/report.h $(ARGCONFIG)/suffix.h
	$(CC) -c $(CFLAGS) $(ARGCONFIG)/report.c

clean:
	rm -rf $(EXE) *.o *~
