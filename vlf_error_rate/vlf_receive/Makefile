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

# Screen
screen:
	screen $(PORT) $(BAUDRATE)

# Log Output
log:
	rm -f screenlog.0
	screen -L $(PORT) $(BAUDRATE)

# Clean Build Files
clean:
	rm -rf $(PROJECT_DIR)/build
	rm screenlog.0

.PHONY: all compile upload log crean
