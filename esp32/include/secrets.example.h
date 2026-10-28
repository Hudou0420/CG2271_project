// Copy to ../../Project_Secrets/secrets.h relative to esp32/, outside the repo.
//
// secrets.h is in .gitignore: never commit it or put it in the submission zip.
// Without secrets.h the firmware still builds and runs, with the cloud link off.
#pragma once

// 2.4 GHz network (the ESP32-S2 has no 5 GHz radio), e.g. a phone hotspot.
#define WIFI_SSID     "your-hotspot-name"
#define WIFI_PASSWORD "your-hotspot-password"

// Firebase console -> Realtime Database -> the URL at the top, no trailing slash.
#define FIREBASE_DB_URL "https://your-project-default-rtdb.asia-southeast1.firebasedatabase.app"

// Firebase console -> Project settings -> General -> Web API key.
// Enable Email/Password authentication. Use a separate device account;
// never use the shared dashboard owner account for the ESP32.
#define FIREBASE_API_KEY ""

// Create the device user in Firebase Authentication and grant its UID access
// under /access/devices/<uid> = true before publishing protected rules.
// Keep these credentials in the external secrets.h, never in browser config.
#define FIREBASE_DEVICE_EMAIL ""
#define FIREBASE_DEVICE_PASSWORD ""

// Google AI Studio -> create an API key in a project WITHOUT paid billing.
// Free-tier availability/quotas vary. Never put this key in VITE_* variables.
#define GEMINI_API_KEY ""
#define GEMINI_MODEL "gemini-2.5-flash-lite"
