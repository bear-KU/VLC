// Example configuration file for ESP32 client
// Change the values as needed for your network and server settings.
// This file should be saved as config.h in the same directory as your main code file.

#pragma once

#ifndef CONFIG_H
#define CONFIG_H

// WiFi configuration
const char *ssid = "exampleSSID";
const char *password = "examplePassword";

// Server configuration
const char *serverIP = "123.456.789.012";
const int serverPort = 12345;

// Buffer sizes
#define BINARY_BUFFER_SIZE 1024
#define HEX_DATA_BUFFER_SIZE 256
#define ROLE_SIZE 10

#endif // CONFIG_H
