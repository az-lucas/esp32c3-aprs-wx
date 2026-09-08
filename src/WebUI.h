#pragma once

// Starts the local HTTP status server on port 80. Call once after WiFi is
// connected.
void webUiStart();

// Must be called every loop() iteration to service HTTP clients.
void webUiHandle();
