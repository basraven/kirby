# Kirby - Smart Tube Fan
Making a PWM Tube Fan smart with an ESP8266 and a temperature sensor

## Contents
This project consists of:
- ESP8266 firmware - can be found at [/src](/src)
  - Exposes the content of [/frontend](/frontend)
  - Exposes metrics data in [Prometheus exporter formatting](https://prometheus.io/docs/instrumenting/writing_exporters/)
- Application frontend - displays ESP8266 temperature and output PWM data


## How to use
Install the following dependencies
- Docker
- VSCode with PlatformIO

### Build Frontend

```bash
cd /frontend
build.sh
```

### Build Firmware
In platformIO: 
1.  Build filesystem image
2.  Upload filesystem image - This will upload the firmware to spiffs
3.  Build
4.  Upload - This will upload the actual firmware