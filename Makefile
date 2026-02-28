CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic -pthread
LDFLAGS := -pthread

SRC_DIR := src
BIN_DIR := bin

TCP_THREAD_SERVER := $(BIN_DIR)/tcp_thread_server
TCP_EVENT_SERVER := $(BIN_DIR)/tcp_event_server
UNIX_THREAD_SERVER := $(BIN_DIR)/unix_domain_thread_server
UNIX_EVENT_SERVER := $(BIN_DIR)/unix_domain_event_server
TCP_CLIENT := $(BIN_DIR)/tcp_client
UNIX_CLIENT := $(BIN_DIR)/unix_domain_client

TARGETS := $(TCP_THREAD_SERVER) $(TCP_EVENT_SERVER) $(UNIX_THREAD_SERVER) $(UNIX_EVENT_SERVER) $(TCP_CLIENT) $(UNIX_CLIENT)

.PHONY: all clean

all: $(TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(TCP_THREAD_SERVER): $(SRC_DIR)/tcp_thread_server.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(TCP_EVENT_SERVER): $(SRC_DIR)/tcp_event_server.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(UNIX_THREAD_SERVER): $(SRC_DIR)/unix_domain_thread_server.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(UNIX_EVENT_SERVER): $(SRC_DIR)/unix_domain_event_server.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(TCP_CLIENT): $(SRC_DIR)/tcp_client.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(UNIX_CLIENT): $(SRC_DIR)/unix_domain_client.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)