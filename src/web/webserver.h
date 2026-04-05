#pragma once
#include <WebServer.h>

extern WebServer webServer;

void setupWebServer();

// MQTT config API endpoint
void handleApiMqtt();
