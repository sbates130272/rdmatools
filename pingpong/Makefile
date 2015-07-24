EXE = rc_pingpong

LDLIBS += -libverbs -lrdmacm
CFLAGS += -std=c99 -D_GNU_SOURCE

default: $(EXE)

$(EXE): pingpong.o

clean:
	rm -rf $(EXE) *.o *~
