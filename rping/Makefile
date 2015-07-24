EXE = rping

LDLIBS += -libverbs -lrdmacm -lpthread
CFLAGS += -std=c99 -D_GNU_SOURCE

default: $(EXE)

clean:
	rm -rf $(EXE) *.o *~
