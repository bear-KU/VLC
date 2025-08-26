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
#define CALLBACK_METHOD_SIZE 64

// Communication timeout
#define COMMUNICATION_TIMEOUT 5000000  // [μs]
/*
 * The timeout is set to 5000 ms to account for the worst-case transmission time.
 * - When all data bits are 1 and the payload size is 1024, the transmission time is maximized.
 * - Assuming a base signal length of T, the total signal duration is:
 *   T (leader) + (T + 2T) * 1024 (data) + (T + 5T) (trailer) = 3079T.
 * - If T is set to 1 ms, the maximum transmission time becomes 3079 ms.
 * - To allow a safety margin, the timeout is set to 5000 ms.
 * - If the signal length T is set longer than 1 ms, please adjust COMMUNICATION_TIMEOUT accordingly.
 */

#endif // CONFIG_H
