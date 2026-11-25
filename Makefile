ifeq ($(OS),Windows_NT)
ifeq ($(origin CC),default)
ifneq ($(wildcard C:/MinGW/bin/gcc.exe),)
CC := C:/MinGW/bin/gcc.exe
else
CC := gcc
endif
endif
EXE_EXT := .exe
else
CC ?= gcc
EXE_EXT :=
endif
CFLAGS ?= -std=c17 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?= -Iinclude -Itests
LDFLAGS ?=
LDLIBS ?= -lm
PYTHON ?= python
BUILD_DIR ?= build
CORE_SRC := $(wildcard src/*.c vendor/bosch/*.c)
HEADERS := $(wildcard include/*.h vendor/bosch/*.h)
TEST_BIN := $(BUILD_DIR)/test_core$(EXE_EXT)
SIGNAL_TEST_BIN := $(BUILD_DIR)/test_signal$(EXE_EXT)
RECOVERY_TEST_BIN := $(BUILD_DIR)/test_runtime_recovery$(EXE_EXT)
SIM_BIN := $(BUILD_DIR)/whm_simulator$(EXE_EXT)

.PHONY: all test simulator dashboard clean

all: test simulator

ifeq ($(OS),Windows_NT)
MKDIR_CMD = powershell -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(BUILD_DIR)' | Out-Null"
else
MKDIR_CMD = mkdir -p $(BUILD_DIR)
endif

$(BUILD_DIR):
	$(MKDIR_CMD)

$(TEST_BIN): $(CORE_SRC) $(HEADERS) tests/test_core.c tests/test_support.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_SRC) tests/test_core.c $(LDFLAGS) $(LDLIBS) -o $@

$(SIGNAL_TEST_BIN): src/whm_signal.c tests/test_signal.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) src/whm_signal.c tests/test_signal.c $(LDFLAGS) $(LDLIBS) -o $@

$(RECOVERY_TEST_BIN): $(CORE_SRC) tests/test_runtime_recovery.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_SRC) tests/test_runtime_recovery.c $(LDFLAGS) $(LDLIBS) -o $@

$(SIM_BIN): $(CORE_SRC) $(HEADERS) app/simulator.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE_SRC) app/simulator.c $(LDFLAGS) $(LDLIBS) -o $@

test: $(TEST_BIN) $(SIGNAL_TEST_BIN) $(RECOVERY_TEST_BIN) $(SIM_BIN)
	$(TEST_BIN)
	$(SIGNAL_TEST_BIN)
	$(RECOVERY_TEST_BIN)
	$(PYTHON) -B -m unittest discover -s tests -p test_dashboard.py -v

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
