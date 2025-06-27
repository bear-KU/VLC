# Makefile

PORT = /dev/ttyUSB1
FQBN = esp32:esp32:esp32s3
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

# Log Output
log:
	rm -f screenlog.0
	screen -L $(PORT) $(BAUDRATE)

# Clean up
clean:
	rm screenlog.0

.PHONY: all compile upload log
