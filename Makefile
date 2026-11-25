CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
PKG_CONFIG ?= pkg-config
SERVER_PKGS = libcjson sqlite3 libcrypto
CLIENT_PKGS = libcjson

.PHONY: all clean test
all: server client

server: src/auth.h src/action.h src/auth.c src/events.c src/list.c src/server.c src/service.c src/net.c src/net.h src/service.h Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(shell $(PKG_CONFIG) --cflags $(SERVER_PKGS)) $(LDFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS) $(shell $(PKG_CONFIG) --libs $(SERVER_PKGS))

client: src/client.c src/net.c src/net.h Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags $(CLIENT_PKGS)) $(LDFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS) $(shell $(PKG_CONFIG) --libs $(CLIENT_PKGS))

tests/framing: tests/framing.c src/net.c src/net.h Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $(shell $(PKG_CONFIG) --cflags $(CLIENT_PKGS)) $(LDFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS) $(shell $(PKG_CONFIG) --libs $(CLIENT_PKGS))

test: all tests/framing
	./tests/framing
	python3 tests/integration.py

clean:
	$(RM) server client tests/framing
