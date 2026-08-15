#pragma once

// Copy this file to secrets.h (same folder) and fill it in. secrets.h is
// gitignored — nothing here should ever be committed with real values.
//
// Without a secrets.h the sketch still builds and simply runs with the radio
// off: no Wi-Fi, no OTA, everything else identical. That's what keeps
// `make compile-all` green on a fresh clone.

#define WIFI_SSID     "your-network-here"
#define WIFI_PASSWORD "your-password-here"
#define OTA_HOSTNAME  "turn-counter"    // reachable at <hostname>.local
#define OTA_PASSWORD  "change-me"       // required to push an OTA update
