// secrets.example.h - copy this file to secrets.h (same folder) and fill it in.
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
// The ESP32 signs in anonymously with it (enable Authentication -> Sign-in
// method -> Anonymous). Leave it "" only if the database rules allow access
// without signing in (test mode).
#define FIREBASE_API_KEY ""
