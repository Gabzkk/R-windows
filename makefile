# Makefile - Kestrel-7 Multi-Platform Agent Build Suite
# Compatible with MSVC (cl.exe) on Windows and MinGW-w64 (x86_64-w64-mingw32-g++) on Linux

TARGET = kestrel7_agent.exe
BIN_DIR = bin
SRC_DIR = src
SRCS = $(SRC_DIR)/main.cpp $(SRC_DIR)/crypto.cpp $(SRC_DIR)/network.cpp $(SRC_DIR)/evasion.cpp $(SRC_DIR)/persistence.cpp $(SRC_DIR)/utils.cpp

# Detect environment / compiler
ifeq ($(OS),Windows_NT)
    CXX ?= cl.exe
    MKDIR = if not exist $(BIN_DIR) mkdir $(BIN_DIR)
    RM = rmdir /s /q $(BIN_DIR) 2>nul
    CFLAGS = /EHsc /MT /O2 /GS- /DNDEBUG /D_WIN32_WINNT=0x0A00 /Fe:$(BIN_DIR)/$(TARGET)
    LIBS = ws2_32.lib winhttp.lib advapi32.lib bcrypt.lib psapi.lib user32.lib
    BUILD_CMD = $(CXX) $(CFLAGS) $(SRCS) /link $(LIBS)
else
    CXX ?= x86_64-w64-mingw32-g++
    MKDIR = mkdir -p $(BIN_DIR)
    RM = rm -rf $(BIN_DIR)
    CFLAGS = -O2 -s -DNDEBUG -D_WIN32_WINNT=0x0A00 -mwindows -static -static-libgcc -static-libstdc++
    LIBS = -lws2_32 -lwinhttp -ladvapi32 -lbcrypt -lpsapi -luser32
    BUILD_CMD = $(CXX) $(CFLAGS) $(SRCS) -o $(BIN_DIR)/$(TARGET) $(LIBS)
endif

all: release

release:
	@$(MKDIR)
	@echo [*] Compiling Kestrel-7 Enterprise Agent ($(TARGET))...
	$(BUILD_CMD)
	@echo [+] Build complete: $(BIN_DIR)/$(TARGET)

clean:
	@echo [*] Cleaning build artifacts...
	@$(RM)

help:
	@echo =======================================================
	@echo   Kestrel-7 Windows 11 Enterprise Agent Makefile
	@echo =======================================================
	@echo   make release   - Build release binary (bin/kestrel7_agent.exe)
	@echo   make clean     - Clean build directory
	@echo =======================================================

.PHONY: all release clean help
