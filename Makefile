CC = gcc
CFLAGS = -Wall -g -pthread
UNAME_S := $(shell uname -s)

# Platform-specific flags
ifeq ($(UNAME_S),Linux)
    LDFLAGS = -pthread -lrt
else
    LDFLAGS = -pthread
endif

all: findsp findst

findsp: findsp.c
	$(CC) $(CFLAGS) -o findsp findsp.c $(LDFLAGS)

findst: findst.c
	$(CC) $(CFLAGS) -o findst findst.c $(LDFLAGS)

clean:
	rm -f findsp findst
	rm -f split-* intermediate-* output-*
	rm -f outp1.txt outp2.txt
	rm -f *.o