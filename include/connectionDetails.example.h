#pragma once

#define WIFI_ACCESSPOINT "network1"
#define WIFI_ACCESSPOINT_PASSWORD "password1"

#define WIFI_ACCESSPOINT1 "network2"
#define WIFI_ACCESSPOINT_PASSWORD1 "password2"

int MQTT_MAX_PACKET_SIZE = 256;
const char* MQTT_SERVERADDRESS = "192.168.1.55";
const char* MQTT_CLIENTNAME = "espADSBMonitor";
const char* ARDUINO_OTA_URI_SUFFIX = "/firmware";
const char* ARDUINO_OTA_UPDATE_USERNAME = "admin";
const char* ARDUINO_OTA_UPDATE_PASSWORD = "testpassword";