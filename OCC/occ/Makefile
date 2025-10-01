# Makefile

# BOARD ?= esp32s3

ifeq ($(BOARD), esp32s3)
  FQBN = esp32:esp32:esp32s3
  PORT = /dev/ueda_ESP32S3
  BAUDRATE = 115200
  LED_PIN = 4
else ifeq ($(BOARD), esp32)
  FQBN = esp32:esp32:esp32
  PORT = /dev/ueda_ESP32
  BAUDRATE = 115200
  LED_PIN = 32
else
  $(error Unknown BOARD_TYPE specified: $(BOARD))
endif

PROJECT_DIR = .


# Default Targets
all: compile upload

# Compile
compile:
	arduino-cli compile --fqbn $(FQBN) --build-property build.extra_flags="-DLED_PIN=$(LED_PIN)" $(PROJECT_DIR)

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

.PHONY: all compile upload screen log clean
