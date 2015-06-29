LDFLAGS += -libverbs
CFLAGS += -std=c99
default: rdmatool

clean:
	rm -rf rdmatool *.o *~
