# esp-motion-reporter-poc

## Usage

To provision WiFi, use provisioning app:

* [Android BLE Provisioning app](https://play.google.com/store/apps/details?id=com.espressif.provble)
* [iOS BLE Provisioning app](https://apps.apple.com/in/app/esp-ble-provisioning/id1473590141)

To initiate provisioning mode, reset the device twice (double tap reset in about 2s interval). Status LED will start flashing rapidly.

## Development

Prepare [ESP-IDF development environment](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#get-started-get-prerequisites)
.

Configure application with

```
idf.py menuconfig
```

and select `Application configuration` in root menu and configure server URL.

Flash it via

```
idf.py -b 921600 build flash monitor
```

As an alternative, you can use [PlatformIO](https://docs.platformio.org/en/latest/core/installation.html) to build and
flash the project.

## Dependencies

* https://github.com/natanaeljr/esp32-MPU-driver.git
* https://github.com/natanaeljr/esp32-I2Cbus.git
* https://github.com/UncleRus/esp-idf-lib.git (HMC5883L)
* https://github.com/mdvorak-iot/esp-status-led.git
* https://github.com/mdvorak-iot/esp-wifi-reconnect.git
* https://github.com/mdvorak-iot/esp-double-reset.git
* https://github.com/mdvorak-iot/esp-app-wifi.git
