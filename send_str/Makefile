# Makefile

PORT = /dev/ttyUSB1
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

.PHONY: all compile upload screen
