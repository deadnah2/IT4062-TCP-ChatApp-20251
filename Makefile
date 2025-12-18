CC=gcc
CFLAGS=-Wall -Wextra -O2 -pthread -I.
BUILD=build

COMMON_SRC=common/framing.c common/protocol.c
SERVER_SRC=server/server.c server/handlers.c server/accounts.c server/sessions.c
CLIENT_SRC=client/client.c

all: $(BUILD) $(BUILD)/server $(BUILD)/client

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/server: $(COMMON_SRC) $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/client: $(COMMON_SRC) $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf $(BUILD)

.PHONY: all clean
