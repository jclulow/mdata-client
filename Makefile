
CC = gcc

UNAME_S := $(shell uname -s)
PLATFORM_OK = false

CFILES = main.c
CFLAGS = -Wall -Wextra
LDLIBS =

ifeq ($(UNAME_S),SunOS)
	CFILES += plat/sunos.c
	LDLIBS += -lnsl -lsocket -lsmbios
	PLATFORM_OK = true
endif

ifeq ($(PLATFORM_OK),false)
	$(error Unknown platform: $(UNAME_S))
endif


.PHONY:	all
all:	mdata-get

mdata-get:	$(CFILES)
	$(CC) $(CFLAGS) $(LDLIBS) -o $@ $^

.PHONY:	clean
clean:
	rm -f mdata-get

