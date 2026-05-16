![Tasmota logo](/tools/logo/TASMOTA_FullLogo_Vector.svg#gh-light-mode-only)![Tasmota logo](/tools/logo/TASMOTA_FullLogo_Vector_White.svg#gh-dark-mode-only)


## Waveshare ESP32-S3 RLCD 4.2 (Tasmota port)

This repository includes ongoing work to support the [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/esp32-s3-rlcd-4.2.htm), a reflective-LCD board based on **ESP32-S3-WROOM-1-N16R8**.

Key board specs:
- ESP32-S3 dual-core Xtensa LX7 up to 240 MHz
- 16 MB flash and 8 MB PSRAM
- 4.2" ST7305 reflective LCD (300x400, black/white, SPI)
- 2.4 GHz Wi-Fi and Bluetooth 5 (LE)
- ES7210 + ES8311 audio path, dual microphones, speaker header
- SHTC3 temperature/humidity sensor and PCF85063 RTC
- TF card slot, USB-C programming/logging, and expansion headers

Flash Tasmota build (`tasmota32s3-lvgl`):

```bash
esptool --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 921600 write-flash -z 0xE0000 Tasmota/.pio/build/tasmota32s3-lvgl/firmware.bin
```

Full factory flash image:

```bash
esptool --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 921600 write-flash -z 0x0 /Tasmota/build_output/firmware/tasmota32s3-lvgl.factory.bin
```

## Support Information

<img src="https://user-images.githubusercontent.com/5904370/68332933-e6e5a600-00d7-11ea-885d-50395f7239a1.png" width=150 align="right" />

For a database of supported devices see [Tasmota Device Templates Repository](https://templates.blakadder.com)

If you're looking for support on **Tasmota** there are some options available:

### Documentation

* [Documentation Site](https://tasmota.github.io/docs): For information on how to flash Tasmota, configure, use and expand it
* [FAQ and Troubleshooting](https://tasmota.github.io/docs/FAQ/): For information on common problems and solutions.
* [Commands Information](https://tasmota.github.io/docs/Commands): For information on all the commands supported by Tasmota.

### Support's Community

* [Tasmota Discussions](https://github.com/arendst/Tasmota/discussions): For Tasmota usage questions, Feature Requests and Projects.
* [Tasmota Users Chat](https://discord.gg/Ks2Kzd4): For support, troubleshooting and general questions. You have better chances to get fast answers from members of the Tasmota Community.
* [Search in Issues](https://github.com/arendst/Tasmota/issues): You might find an answer to your question by searching current or closed issues.
* [Software Problem Report](https://github.com/arendst/Tasmota/issues/new?template=Bug_report.md): For reporting problems of Tasmota Software.

### Unofficial Community Resources
* [Tasmota-DE](https://t.me/TasmotaDE): A German-language Telegram group related to Tasmota.

## Contribute

You can contribute to Tasmota by
- Providing Pull Requests (Features, Proof of Concepts, Language files or Fixes)
- Testing new released features and report issues
- Donating to acquire hardware for testing and implementing or out of gratitude
- Contributing missing [documentation](https://tasmota.github.io/docs) for features and devices

[![donate](https://img.shields.io/badge/donate-PayPal-blue.svg)](https://paypal.me/tasmota)

## Credits

People helping to keep the show on the road:
- Sfromis providing extensive user support
- Barbudor providing user support and code fixes and additions
- David Lang providing initial issue resolution and code optimizations
- Heiko Krupp for his IRSend, HTU21, SI70xx and Wemo/Hue emulation drivers
- Wiktor Schmidt for Travis CI implementation
- Thom Dietrich for PlatformIO optimizations
- Marinus van den Broek for his EspEasy groundwork
- Pete Ba for more user friendly energy monitor calibration
- Lobradov providing compile optimization tips
- Flexiti for his initial timer implementation
- reloxx13 for his [TasmoAdmin](https://github.com/reloxx13/TasmoAdmin) management tool
- Joachim Banzhaf for his TSL2561 library and driver
- Andre Thomas for providing many drivers
- Gijs Noorlander for his MHZ19, SenseAir and updated PubSubClient drivers
- Erik Montnemery for his HomeAssistant Discovery concept and many code tuning tips
- Federico Leoni for continued HomeAssistant Discovery support
- Aidan Mountford for his HSB support
- Daniel Ztolnai for his Serial Bridge implementation
- Gerhard Mutz for multiple sensor & display drivers, Sunrise/Sunset, and scripting
- Nuno Ferreira for his HC-SR04 driver
- Adrian Scillato for his (security)fixes and implementing and maintaining KNX
- Gennaro Tortone for implementing and maintaining Eastron drivers
- Raymond Mouthaan for managing Wemos Wiki information
- Norbert Richter for his [decode-config.py](https://github.com/tasmota/decode-config) tool
- Joel Stein, digiblur and Shantur Rathore for their Tuya research and driver
- Frogmore42 for providing many issue answers
- Jason2866 for platformio support and providing many issue answers
- Blakadder for managing the document site and providing template management
- Stephan Hadinger for refactoring light driver, enhancing HueEmulation, LVGL, Zigbee and Berry support
- tmo for designing the official Tasmota logo
- Stefan Bode for his Shutter and Deep sleep drivers
- Jacek Ziółkowski for his [TDM](https://github.com/jziolkowski/tdm) management tool and [Tasmotizer](https://github.com/tasmota/tasmotizer) flashing tool
- Christian Staars for NRF24L01 and HM-10 Bluetooth sensor support
- Paul Diem for UDP Group communication support
- Jörg Schüler-Maroldt for his initial ESP32 port
- Javier Arigita for his thermostat driver
- Simon Hailes for ESP32 Bluetooth extensions
- Many more providing Tips, Wips, Pocs, PRs and Donations

## License

This program is licensed under GPL-3.0-only
