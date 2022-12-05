# ESP Fish Feeder

ESP32 controlling a device that releases fish food at regular intervals.

## Hardware

- ESP32 dev kit
- 4-pin stepper motor / driver
- Limit switch
- 2 push-buttons

## Install

Setup typical ESP-IDF environment (tested on IDF 4.2). Menu options for `ESP-Fish-Feeder`. Set timezone according to POSIX rules.

## Behaviour

Device will sleep until the start of sunrise or sunset, then wake every minute to update the color during the transitions. WIFI will be activated on first start as time establishment is required, then after light update if 24 hours has passed since the last update.

Blue lights mean initial loading/time sync. Red lights mean failed initial load.
