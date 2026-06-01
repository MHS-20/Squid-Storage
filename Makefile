# CMake Wrapper Makefile
BUILD_DIR  = build
CLIENT_TARGET = SquidStorageClient
SRV_TARGET = SquidStorageServer
DN_TARGET  = DataNode

.PHONY: all init build clean run-client run-server run-datanode install uninstall

all: build

init:
	cmake -B $(BUILD_DIR) -S .

build:
	@if [ ! -d "$(BUILD_DIR)" ]; then \
		echo "Error: '$(BUILD_DIR)' folder missing. Run 'make init' first."; \
		exit 1; \
	fi
	cmake --build $(BUILD_DIR)

clean:
	cmake --build $(BUILD_DIR) --target clean

run-client: build
	./$(BUILD_DIR)/$(CLIENT_TARGET)

run-server: build
	./$(BUILD_DIR)/$(SRV_TARGET)

run-datanode: build
	./$(BUILD_DIR)/$(DN_TARGET)

install: build
	sudo cmake --install $(BUILD_DIR)

uninstall:
	@if [ -f "$(BUILD_DIR)/install_manifest.txt" ]; then \
		echo "Uninstalling binaries tracked in install_manifest.txt..."; \
		sudo xargs rm -f < $(BUILD_DIR)/install_manifest.txt; \
		echo "Clean uninstallation complete."; \
	else \
		echo "Error: install_manifest.txt not found inside '$(BUILD_DIR)'. Was the project installed?"; \
		exit 1; \
	fi

