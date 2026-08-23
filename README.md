# CareLink-Firmware
Embedded firmware for the CareLink wearable, managing ESP32-C3, LoRa communication, GPS, IMU, power management, and SOS detection.

## ESP-IDF Project

This repository is structured as an ESP-IDF project:

- `CMakeLists.txt` defines the project entry point.
- `main/CMakeLists.txt` registers the application component and includes `main/include`.
- `main/main.c` contains the initial `app_main` task.

Build and flash with ESP-IDF:

```sh
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```
