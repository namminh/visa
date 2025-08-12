CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -pthread

# Try to discover PostgreSQL include/lib paths via pg_config or pkg-config
PG_CFLAGS := $(shell pg_config --includedir 2>/dev/null | sed 's/^/-I/')
PG_LDFLAGS := $(shell pg_config --libdir 2>/dev/null | sed 's/^/-L/')
ifeq ($(strip $(PG_CFLAGS)),)
  # Fallback to pkg-config if available
  PG_CFLAGS := $(shell pkg-config --cflags libpq 2>/dev/null)
  PG_LDFLAGS := $(shell pkg-config --libs-only-L libpq 2>/dev/null)
endif

# Directories
SERVER_DIR := server
CLIENT_DIR := client
BUILD_DIR  := build

# Source files
SERVER_SOURCES := $(wildcard $(SERVER_DIR)/*.c)
CLIENT_SOURCES := $(wildcard $(CLIENT_DIR)/*.c)

# Objects
SERVER_OBJS := $(patsubst $(SERVER_DIR)/%.c,$(BUILD_DIR)/%.o,$(SERVER_SOURCES))
CLIENT_OBJS := $(patsubst $(CLIENT_DIR)/%.c,$(BUILD_DIR)/%.o,$(CLIENT_SOURCES))

.PHONY: all clean

all: $(BUILD_DIR) server client

# Build server executable
server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(PG_LDFLAGS) -o $(BUILD_DIR)/server $^ -lpq

# Build client executable
client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/loadgen $^

# Compile source files into objects
$(BUILD_DIR)/%.o: $(SERVER_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(PG_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(CLIENT_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Remove built files
clean:
	rm -rf $(BUILD_DIR)
