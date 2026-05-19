# Project Notes for Codex

## Board and Project Goal

This checkout is being used for a side project to support the Waveshare ESP32-S3-RLCD-4.2 board in Tasmota.

- Board: Waveshare ESP32-S3-RLCD-4.2
- Vendor documentation/specs: https://docs.waveshare.com/ESP32-S3-RLCD-4.2
- Current PlatformIO environment: `tasmota32s3-lvgl`
- Current PlatformIO board: `esp32s3-qio_qspi`
- Project build define: `ESP32S3_RLCD_4_2`

The goal is to produce a working Tasmota binary for this board with access to all onboard features and peripherals.

Much of the required support already exists in upstream Tasmota and can be adapted for this board. Some board-specific changes are needed, including changes to the universal display driver to make the screen work on the ESP32-S3-RLCD-4.2. The Tasmota maintainers do not want to integrate those driver changes upstream, so this repository is maintained as a side project focused only on making Tasmota work well on this Waveshare board.

## Current Focus

The next project phase is making sure Tasmota can access all peripherals on the ESP32-S3-RLCD-4.2.

The current active step is microphone and speaker support, including the onboard ES8311 audio codec and I2S audio path.

Before working on microphone, speaker, I2S, ES8311, or related GPIO/peripheral behavior, read:

- `waveshare_es8311_tasmota_test_report.md`

That report documents the current status, test commands, logs, modified files, build flags, known working pieces, and remaining issues.

## Useful Local Commands

Build the active firmware:

```bash
pio run -e tasmota32s3-lvgl
```

Flash the board, when connected on the documented serial port:

```bash
pio run -e tasmota32s3-lvgl -t upload --upload-port /dev/cu.usbmodem1101
```

