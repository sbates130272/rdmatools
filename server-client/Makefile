EXE = rdma_server rdma_client

LDLIBS += -libverbs -lrdmacm
CFLAGS += -std=c99 -D_GNU_SOURCE

default: $(EXE)

clean:
	rm -rf $(EXE) *.o *~
