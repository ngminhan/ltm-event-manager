CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic

PKG_CONFIG ?= pkg-config
CJSON_CFLAGS = $(shell $(PKG_CONFIG) --cflags libcjson)
CJSON_LIBS = $(shell $(PKG_CONFIG) --libs libcjson)

.PHONY: all clean check-cjson

all: server client

server: src/server.c Makefile | check-cjson
	$(CC) $(CPPFLAGS) $(CJSON_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS) $(CJSON_LIBS)

client: src/client.c Makefile | check-cjson
	$(CC) $(CPPFLAGS) $(CJSON_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS) $(CJSON_LIBS)

check-cjson:
	@$(PKG_CONFIG) --exists libcjson || { \
		echo "cJSON development package or pkg-config not found. See README.md." >&2; \
		exit 1; \
	}

clean:
	$(RM) server client
