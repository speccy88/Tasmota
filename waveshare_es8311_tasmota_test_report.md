# Waveshare ESP32-S3-RLCD-4.2 ES8311 test report

## Root cause
ES8311 initialization was tied to I2C bus readiness and needed to run against Tasmota's bus1 index mapping. The fix now defers/retries codec init until bus1 is ready, and the codec initializes successfully during boot.

## Files modified
- `tasmota/tasmota_xdrv_driver/xdrv_42_0_i2s_audio_idf51.ino`
- `tasmota/tasmota_xdrv_driver/xdrv_42_4_i2s_codecs.ino`

## Code changes
- Added deferred/retry ES8311 init state.
- Switched codec I2C access to the Tasmota bus1 mapping.
- Added retry hooks before playback and microphone start.
- Added periodic retry from the I2S event loop.
- Forced amplifier GPIO46 on before playback.
- Added codec init/debug logs and register readback.

## Build flags
- `USE_I2S_AUDIO`
- `USE_I2S_MIC`
- `USE_BERRY`
- `USE_UFILESYS`
- `ESP32S3_RLCD_4_2`

## Build command
```bash
pio run -e tasmota32s3-lvgl
```

## Flash command
```bash
pio run -e tasmota32s3-lvgl -t upload --upload-port /dev/cu.usbmodem1101
```

## Serial port
- `/dev/cu.usbmodem1101`

## Template after flash
```json
{"NAME":"ESP32-S3-RLCD-4.2","ARCH":"ESP32S3","GPIO":[6210,0,0,0,4704,800,0,0,7776,7808,7872,736,704,640,608,0,9376,0,32,0,0,705,0,0,0,0,0,737,673,0,0,0,0,0,7840,224,0,0],"FLAG":0,"BASE":1}
```

## Commands sent
```text
WebLog 4
SerialLog 4
Restart 1
I2CScan
Template
I2SConfig
Status 0
I2SConfig {"Sys":{"Duplex":1,"Tx":1,"Rx":1,"Exclusive":0},"Tx":{"SampleRate":16000,"Gain":80,"Mode":0,"Channels":2,"SlotMask":3},"Rx":{"SampleRate":16000,"Gain":30,"Mode":0,"Channels":1,"SlotMask":1,"SlotWidth":32}}
Power1 ON
Power2 ON
I2SStop
I2SPlay /sample2.mp3
I2SMic
I2SRec 10,/mic.wav
I2SStop
I2SPlay /mic.wav
```

## Relevant boot and test logs
```text
00:00:00.134 I2C: Bus1 using GPIO14(SCL) and GPIO13(SDA)
00:00:01.092 I2S: amplifier GPIO46 -> OFF
00:00:01.093 I2S: initializing ES8311 codec on I2C bus1 addr 0x18
00:00:01.094 I2C: ES8311-I2C found at 0x18 on bus1
00:00:01.095 I2S: ES8311 detected at 0x18
00:00:01.095 I2S: ES8311 init start (slave, Philips, 16-bit)
00:00:01.109 I2S: ES8311 reset done
00:00:01.113 I2S: ES8311 clocks/I2S format configured
00:00:01.128 I2S: ES8311 DAC enabled
00:00:01.129 I2S: ES8311 ADC enabled
00:00:01.129 I2S: ES8311 output unmuted (mute=0)
00:00:01.129 I2S: ES8311 volume set
00:00:01.132 I2S: ES8311 registers SYS0D=0x01 SYS0E=0x02 SYS12=0x00 DAC31=0x00 DAC32=0xC0
00:00:01.133 I2S: ES8311 init complete
00:00:01.175 I2S: codec init summary ES8311=0x00000000 ES7210=0x00000000 ready=1
00:00:06.394 RSL: RESULT = {"I2CScan":"Device(s) found on bus1 at 0x18 0x40 0x51 0x70"}
00:00:06.453 RSL: RESULT = {"I2SConfig":{"Sys":{"Version":2,"Duplex":1,"Tx":1,"Rx":1,"Exclusive":0,"MclkInv0":0,"MclkInv1":0,"BclkInv0":0,"BclkInv1":0,"WsInv0":0,"WsInv1":0,"Mp3Preallocate":0},"Tx":{"SampleRate":16000,"Gain":80,"Mode":0,"SlotMask":3,"SlotConfig":0,"Channels":2,"APLL":1},"Rx":{"SampleRate":16000,"Gain":30,"Mode":0,"SlotMask":1,"SlotWidth":32,"Channels":1,"DCFilterAlpha":32511,"LowpassAlpha":17719,"APLL":1,"WsWidth":32,"WsPol":0,"BitShift":1,"LeftAlign":1,"BigEndian":0,"LsbOrder":0,"DMAFrame":576,"DMADesc":3}}}
00:00:07.083 I2S: amplifier GPIO46 -> OFF
00:00:07.088 I2S: amplifier GPIO46 -> ON before playback
00:00:07.140 RSL: RESULT = {"I2SPlay":"Started"}
00:00:07.146 I2S: ES8311 TX sample rate configured to 44100 Hz
00:00:07.305 RSL: RESULT = {"I2SMic":"Done"}
00:00:07.353 RSL: RESULT = {"I2SRec":"Did not launch recording task"}
00:00:07.474 RSL: RESULT = {"I2SPlay":"File not found"}
```

## Notes
- `I2SRec` still reports that recording did not launch.
- `I2SPlay /mic.wav` failed because the file was not present.

## 2026-05-17 follow-up

The failed recording command was not reaching the recording task because `I2sRecord()` expected the command index to be an encoder selector. An unsuffixed `I2SRec ...` passed encoder `0`, which was unsupported. The attempted command also used a timed WAV-recording shape (`I2SRec 10,/mic.wav`) while the existing command path only handled MP3/Opus encoders selected by command suffix.

Changes made:
- Added `WAV_ENCODER` and `WAV_DECODER`.
- Added a lightweight PCM WAV file encoder for microphone captures.
- Added extension-based default encoder selection for `I2SRec`:
  - `.wav` -> WAV
  - `.opus` / `.webm` -> Opus when enabled
  - everything else -> MP3
- Added extension-based default decoder selection for `I2SPlay`:
  - `.wav` -> WAV
  - `.opus` / `.webm` -> Opus when enabled
  - `.aac` / `.m4a` -> AAC when enabled
  - everything else -> MP3
- Added parsing for `I2SRec <seconds>,<path>` and automatic stop after the requested duration.
- Initialized the microphone task encoder pointer and write-result variable defensively.

Build verification:

```bash
pio run -e tasmota32s3-lvgl
```

Result: success.

Next on-device test sequence:

```text
WebLog 4
SerialLog 4
I2SStop
I2SConfig
I2SMic
I2SRec 10,/mic.wav
I2SRec -?
I2SPlay /mic.wav
```

Expected behavior:
- `I2SRec 10,/mic.wav` should respond with `/mic.wav` instead of `Did not launch recording task`.
- The recording task should stop itself after about 10 seconds.
- `I2SPlay /mic.wav` should select the WAV decoder automatically and attempt playback.

Still needs hardware validation:
- Confirm that the WAV file is created in the filesystem.
- Confirm that recorded audio has a valid signal level and is not silent/noisy.
- Confirm whether the raw I2S recording path handles the board's configured RX slot width correctly, or whether it needs to use the filtered `readMic()` path instead of direct `i2s_channel_read()`.
