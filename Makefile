ifeq ($(OS),Windows_NT)
CC := C:/MinGW/bin/gcc.exe
else
CC ?= gcc
endif
CFLAGS ?= -std=c17 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?= -Iinclude -Itests
LDFLAGS ?=
LDLIBS ?= -lm
PYTHON ?= python
BUILD_DIR ?= build
CORE_SRC := $(wildcard src/*.c)
TEST_BIN := $(BUILD_DIR)/test_core.exe
SIM_BIN := $(BUILD_DIR)/whm_simulator.exe

.PHONY: all test simulator dashboard clean

all: test simulator

ifeq ($(OS),Windows_NT)
MKDIR_CMD = powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(BUILD_DIR)' | Out-Null"
else
MKDIR_CMD = mkdir -p $(BUILD_DIR)
endif

$(BUILD_DIR):
	$(MKDIR_CMD)

$(TEST_BIN): $(CORE_SRC) tests/test_core.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_SRC) tests/test_core.c $(LDFLAGS) $(LDLIBS) -o $@

$(SIM_BIN): $(CORE_SRC) app/simulator.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_SRC) app/simulator.c $(LDFLAGS) $(LDLIBS) -o $@

test: $(TEST_BIN)
	$(TEST_BIN)

simulator: $(SIM_BIN)
	$(SIM_BIN) --seconds 12 --mode nominal

dashboard: $(SIM_BIN)
	$(PYTHON) app/dashboard_server.py

ifeq ($(OS),Windows_NT)
CLEAN_CMD = powershell -NoProfile -Command "if (Test-Path -LiteralPath '$(BUILD_DIR)') { Remove-Item -LiteralPath '$(BUILD_DIR)' -Recurse -Force }"
else
CLEAN_CMD = rm -rf $(BUILD_DIR)
endif

clean:
	$(CLEAN_CMD)
