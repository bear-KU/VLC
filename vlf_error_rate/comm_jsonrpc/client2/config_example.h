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
#define RECEIVE_BUFFER_SIZE 1024
#define SEND_BUFFER_SIZE 1100

#endif // CONFIG_H
