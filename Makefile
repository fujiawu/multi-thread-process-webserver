CFLAGS := -Wall --std=gnu99 
CC := gcc
CURL_LIBS := $(shell curl-config --libs)
CURL_CFLAGS := $(shell curl-config --cflags)


ARCH := $(shell uname)
ifneq ($(ARCH),Darwin)
  LDFLAGS += -pthread
endif

all: indent webserver webclient simplecached

webserver: steque.o webserver.o
	$(CC) -o $@ $(CFLAGS) $(CURL_CFLAGS) $^ $(LDFLAGS) $(CURL_LIBS)

webclient: steque.o webclient.o
	$(CC) -o $@ $(CFLAGS) $(CURL_CFLAGS) $^ $(LDFLAGS) $(CURL_LIBS)

simplecached: simplecache.o simplecached.o steque.o
	$(CC) -o $@ $(CFLAGS) $^ $(LDFLAGS)

.PHONY: clean ctags indent

clean:
	rm -fr *.o webserver webclient simplecached *.c~ *.h~

ctags:
	ctags -R -f .tags --fields=+iaS --extra=+q

indent:
	indent *.c

