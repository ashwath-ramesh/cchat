CC = gcc
CFLAGS = -Wall -Wextra -std=c17
CFLAGS_DEBUG = -Wall -Wextra -std=c17 -g -O0 -DDEBUG

.PHONY: all build-server build-debug run-server run-debug run-bridge open-client clean

all: build-server

# ─── Server ─────────────────────────────────────────────────────────────────

build-server: cchat-server

cchat-server: server/server.c server/utils.c
	$(CC) $(CFLAGS) -o cchat-server server/server.c server/utils.c

run-server: cchat-server
	./cchat-server

# ─── Debug ──────────────────────────────────────────────────────────────────

build-debug: cchat-server-debug

cchat-server-debug: server/server.c server/utils.c
	$(CC) $(CFLAGS_DEBUG) -o cchat-server-debug server/server.c server/utils.c

run-debug: cchat-server-debug
	./cchat-server-debug

# ─── Bridge ─────────────────────────────────────────────────────────────────

run-bridge:
	@cd bridge && npm install --silent && node bridge.js

# ─── Client ─────────────────────────────────────────────────────────────────

open-client:
	@open client/client.html || xdg-open client/client.html

# ─── Clean ──────────────────────────────────────────────────────────────────

clean:
	rm -f cchat-server cchat-server-debug
