# Makefile

PORT = /dev/ttyUSB0
FQBN = esp32:esp32:esp32
PROJECT_DIR = .
BAUDRATE = 115200

# Default Targets
all: compile upload

# Compile
compile:
	arduino-cli compile --fqbn $(FQBN) $(PROJECT_DIR)

# Upload
upload:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(PROJECT_DIR)

# Serial Monitor
screen:
	screen $(PORT) $(BAUDRATE)

# Clean Build Files
clean:
	rm -rf $(PROJECT_DIR)/build

.PHONY: all compile upload screen crean
