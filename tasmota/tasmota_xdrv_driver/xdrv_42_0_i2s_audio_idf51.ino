/*
  xdrv_42_i2s_audio.ino - Audio dac support for Tasmota

  Copyright (C) 2021  Gerhard Mutz and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef ESP32
#if defined(USE_I2S_AUDIO) && ESP_IDF_VERSION_MAJOR >= 5

#include <ESP8266SAM.h>

#include "AudioFileSourcePROGMEM.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorMP3.h"
#include "AudioGeneratorWAV.h"

#include "AudioFileSourceFS.h"
#include "AudioGeneratorTalkie.h"
#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#if defined(ESP32S3_RLCD_4_2)
#include "driver/gpio.h"
#include "driver/i2s_tdm.h"
#endif
#ifdef USE_I2S_AAC
#include "AudioGeneratorAAC.h"
#endif // USE_I2S_AAC
#ifdef USE_I2S_OPUS
#include "AudioGeneratorOpus.h"
#endif // USE_I2S_OPUS

#include <layer3.h>

#define XDRV_42           42

#define USE_I2S_SAY
#define USE_I2S_SAY_TIME
#define USE_I2S_RTTTL
#define USE_I2S_WEBRADIO
#define USE_I2S_MP3
#define USE_I2S_DEBUG       // remove before flight

// Macros used in audio sub-functions
#undef AUDIO_PWR_ON
#undef AUDIO_PWR_OFF
#define AUDIO_PWR_ON    I2SAudioPower(true);
#define AUDIO_PWR_OFF   I2SAudioPower(false);

#define AUDIO_CONFIG_FILENAME "/.drvset042"

#if defined(ESP32S3_RLCD_4_2)
static constexpr int16_t kRlcdBeepAmplitude = 7000;
#endif

extern FS *ufsp;
extern FS *ffsp;

#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
void S3boxAudioPower(uint8_t pwr);
void S3boxInit(void);
bool S3boxCodecReady(void);
bool EnsureES8311Initialized(void);
void S3boxDumpCodec(const char *tag);
void S3boxForcePlaybackCodec(uint32_t hz);
void S3boxCodecPeriodic(void);
#if defined(ESP32S3_RLCD_4_2)
void S3boxApplyCodecVolume(void);
#endif
#endif

constexpr int preallocateBufferSize = 16*1024;
constexpr int preallocateCodecSize  = 29192; // MP3 codec max mem needed
constexpr int preallocateCodecSizeAAC = 85332; // AAC+SBR codec max mem needed

void sayTime(int hour, int minutes);
void Cmndwav2mp3(void);
void Cmd_Time(void);

void Rtttl(char *buffer);
void CmndI2SRtttl(void);
void I2sWebRadioStopPlaying(void);
void CmndI2SMP3Stream(void);
void CmndI2SBeep(void);
void CmndI2SCodec(void);
void I2SAudioPower(bool power);

#if defined(ESP32S3_RLCD_4_2)
static void RlcdLogAudioPinActivity(const char *tag, uint32_t sample_us) {
  const gpio_num_t pins[] = { GPIO_NUM_16, GPIO_NUM_9, GPIO_NUM_45, GPIO_NUM_8, GPIO_NUM_46 };
  const char *names[] = { "MCLK16", "BCLK9", "WS45", "DOUT8", "PA46" };
  constexpr uint32_t pin_count = sizeof(pins) / sizeof(pins[0]);
  uint32_t changes[pin_count] = {0};
  int last[pin_count];
  int first[pin_count];
  for (uint32_t i = 0; i < pin_count; i++) {
    first[i] = last[i] = gpio_get_level(pins[i]);
  }

  uint32_t loops = 0;
  uint32_t start = micros();
  while ((uint32_t)(micros() - start) < sample_us) {
    loops++;
    for (uint32_t i = 0; i < pin_count; i++) {
      int level = gpio_get_level(pins[i]);
      if (level != last[i]) {
        changes[i]++;
        last[i] = level;
      }
    }
  }
  AddLog(LOG_LEVEL_INFO,
         "I2S: pin activity %s %uus loops=%u %s:%u %d>%d %s:%u %d>%d %s:%u %d>%d %s:%u %d>%d %s:%u %d>%d",
         tag ? tag : "", sample_us, loops,
         names[0], changes[0], first[0], last[0],
         names[1], changes[1], first[1], last[1],
         names[2], changes[2], first[2], last[2],
         names[3], changes[3], first[3], last[3],
         names[4], changes[4], first[4], last[4]);
}

static i2s_chan_handle_t g_rlcd_factory_tx = nullptr;
static i2s_chan_handle_t g_rlcd_factory_rx = nullptr;

static void RlcdFactoryI2SDelete(void) {
  if (g_rlcd_factory_rx) {
    esp_err_t err = i2s_channel_disable(g_rlcd_factory_rx);
    AddLog(LOG_LEVEL_DEBUG, "I2S: factory RX disable err=0x%04X", err);
    err = i2s_del_channel(g_rlcd_factory_rx);
    AddLog(LOG_LEVEL_INFO, "I2S: factory RX delete err=0x%04X", err);
    g_rlcd_factory_rx = nullptr;
  }
  if (g_rlcd_factory_tx) {
    esp_err_t err = i2s_channel_disable(g_rlcd_factory_tx);
    AddLog(LOG_LEVEL_DEBUG, "I2S: factory TX disable err=0x%04X", err);
    err = i2s_del_channel(g_rlcd_factory_tx);
    AddLog(LOG_LEVEL_INFO, "I2S: factory TX delete err=0x%04X", err);
    g_rlcd_factory_tx = nullptr;
  }
}

static void RlcdReleaseTasmotaI2SHandles(void) {
  if (audio_i2s.in && (audio_i2s.in == audio_i2s.out)) {
    AddLog(LOG_LEVEL_INFO, "I2S: releasing shared Tasmota I2S handles");
    audio_i2s.in->releaseHandles();
    return;
  }
  if (audio_i2s.in) {
    AddLog(LOG_LEVEL_INFO, "I2S: releasing Tasmota RX handle");
    audio_i2s.in->releaseHandles();
  }
  if (audio_i2s.out) {
    AddLog(LOG_LEVEL_INFO, "I2S: releasing Tasmota TX handle");
    audio_i2s.out->releaseHandles();
  }
}

static bool RlcdFactoryI2SInit(uint32_t initial_rate) {
  RlcdFactoryI2SDelete();
  RlcdReleaseTasmotaI2SHandles();

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  chan_cfg.id = I2S_NUM_AUTO;   // Matches Waveshare codec_board _i2s_init()
  esp_err_t err = i2s_new_channel(&chan_cfg, &g_rlcd_factory_tx, &g_rlcd_factory_rx);
  AddLog(LOG_LEVEL_INFO, "I2S: factory i2s_new_channel tx=%p rx=%p err=0x%04X",
         g_rlcd_factory_tx, g_rlcd_factory_rx, err);
  if (err != ESP_OK || !g_rlcd_factory_tx || !g_rlcd_factory_rx) {
    RlcdFactoryI2SDelete();
    return false;
  }

  i2s_tdm_slot_mask_t init_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3);
  i2s_tdm_config_t tdm_cfg = {
    .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(initial_rate),
    .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO, init_mask),
    .gpio_cfg = {
      .mclk = GPIO_NUM_16,
      .bclk = GPIO_NUM_9,
      .ws = GPIO_NUM_45,
      .dout = GPIO_NUM_8,
      .din = GPIO_NUM_10,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };
  tdm_cfg.slot_cfg.total_slot = 4;

  err = i2s_channel_init_tdm_mode(g_rlcd_factory_tx, &tdm_cfg);
  AddLog(LOG_LEVEL_INFO, "I2S: factory TX init TDM 32bit/4slot/%u err=0x%04X", initial_rate, err);
  if (err != ESP_OK) {
    RlcdFactoryI2SDelete();
    return false;
  }
  err = i2s_channel_init_tdm_mode(g_rlcd_factory_rx, &tdm_cfg);
  AddLog(LOG_LEVEL_INFO, "I2S: factory RX init TDM 32bit/4slot/%u err=0x%04X", initial_rate, err);
  if (err != ESP_OK) {
    RlcdFactoryI2SDelete();
    return false;
  }

  err = i2s_channel_enable(g_rlcd_factory_tx);
  AddLog(LOG_LEVEL_INFO, "I2S: factory TX enable err=0x%04X", err);
  if (err != ESP_OK) {
    RlcdFactoryI2SDelete();
    return false;
  }
  err = i2s_channel_enable(g_rlcd_factory_rx);
  AddLog(LOG_LEVEL_INFO, "I2S: factory RX enable err=0x%04X", err);
  if (err != ESP_OK) {
    RlcdFactoryI2SDelete();
    return false;
  }

  RlcdLogAudioPinActivity("factory-init", 2000);
  return true;
}

static bool RlcdFactoryI2SSetPlayback(uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample) {
  if (!g_rlcd_factory_tx) { return false; }

  esp_err_t err = i2s_channel_disable(g_rlcd_factory_tx);
  AddLog(LOG_LEVEL_INFO, "I2S: factory TX disable before fs err=0x%04X", err);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { return false; }

  i2s_tdm_slot_mask_t slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1);
  i2s_tdm_slot_config_t slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
    (i2s_data_bit_width_t)bits_per_sample,
    I2S_SLOT_MODE_STEREO,
    slot_mask);
  slot_cfg.total_slot = channels;
  slot_cfg.slot_bit_width = (bits_per_sample == 32) ? I2S_SLOT_BIT_WIDTH_32BIT : I2S_SLOT_BIT_WIDTH_16BIT;

  err = i2s_channel_reconfig_tdm_slot(g_rlcd_factory_tx, &slot_cfg);
  AddLog(LOG_LEVEL_INFO, "I2S: factory TX slot bits=%u channels=%u mask=0x%X err=0x%04X",
         bits_per_sample, channels, slot_mask, err);
  if (err != ESP_OK) { return false; }

  i2s_tdm_clk_config_t clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(sample_rate);
  err = i2s_channel_reconfig_tdm_clock(g_rlcd_factory_tx, &clk_cfg);
  AddLog(LOG_LEVEL_INFO, "I2S: factory TX clock rate=%u err=0x%04X", sample_rate, err);
  if (err != ESP_OK) { return false; }

  err = i2s_channel_enable(g_rlcd_factory_tx);
  AddLog(LOG_LEVEL_INFO, "I2S: factory TX enable after fs err=0x%04X", err);
  if (err != ESP_OK) { return false; }

  RlcdLogAudioPinActivity("factory-playback", 2000);
  return true;
}

static bool RlcdFactoryBeep(uint32_t duration_ms, uint32_t tone_hz) {
  constexpr uint32_t sample_rate = 24000;
  if (!RlcdFactoryI2SInit(16000)) {
    AddLog(LOG_LEVEL_ERROR, "I2S: factory beep I2S init failed");
    return false;
  }

  S3boxForcePlaybackCodec(sample_rate);
  I2SAudioPower(true);
  if (!RlcdFactoryI2SSetPlayback(sample_rate, 2, 16)) {
    I2SAudioPower(false);
    RlcdFactoryI2SDelete();
    return false;
  }

  uint32_t total_frames = (sample_rate * duration_ms) / 1000;
  uint32_t frame_index = 0;
  uint32_t samples_per_half_period = sample_rate / (tone_hz * 2);
  if (!samples_per_half_period) { samples_per_half_period = 1; }

  uint32_t writes = 0;
  uint32_t total_bytes_written = 0;
  AddLog(LOG_LEVEL_INFO, "I2S: factory direct beep %u ms at %u Hz sample_rate=%u", duration_ms, tone_hz, sample_rate);
  while (frame_index < total_frames) {
    int16_t pcm[128 * 2];
    uint32_t frames = total_frames - frame_index;
    if (frames > 128) { frames = 128; }
    for (uint32_t i = 0; i < frames; i++) {
      int16_t value = (((frame_index + i) / samples_per_half_period) & 1) ? kRlcdBeepAmplitude : -kRlcdBeepAmplitude;
      pcm[(i * 2)] = value;
      pcm[(i * 2) + 1] = value;
    }
    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(g_rlcd_factory_tx, pcm, frames * 2 * sizeof(int16_t), &bytes_written, 1000);
    writes++;
    total_bytes_written += bytes_written;
    if (writes <= 2) {
      AddLog(LOG_LEVEL_INFO, "I2S: factory write #%u requested=%u wrote=%u err=0x%04X",
             writes, frames * 2 * sizeof(int16_t), bytes_written, err);
      if (writes == 2) { RlcdLogAudioPinActivity("factory-write", 2000); }
    }
    if (err != ESP_OK) {
      AddLog(LOG_LEVEL_ERROR, "I2S: factory write failed err=0x%04X bytes=%u", err, bytes_written);
      break;
    }
    frame_index += frames;
  }
  AddLog(LOG_LEVEL_INFO, "I2S: factory beep wrote total=%u bytes in %u writes", total_bytes_written, writes);

  delay(250);
  I2SAudioPower(false);
  RlcdFactoryI2SDelete();
  return total_bytes_written > 0;
}

#if defined(RLCD_ENABLE_LEGACY_I2S_TEST)
static bool RlcdLegacyBeep(uint32_t duration_ms, uint32_t tone_hz) {
  constexpr i2s_port_t port = I2S_NUM_0;
  constexpr uint32_t sample_rate = 24000;

  RlcdFactoryI2SDelete();
  RlcdReleaseTasmotaI2SHandles();
  i2s_driver_uninstall(port);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 3,
    .dma_buf_len = 300,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
  };
  esp_err_t err = i2s_driver_install(port, &i2s_config, 0, nullptr);
  AddLog(LOG_LEVEL_INFO, "I2S: legacy driver install err=0x%04X", err);
  if (err != ESP_OK) { return false; }

  i2s_pin_config_t pin_config = {
    .mck_io_num = GPIO_NUM_16,
    .bck_io_num = GPIO_NUM_9,
    .ws_io_num = GPIO_NUM_45,
    .data_out_num = GPIO_NUM_8,
    .data_in_num = GPIO_NUM_10,
  };
  err = i2s_set_pin(port, &pin_config);
  AddLog(LOG_LEVEL_INFO, "I2S: legacy set_pin mclk=16 bclk=9 ws=45 dout=8 din=10 err=0x%04X", err);
  if (err != ESP_OK) {
    i2s_driver_uninstall(port);
    return false;
  }

  S3boxForcePlaybackCodec(sample_rate);
  I2SAudioPower(true);
  err = i2s_set_clk(port, sample_rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  AddLog(LOG_LEVEL_INFO, "I2S: legacy set_clk %u/16/stereo err=0x%04X", sample_rate, err);
  if (err != ESP_OK) {
    I2SAudioPower(false);
    i2s_driver_uninstall(port);
    return false;
  }
  i2s_zero_dma_buffer(port);
  i2s_start(port);
  RlcdLogAudioPinActivity("legacy-start", 2000);

  uint32_t total_frames = (sample_rate * duration_ms) / 1000;
  uint32_t frame_index = 0;
  uint32_t samples_per_half_period = sample_rate / (tone_hz * 2);
  if (!samples_per_half_period) { samples_per_half_period = 1; }

  uint32_t writes = 0;
  uint32_t total_bytes_written = 0;
  AddLog(LOG_LEVEL_INFO, "I2S: legacy beep %u ms at %u Hz sample_rate=%u", duration_ms, tone_hz, sample_rate);
  while (frame_index < total_frames) {
    int16_t pcm[128 * 2];
    uint32_t frames = total_frames - frame_index;
    if (frames > 128) { frames = 128; }
    for (uint32_t i = 0; i < frames; i++) {
      int16_t value = (((frame_index + i) / samples_per_half_period) & 1) ? kRlcdBeepAmplitude : -kRlcdBeepAmplitude;
      pcm[(i * 2)] = value;
      pcm[(i * 2) + 1] = value;
    }
    size_t bytes_written = 0;
    err = i2s_write(port, pcm, frames * 2 * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(1000));
    writes++;
    total_bytes_written += bytes_written;
    if (writes <= 2) {
      AddLog(LOG_LEVEL_INFO, "I2S: legacy write #%u requested=%u wrote=%u err=0x%04X",
             writes, frames * 2 * sizeof(int16_t), bytes_written, err);
      if (writes == 2) { RlcdLogAudioPinActivity("legacy-write", 2000); }
    }
    if (err != ESP_OK) {
      AddLog(LOG_LEVEL_ERROR, "I2S: legacy write failed err=0x%04X bytes=%u", err, bytes_written);
      break;
    }
    frame_index += frames;
  }
  AddLog(LOG_LEVEL_INFO, "I2S: legacy beep wrote total=%u bytes in %u writes", total_bytes_written, writes);

  delay(250);
  I2SAudioPower(false);
  i2s_stop(port);
  i2s_driver_uninstall(port);
  return total_bytes_written > 0;
}
#endif
#endif

/*********************************************************************************************\
 * More structures
\*********************************************************************************************/

struct AUDIO_I2S_MP3_t {
#ifdef USE_I2S_MP3
  AudioGenerator *decoder = nullptr;
  AudioFileSource *file = nullptr;
  AudioFileSource *id3 = nullptr;
  AudioFileSource *buff = NULL;

  void *preallocateBuffer = NULL;
  void *preallocateCodec = NULL;
#endif // USE_I2S_MP3

#if defined(USE_I2S_MP3) || defined(USE_I2S_WEBRADIO) || defined(USE_SHINE) || defined(MP3_MIC_STREAM)
  TaskHandle_t mp3_task_handle;
  TaskHandle_t mic_task_handle;
  char audio_title[64];
#endif // defined(USE_I2S_MP3) || defined(USE_I2S_WEBRADIO)

  char mic_path[32];
  int8_t mic_error;
  uint32_t mic_duration_limit;
  volatile bool mic_stop = false;
  bool use_stream = false;
  bool task_running = false;
  bool file_play_pausing = false;
  bool task_has_ended = false;
  bool file_has_paused = false;
  bool task_loop_mode = false;
  uint8_t current_file_type = 0; // for resumed playback
  uint32_t paused_position = 0; // position in the file for paused audio file

// RECORD/STREAM/ENCODING
  uint32_t recdur;
  uint8_t encoder_type;
  bool  stream_active;
  bool  stream_enable;
  WiFiClient client;
  ESP8266WebServer *StreamServer;

// I2S_BRIDGE
  BRIDGE_MODE bridge_mode;
  WiFiUDP i2s_bridge_udp;
  WiFiUDP i2s_bridgec_udp;
  IPAddress i2s_bridge_ip;
  TaskHandle_t i2s_bridge_h;
  int8_t ptt_pin = -1;

} audio_i2s_mp3;

struct AUDIO_I2S_WEBRADIO_t {
  AudioFileSourceICYStream *ifile = NULL;
} Audio_webradio;

#define I2S_AUDIO_MODE_MIC 1
#define I2S_AUDIO_MODE_SPK 2

/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

#ifdef USE_WEBSERVER
const char HTTP_I2SAUDIO[] PROGMEM =
   "{s}" "Audio:" "{m}%s{e}";

void I2sWrShow(bool json) {
    if (audio_i2s_mp3.decoder) {
      if (json) {
        ResponseAppend_P(PSTR(",\"Audio\":{\"Title\":\"%s\"}"), audio_i2s_mp3.audio_title);
      } else {
        WSContentSend_PD(HTTP_I2SAUDIO,audio_i2s_mp3.audio_title);
      }
    }
}
#endif  // USE_WEBSERVER

/*********************************************************************************************\
 * Commands definitions
\*********************************************************************************************/

const char kI2SAudio_Commands[] PROGMEM = "I2S|"
  "Gain|Rec|Stop|Config"
#ifdef USE_I2S_MP3
  "|Play|Loop|Pause"
#endif
#ifdef USE_I2S_DEBUG
  "|Mic|Beep|Codec"      // debug only
#endif // USE_I2S_DEBUG
#ifdef USE_I2S_WEBRADIO
  "|WR"
#endif // USE_I2S_WEBRADIO
#ifdef USE_I2S_SAY
  "|Say"
#endif // USE_I2S_SAY
#ifdef USE_I2S_SAY_TIME
  "|Time"
#endif // USE_I2S_SAY_TIME
#ifdef USE_I2S_RTTTL
  "|Rtttl"
#endif
#if defined(USE_SHINE) && defined(MP3_MIC_STREAM)
  "|Stream"
#endif // MP3_MIC_STREAM
#ifdef USE_I2S_BRIDGE
  "|Bridge"
#endif // USE_I2S_BRIDGE
;

void (* const I2SAudio_Command[])(void) PROGMEM = {
  &CmndI2SGain, &CmndI2SMicRec, &CmndI2SStop, &CmndI2SConfig,
#ifdef USE_I2S_MP3
  &CmndI2SPlay,
  &CmndI2SLoop,
  &CmndI2SPause,
#endif
#ifdef USE_I2S_DEBUG
  &CmndI2SMic,
  &CmndI2SBeep,
  &CmndI2SCodec,
#endif // USE_I2S_DEBUG
#ifdef USE_I2S_WEBRADIO
  &CmndI2SWebRadio,
#endif // USE_I2S_WEBRADIO
#ifdef USE_I2S_SAY
  &CmndI2SSay,
#endif // USE_I2S_SAY
#ifdef USE_I2S_SAY_TIME
  &Cmd_Time,
#endif // USE_I2S_SAY_TIME
#ifdef USE_I2S_RTTTL
  &CmndI2SI2SRtttl,
#endif
#if defined(USE_SHINE) && defined(MP3_MIC_STREAM)
  &CmndI2SMP3Stream,
#endif // MP3_MIC_STREAM
#ifdef USE_I2S_BRIDGE
  &CmndI2SBridge,
#endif // I2S_BRIDGE
};


/*********************************************************************************************\
 * I2S configuration
\*********************************************************************************************/

void CmndI2SConfig(void) {
  if(!audio_i2s.Settings){
    ResponseCmndChar_P(PSTR("no valid settings"));
    return;
  }
  tI2SSettings * cfg = audio_i2s.Settings;

  // if (zigbee.init_phase) { ResponseCmndChar_P(PSTR(D_ZIGBEE_NOT_STARTED)); return; }
  TrimSpace(XdrvMailbox.data);
  if (strlen(XdrvMailbox.data) > 0) {
    JsonParser parser(XdrvMailbox.data);
    JsonParserObject root = parser.getRootObject();
    if (!root) { ResponseCmndChar_P(D_JSON_INVALID_JSON); return; }

    // remove "I2SConfig" primary key if present
    JsonParserToken config_tk = root[D_PRFX_I2S D_JSON_I2S_CONFIG];
    if ((bool)config_tk) {
      root = config_tk.getObject();
    }

    JsonParserToken sys_tk = root["Sys"];
    if (sys_tk.isObject()) {
      JsonParserObject sys = sys_tk.getObject();
      cfg->sys.mclk_inv[0] = sys.getUInt(PSTR("MclkInv0"), cfg->sys.mclk_inv[0]);
      cfg->sys.mclk_inv[1] = sys.getUInt(PSTR("MclkInv1"), cfg->sys.mclk_inv[1]);
      cfg->sys.bclk_inv[0] = sys.getUInt(PSTR("BclkInv0"), cfg->sys.bclk_inv[0]);
      cfg->sys.bclk_inv[1] = sys.getUInt(PSTR("BclkInv1"), cfg->sys.bclk_inv[1]);
      cfg->sys.ws_inv[0] = sys.getUInt(PSTR("WsInv0"), cfg->sys.ws_inv[0]);
      cfg->sys.ws_inv[1] = sys.getUInt(PSTR("WsInv1"), cfg->sys.ws_inv[1]);
      cfg->sys.mp3_preallocate = sys.getUInt(PSTR("Mp3Preallocate"), cfg->sys.mp3_preallocate);
#if defined(ESP32S3_RLCD_4_2)
      cfg->sys.codec_volume = sys.getUInt(PSTR("CodecVolume"), cfg->sys.codec_volume);
      if (cfg->sys.codec_volume > 100) { cfg->sys.codec_volume = 100; }
#endif
    }

    JsonParserToken tx_tk = root["Tx"];
    if (tx_tk.isObject()) {
      JsonParserObject tx = tx_tk.getObject();
      cfg->tx.sample_rate = tx.getUInt(PSTR("SampleRate"), cfg->tx.sample_rate);
      cfg->tx.gain = tx.getUInt(PSTR("Gain"), cfg->tx.gain);
      cfg->tx.mode = tx.getUInt(PSTR("Mode"), cfg->tx.mode);
      cfg->tx.slot_mask = tx.getUInt(PSTR("SlotMask"), cfg->tx.slot_mask);
      cfg->tx.slot_config = tx.getUInt(PSTR("SlotConfig"), cfg->tx.slot_config);
      cfg->tx.channels = tx.getUInt(PSTR("Channels"), cfg->tx.channels);
      cfg->tx.apll = tx.getUInt(PSTR("APLL"), cfg->tx.apll);
    }

    JsonParserToken rx_tk = root["Rx"];
    if (rx_tk.isObject()) {
      JsonParserObject rx = rx_tk.getObject();
      cfg->rx.sample_rate = rx.getUInt(PSTR("SampleRate"), cfg->rx.sample_rate);
      float rx_gain = ((float)cfg->rx.gain) / 16;
      rx_gain = rx.getFloat(PSTR("Gain"), rx_gain);
      cfg->rx.gain = (uint16_t)(rx_gain * 16);
      // cfg->rx.gain = rx.getUInt(PSTR("Gain"), cfg->rx.gain);
      cfg->rx.mode = rx.getUInt(PSTR("Mode"), cfg->rx.mode);
      cfg->rx.slot_mask = rx.getUInt(PSTR("SlotMask"), cfg->rx.slot_mask);
      cfg->rx.slot_bit_width = rx.getUInt(PSTR("SlotWidth"), cfg->rx.slot_bit_width);
      cfg->rx.channels = rx.getUInt(PSTR("Channels"), cfg->rx.channels);
      cfg->rx.dc_filter_alpha = rx.getUInt(PSTR("DCFilterAlpha"), cfg->rx.dc_filter_alpha);
      cfg->rx.lowpass_alpha = rx.getUInt(PSTR("LowpassAlpha"), cfg->rx.lowpass_alpha);
      cfg->rx.apll = rx.getUInt(PSTR("APLL"), cfg->rx.apll);
      cfg->rx.ws_width = rx.getUInt(PSTR("WsWidth"), cfg->rx.ws_width);
      cfg->rx.ws_pol = rx.getUInt(PSTR("WsPol"), cfg->rx.ws_pol);
      cfg->rx.bit_shift = rx.getUInt(PSTR("BitShift"), cfg->rx.bit_shift);
      cfg->rx.left_align = rx.getUInt(PSTR("LeftAlign"), cfg->rx.left_align);
      cfg->rx.big_endian = rx.getUInt(PSTR("BigEndian"), cfg->rx.big_endian);
      cfg->rx.bit_order_lsb = rx.getUInt(PSTR("LsbOrder"), cfg->rx.bit_order_lsb);
      cfg->rx.dma_frame_num = rx.getUInt(PSTR("DMAFrame"), cfg->rx.dma_frame_num);
      cfg->rx.dma_desc_num = rx.getUInt(PSTR("DMADesc"), cfg->rx.dma_desc_num);
    }
    I2SSettingsSave(AUDIO_CONFIG_FILENAME);
#if defined(ESP32S3_RLCD_4_2)
    S3boxApplyCodecVolume();
#endif
  }

  float mic_gain = ((float)cfg->rx.gain) / 16;
  Response_P("{\"" D_PRFX_I2S D_JSON_I2S_CONFIG "\":{"
                  // Sys
                  "\"Sys\":{"
                    "\"Version\":%d,"
                    "\"Duplex\":%d,"
                    "\"Tx\":%d,"
                    "\"Rx\":%d,"
                    "\"Exclusive\":%d,"
                    "\"MclkInv0\":%d,"
                    "\"MclkInv1\":%d,"
                    "\"BclkInv0\":%d,"
                    "\"BclkInv1\":%d,"
                    "\"WsInv0\":%d,"
                    "\"WsInv1\":%d,"
                    "\"Mp3Preallocate\":%d"
#if defined(ESP32S3_RLCD_4_2)
                    ",\"CodecVolume\":%d"
#endif
                  "},"
                  "\"Tx\":{"
                    "\"SampleRate\":%d,"
                    "\"Gain\":%d,"
                    "\"Mode\":%d,"
                    "\"SlotMask\":%d,"
                    "\"SlotConfig\":%d,"
                    "\"Channels\":%d,"
                    "\"APLL\":%d"
                  "},"
                  "\"Rx\":{"
                    "\"SampleRate\":%d,"
                    "\"Gain\":%_f,"
                    // "\"Gain\":%d,"
                    "\"Mode\":%d,"
                    "\"SlotMask\":%d,"
                    "\"SlotWidth\":%d,"
                    "\"Channels\":%d,"
                    "\"DCFilterAlpha\":%d,"
                    "\"LowpassAlpha\":%d,"
                    "\"APLL\":%d,"
                    "\"WsWidth\":%d,"
                    "\"WsPol\":%d,"
                    "\"BitShift\":%d,"
                    "\"LeftAlign\":%d,"
                    "\"BigEndian\":%d,"
                    "\"LsbOrder\":%d,"
                    "\"DMAFrame\":%d,"
                    "\"DMADesc\":%d"
                  "}}}",
                  cfg->sys.version,
                  cfg->sys.full_duplex,
                  cfg->sys.tx,
                  cfg->sys.rx,
                  cfg->sys.exclusive,
                  cfg->sys.mclk_inv[0],
                  cfg->sys.mclk_inv[1],
                  cfg->sys.bclk_inv[0],
                  cfg->sys.bclk_inv[1],
                  cfg->sys.ws_inv[0],
                  cfg->sys.ws_inv[1],
                  cfg->sys.mp3_preallocate,
#if defined(ESP32S3_RLCD_4_2)
                  cfg->sys.codec_volume,
#endif
                  //
                  cfg->tx.sample_rate,
                  cfg->tx.gain,
                  cfg->tx.mode,
                  cfg->tx.slot_mask,
                  cfg->tx.slot_config,
                  cfg->tx.channels,
                  cfg->tx.apll,
                  //
                  cfg->rx.sample_rate,
                  &mic_gain,
                  // cfg->rx.gain,
                  cfg->rx.mode,
                  cfg->rx.slot_mask,
                  cfg->rx.slot_bit_width,
                  cfg->rx.channels,
                  cfg->rx.dc_filter_alpha,
                  cfg->rx.lowpass_alpha,
                  cfg->rx.apll,
                  cfg->rx.ws_width,
                  cfg->rx.ws_pol,
                  cfg->rx.bit_shift,
                  cfg->rx.left_align,
                  cfg->rx.big_endian,
                  cfg->rx.bit_order_lsb,
                  cfg->rx.dma_frame_num,
                  cfg->rx.dma_desc_num
                  );
}

/*********************************************************************************************\
 * Driver Settings load and save using filesystem
\*********************************************************************************************/
// error codes
enum {
  I2S_OK = 0,
  I2S_ERR_OUTPUT_NOT_CONFIGURED,
  I2S_ERR_INPUT_NOT_CONFIGURED,
  I2S_ERR_DECODER_IN_USE,
  I2S_ERR_DECODER_FAILED_TO_INIT,
  I2S_ERR_FILE_NOT_FOUND,
  I2S_ERR_TX_FAILED,
};

// signal to an external Berry driver that we turn audio power on or off
void I2SAudioPower(bool power) {
  callBerryEventDispatcher(PSTR("audio"), PSTR("power"), power, nullptr, 0);
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
  if (S3boxCodecReady()) {
    S3boxAudioPower(power ? 1 : 0);
  }
#endif
}

//
// I2SSettingsLoad(erase:bool)
//
// Load settings from file system.
// File is `/.drvset042`
void I2SSettingsLoad(const char * config_filename, bool erase) {
  // allocate memory for settings
  audio_i2s.Settings = new tI2SSettings();
  if (!audio_i2s.Settings) {
    AddLog(LOG_LEVEL_ERROR, "I2S: ERROR memory allocation failed");
    return;
  }
  if (!config_filename) { return; }     // if no filename, use defaults

#ifndef USE_UFILESYS
  AddLog(LOG_LEVEL_INFO, "I2S: use defaults as file system not enabled");
#else
  if (erase) {
    TfsDeleteFile(config_filename);  // Use defaults
  }
  else if (TfsLoadFile(config_filename, (uint8_t*)audio_i2s.Settings, sizeof(tI2SSettings))) {
    AddLog(LOG_LEVEL_INFO, "I2S: config loaded from file '%s'", config_filename);
    if ((audio_i2s.Settings->sys.version == 0) || (audio_i2s.Settings->sys.version > AUDIO_SETTINGS_VERSION)) {
      AddLog(LOG_LEVEL_DEBUG, "I2S: unsupported configuration version %u, use defaults", audio_i2s.Settings->sys.version);
      delete audio_i2s.Settings;
      audio_i2s.Settings = new tI2SSettings();
      I2SSettingsSave(config_filename);
    }
#if defined(ESP32S3_RLCD_4_2)
    else if (audio_i2s.Settings->sys.version < AUDIO_SETTINGS_VERSION) {
      audio_i2s.Settings->sys.codec_volume = RLCD_ES8311_DEFAULT_VOLUME;
      audio_i2s.Settings->sys.version = AUDIO_SETTINGS_VERSION;
      I2SSettingsSave(config_filename);
    }
#endif
  }
  else {
    // File system not ready: No flash space reserved for file system
    AddLog(LOG_LEVEL_DEBUG, "I2S: use defaults as file system not ready or file not found");
    I2SSettingsSave(config_filename);
  }
#endif  // USE_UFILESYS
}

void I2SSettingsSave(const char * config_filename) {
#ifdef USE_UFILESYS
  if (!config_filename) { return; }     // if no filename, use defaults

  if (TfsSaveFile(config_filename, (const uint8_t*)audio_i2s.Settings, sizeof(tI2SSettings))) {
    AddLog(LOG_LEVEL_DEBUG, "I2S: config saved to file '%s'", config_filename);
  } else {
    // File system not ready: No flash space reserved for file system
    AddLog(LOG_LEVEL_DEBUG, "I2S: ERROR file system not ready or unable to save file");
  }
#endif  // USE_UFILESYS
}

/*********************************************************************************************\
 * Driver init
\*********************************************************************************************/

//
// I2sInit
//
// Initialize I2S driver for input and output
void I2sInit(void) {
  int32_t gpio_din_0 = Pin(GPIO_I2S_DIN, 0);
  int32_t gpio_din_1 = Pin(GPIO_I2S_DIN, 1);
  int32_t gpio_dout_0 = Pin(GPIO_I2S_DOUT, 0);
  int32_t gpio_dout_1 = Pin(GPIO_I2S_DOUT, 1);
  int32_t gpio_ws_0 = Pin(GPIO_I2S_WS, 0);
  int32_t gpio_dac_0 = Pin(GPIO_I2S_DAC, 0);    // DAC-1 needs to be comfigured if DAC-2 is needed as well
  int32_t gpio_dac_1 = Pin(GPIO_I2S_DAC, 1);    // DAC-1 needs to be comfigured if DAC-2 is needed as well

  // we need at least one pin configured
  // Note: in case of ESP32 DAC output we may have only WS_0 configured. DAC is only supported on port 0
  if ((gpio_din_0 < 0) && (gpio_din_1 < 0) && (gpio_dout_0 < 0) && (gpio_dout_1 < 0) && (gpio_ws_0 < 0) && (gpio_dac_0 < 0)) {
    AddLog(LOG_LEVEL_DEBUG,PSTR("I2S: no pin configured"));
    return;
  }

  I2SSettingsLoad(AUDIO_CONFIG_FILENAME, false);    // load configuration (no-erase)
  if (!audio_i2s.Settings) { return; }     // fatal error, could not allocate memory for configuration

  bool tx_and_rx = false;      // the same ports are used for input and output
  bool exclusive = false;   // signals that in/out have a shared GPIO and need to un/install driver before use
  bool dac_mode = (gpio_dac_0 >= 0);
  if (dac_mode) {
    audio_i2s.Settings->tx.mode = I2S_MODE_DAC;
  }

  // AddLog(LOG_LEVEL_INFO, PSTR("I2S: init pins bclk=%d, ws=%d, dout=%d, mclk=%d, din=%d"),
  //                         Pin(GPIO_I2S_BCLK, 0) , Pin(GPIO_I2S_WS, 0), Pin(GPIO_I2S_DOUT, 0), Pin(GPIO_I2S_MCLK, 0), Pin(GPIO_I2S_DIN, 0));

  audio_i2s.Settings->sys.full_duplex = false;
  audio_i2s.Settings->sys.tx = false;
  audio_i2s.Settings->sys.rx = false;
  audio_i2s.Settings->sys.exclusive = false;

  for (uint32_t port = 0; port < SOC_I2S_NUM; port++) {
    int32_t bclk = Pin(GPIO_I2S_BCLK, port);
    int32_t ws = Pin(GPIO_I2S_WS, port);
    int32_t dout = Pin(GPIO_I2S_DOUT, port);
    int32_t mclk = Pin(GPIO_I2S_MCLK, port);
    int32_t din = Pin(GPIO_I2S_DIN, port);
    bool tx = false;      // is Tx enabled for this port
    bool rx = false;      // is Rx enabled for this port

    AddLog(LOG_LEVEL_DEBUG, "I2S: I2S%i bclk=%i, ws=%i, dout=%i, mclk=%i, din=%i", port, bclk, ws, dout, mclk, din);

    // if neither input, nor output, nor DAC/ADC skip (WS could is only needed for DAC but supports only port 0)
    if (din < 0 && dout < 0 && (!(dac_mode && port == 0))) { continue; }

    tx_and_rx = (din >= 0) && (dout >= 0); // portential full duplex
    if (tx_and_rx) {
      if (audio_i2s.Settings->rx.mode == I2S_MODE_PDM || audio_i2s.Settings->tx.mode == I2S_MODE_PDM ){
        exclusive = true;
      }
      AddLog(LOG_LEVEL_DEBUG, "I2S: enabling exclusive:%i", exclusive);
    }

    const char *err_msg = nullptr;   // to save code, we indicate an error with a message configured
    if (din >= 0 || dout >= 0) {
      // we have regular I2S configuration
      // do multiple checks
      // 1. check that WS is configured
      if (ws < 0) {
        // WS may be shared between both ports, so if it is configured on port 0, we accept it on port 1
        int32_t ws0 = Pin(GPIO_I2S_WS, 0);
        if (ws0 >= 0) {
          ws = ws0;
          AddLog(LOG_LEVEL_DEBUG, "I2S: I2S%i WS is shared, using WS from port 0 (%i)", port, ws);
          exclusive = true;
        }
        if (ws < 0) {
          err_msg = "no WS pin configured";
        }
      }
      // 2. check that DAC mode is not enabled for output(incompatible with DIN/DOUT)
      else if (dout >= 0 && audio_i2s.Settings->tx.mode == I2S_MODE_DAC) {
        err_msg = "DAC mode is not compatible with DOUT";
      }
      // 3. check that ADC mode is not enabled for output (incompatible with DIN/DOUT)
      else if (din >= 0 && audio_i2s.Settings->rx.mode == I2S_MODE_DAC) {
        err_msg = "ADC mode is not compatible with DIN";
      }
      // 4. check that output is not already configured
      else if (dout >= 0 && audio_i2s.out) {
        err_msg = "output already configured";
      }
      // 5. check that input is not already configured
      else if (din >= 0 && audio_i2s.in) {
        err_msg = "input already configured";
      }
      // 6. check that we don't try PDM on port 1
      else if (port != 0 && din >= 0 && audio_i2s.Settings->rx.mode == I2S_MODE_PDM) {
        err_msg = "PDM Rx is not supported";
      }
      // 7. check that we don't try PDM on port 1
      else if (port != 0 && dout >= 0 && audio_i2s.Settings->tx.mode == I2S_MODE_PDM) {
        err_msg = "PDM Tx is not supported";
      }
    } else {
      // no DIN/DOUT, try DAC mode
      // 1. Check that tx.mode is I2S_MODE_DAC
      if (dac_mode) {
        AddLog(LOG_LEVEL_DEBUG, "I2S: Configuraing DAC mode DAC0=%i DAC1=%i", gpio_dac_0, gpio_dac_1);
      } else {
        err_msg = "DAC mode is not enabled";
      }
    }

    // is there any error?
    if (err_msg) {
      AddLog(LOG_LEVEL_DEBUG, "I2S: Error: %s for I2S%i, skipping", err_msg, port);
      continue;   // skip this port
    }

    tx = (dout >= 0) || dac_mode;
    rx = (din >= 0);

    if (tx && audio_i2s.out) {
      AddLog(LOG_LEVEL_DEBUG, "I2S: Warning: Tx already configured, skipping superfluous Tx configuration");
      tx = false;
    }
    if (rx && audio_i2s.in) {
      AddLog(LOG_LEVEL_DEBUG, "I2S: Warning: Rx already configured, skipping superfluous Rx configuration");
      rx = false;
    }

    TasmotaI2S * i2s = new TasmotaI2S;
#if defined(ESP32S3_RLCD_4_2)
    if (tx) {
      audio_i2s.Settings->tx.mode = I2S_MODE_TDM;
      audio_i2s.Settings->tx.slot_config = I2S_SLOT_PHILIPS;
      audio_i2s.Settings->tx.slot_mask = BIT(0) | BIT(1);
      audio_i2s.Settings->tx.channels = 2;
      audio_i2s.Settings->tx.apll = 0;
    }
    if (rx) {
      audio_i2s.Settings->rx.mode = I2S_MODE_TDM;
      audio_i2s.Settings->rx.slot_mask = BIT(0);
      audio_i2s.Settings->rx.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
      audio_i2s.Settings->rx.channels = 1;
      audio_i2s.Settings->rx.apll = 0;
    }
#endif
    i2s->setPinout(bclk, ws, dout, mclk, din,
                    audio_i2s.Settings->sys.mclk_inv[0], audio_i2s.Settings->sys.bclk_inv[0],
                    audio_i2s.Settings->sys.ws_inv[0], audio_i2s.Settings->tx.apll);
    i2s->setSlotConfig((i2s_port_t)port, audio_i2s.Settings->tx.slot_config,
                      audio_i2s.Settings->tx.slot_mask, audio_i2s.Settings->rx.slot_mask);
    if (tx) {
      i2s->setTxMode(audio_i2s.Settings->tx.mode);
      // i2s->setTxChannels(audio_i2s.Settings->tx.channels);
      // i2s->setRate(audio_i2s.Settings->tx.sample_rate);
    }
    if (rx) {
      i2s->setRxMode(audio_i2s.Settings->rx.mode);
      i2s->setRxFreq(audio_i2s.Settings->rx.sample_rate);
      i2s->setRxChannels(audio_i2s.Settings->rx.channels);
      i2s->setRxGain(audio_i2s.Settings->rx.gain);
    }

    bool init_tx_ok = false;
    bool init_rx_ok = false;
    if (tx && rx && exclusive) {
      i2s->setExclusive(true);
      audio_i2s.Settings->sys.exclusive = exclusive;
      // in exclusive mode, we need to intialize in sequence Tx and Rx
      init_tx_ok = i2s->startI2SChannel(true, false);
      init_rx_ok = i2s->startI2SChannel(false, true);
    } else if (tx && rx) {
      init_tx_ok = init_rx_ok = i2s->startI2SChannel(true, true);
    } else {
      if (tx) { init_tx_ok = i2s->startI2SChannel(true, false); }
      if (rx) { init_rx_ok = i2s->startI2SChannel(false, true); }
    }
    if (init_tx_ok) { audio_i2s.out = i2s; }
    if (init_rx_ok) { audio_i2s.in = i2s; }
    audio_i2s.Settings->sys.tx |= init_tx_ok; // Do not set to zero id already configured on another channnel
    audio_i2s.Settings->sys.rx |= init_rx_ok;
    if (init_tx_ok && init_rx_ok && exclusive == false) { audio_i2s.Settings->sys.full_duplex = true; }

    // if intput and output are configured, don't proceed with other IS2 ports
    if (audio_i2s.out && audio_i2s.in) {
      break;
    }

  }

  // do we have exclusive mode?
  if (audio_i2s.out) { audio_i2s.out->setExclusive(exclusive); }
  if (audio_i2s.in) { audio_i2s.in->setExclusive(exclusive); }

  // if(audio_i2s.out != nullptr){
  //   audio_i2s.out->SetGain(((float)(audio_i2s.Settings->tx.gain + 1)/ 100.0));
  //   audio_i2s.out->beginTx();     // TODO - useful?
  //   audio_i2s.out->stopTx();
  // }
#ifdef USE_I2S_MP3
  audio_i2s_mp3.preallocateCodec = nullptr;
  if (audio_i2s.Settings->sys.mp3_preallocate == 1){
    // if (UsePSRAM()) {
    AddLog(LOG_LEVEL_DEBUG,PSTR("I2S: will allocate buffer for mp3 encoder"));
    audio_i2s_mp3.preallocateCodec = special_malloc(preallocateCodecSize);
  }
#endif // USE_I2S_MP3
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
#if defined(ESP32S3_RLCD_4_2)
  // Waveshare's factory firmware enables both I2S channels before codec init.
  // Some ES8311 setup writes appear to depend on live MCLK/BCLK/WS clocks.
  if (audio_i2s.out && audio_i2s.in && !exclusive) {
    bool boot_tx = audio_i2s.out->beginTx();
    uint32_t boot_rx = audio_i2s.in->startRx();
    AddLog(LOG_LEVEL_INFO, "I2S: RLCD boot clocks before codec init tx=%d rx=0x%04X", boot_tx, boot_rx);
  }
#endif
  S3boxInit();
#endif
  AddLog(LOG_LEVEL_DEBUG, "I2S: I2sInit done");
}

/*********************************************************************************************\
 * General functions
\*********************************************************************************************/

//
// I2SPrepareTx() -> int
//
// Prepare I2S for output, handle exclusive access if necessary
//
// Returns `I2S_OK` if ok to send to output or error code
int32_t I2SPrepareTx(void) {
  I2sStopPlaying();

  AddLog(LOG_LEVEL_DEBUG, "I2S: I2SPrepareTx out=%p", audio_i2s.out);
  if (!audio_i2s.out) { return I2S_ERR_OUTPUT_NOT_CONFIGURED; }
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
  if (!EnsureES8311Initialized()) {
    AddLog(LOG_LEVEL_ERROR, "I2S: ES8311 init not ready before playback");
  }
#endif

  if (!audio_i2s.out->beginTx()) { return I2S_ERR_TX_FAILED; }

  audio_i2s.out->SetGain(((float)(audio_i2s.Settings->tx.gain + 1)/ 100.0));
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
  S3boxAudioPower(1);
  AddLog(LOG_LEVEL_INFO, "I2S: amplifier GPIO46 -> ON before playback");
#endif

  return I2S_OK;
}

//
// I2SPrepareRx() -> int
//
// Prepare I2S for input, handle exclusive access if necessary
//
// Returns `I2S_OK` if ok to record input or error code
int32_t I2SPrepareRx(void) {
  if (!audio_i2s.in) return I2S_ERR_INPUT_NOT_CONFIGURED;
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
  if (!EnsureES8311Initialized()) {
    AddLog(LOG_LEVEL_ERROR, "I2S: ES8311 init not ready before microphone start");
  }
#endif

  if (audio_i2s.Settings->sys.exclusive) {
    // TODO - deconfigure input driver
  }
  return I2S_OK;
}

/*********************************************************************************************\
 * Driver features and commands
\*********************************************************************************************/

#if defined(USE_I2S_MP3) || defined(USE_I2S_WEBRADIO)
void I2sMp3Task(void *arg) {
  audio_i2s_mp3.task_running = true;
  audio_i2s_mp3.file_play_pausing = false;
  while (audio_i2s_mp3.decoder->isRunning() && audio_i2s_mp3.task_running) {
    if (!audio_i2s_mp3.decoder->loop()) {
      audio_i2s_mp3.task_running = false;
    }
    if (audio_i2s_mp3.file_play_pausing == true) {
      audio_i2s_mp3.paused_position = audio_i2s_mp3.file->getPos();
      audio_i2s_mp3.decoder->stop();
      audio_i2s_mp3.file_has_paused = true;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  audio_i2s.out->flush();
  audio_i2s_mp3.decoder->stop();
  audio_i2s_mp3.task_loop_mode = false;
  mp3_delete();
  audio_i2s_mp3.mp3_task_handle = nullptr;
  audio_i2s_mp3.task_has_ended = true;
  vTaskDelete(NULL);
}
#endif // defined(USE_I2S_MP3) || defined(USE_I2S_WEBRADIO)

void I2sStatusCallback(void *cbData, int code, const char *string) {
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) code;
  (void) ptr;
  AddLog(LOG_LEVEL_DEBUG, "I2S: -> %s", string);
}

#ifdef USE_I2S_MP3
void I2sMp3WrTask(void *arg){
  audio_i2s_mp3.task_running = true;
  audio_i2s_mp3.task_has_ended = false;
  while (audio_i2s_mp3.task_running) {
    if (audio_i2s_mp3.decoder && audio_i2s_mp3.decoder->isRunning()) {
      if (!audio_i2s_mp3.decoder->loop()) {
        audio_i2s_mp3.task_running = false;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  audio_i2s.out->flush();
  I2sWebRadioStopPlaying();
  audio_i2s_mp3.mp3_task_handle = nullptr;
  audio_i2s_mp3.task_has_ended = true;
  vTaskDelete(NULL);
}

#endif // USE_I2S_MP3

void I2sStopPlaying() {
  I2SAudioPower(false);

  if(audio_i2s_mp3.task_running){
    audio_i2s_mp3.task_running = false;
    while(audio_i2s_mp3.task_has_ended == false){
      delay(10);
    }
    while(audio_i2s_mp3.decoder){
      delay(10);
    }
  }
  if (audio_i2s_mp3.mic_task_handle) {
    audio_i2s_mp3.mic_stop = 1;
    while (audio_i2s_mp3.mic_stop) {
      delay(10);
    }
  }
}

#ifdef USE_I2S_MP3

bool I2SinitDecoder(uint32_t decoder_type){
  switch(decoder_type){
    case MP3_DECODER:
      if (audio_i2s_mp3.preallocateCodec) {
        audio_i2s_mp3.decoder = dynamic_cast<AudioGenerator *>(new AudioGeneratorMP3(audio_i2s_mp3.preallocateCodec, preallocateCodecSize));
      } else {
        audio_i2s_mp3.decoder = dynamic_cast<AudioGenerator *>(new AudioGeneratorMP3());
      }
      break;
#ifdef USE_I2S_AAC
    case AAC_DECODER:
      audio_i2s_mp3.preallocateCodec = special_realloc(audio_i2s_mp3.preallocateCodec, preallocateCodecSizeAAC);
      if(audio_i2s_mp3.preallocateCodec == nullptr){
        AddLog(LOG_LEVEL_ERROR, "I2S: could not alloc heap for AAC");
        return false;
      }
      audio_i2s_mp3.decoder = dynamic_cast<AudioGenerator *>(new AudioGeneratorAAC(audio_i2s_mp3.preallocateCodec, preallocateCodecSizeAAC));
      break;
#endif //USE_I2S_AAC
#ifdef USE_I2S_OPUS
    case OPUS_DECODER:
      free(audio_i2s_mp3.preallocateCodec);
      audio_i2s_mp3.preallocateCodec = nullptr;
      audio_i2s_mp3.decoder = dynamic_cast<AudioGenerator *>(new AudioGeneratorOpus());
      break;
#endif //USE_I2S_OPUS
    case WAV_DECODER:
      audio_i2s_mp3.decoder = dynamic_cast<AudioGenerator *>(new AudioGeneratorWAV());
      break;
  }
  if(audio_i2s_mp3.decoder == nullptr){
    return false;
  }
  return true;
}

// Play a audio file from filesystem
//
// Returns I2S_error_t
int32_t I2SPlayFile(const char *path, uint32_t decoder_type) {
  if (audio_i2s_mp3.decoder != nullptr) return I2S_ERR_DECODER_IN_USE;

  int32_t i2s_err = I2SPrepareTx();
  if ((i2s_err) != I2S_OK) { return i2s_err; }

  // check if the filename starts with '/', if not add it
  char fname[64];
  if (path[0] != '/') {
    snprintf(fname, sizeof(fname), "/%s", path);
  } else {
    snprintf(fname, sizeof(fname), "%s", path);
  }
  if (!ufsp->exists(fname)) { return I2S_ERR_FILE_NOT_FOUND; }

  strncpy(audio_i2s_mp3.audio_title, fname, sizeof(audio_i2s_mp3.audio_title));
  audio_i2s_mp3.audio_title[sizeof(audio_i2s_mp3.audio_title)-1] = 0;
  audio_i2s_mp3.current_file_type = decoder_type; //save for i2spause

  I2SAudioPower(true);

  if (audio_i2s_mp3.task_loop_mode == true){
    File _loopFile = ufsp->open(fname);
    size_t _fsize = _loopFile.size();
    audio_i2s_mp3.preallocateBuffer = special_realloc(audio_i2s_mp3.preallocateBuffer,_fsize);
    size_t _received = _loopFile.read(reinterpret_cast<uint8_t*>(audio_i2s_mp3.preallocateBuffer),_fsize);
    _loopFile.close();

    audio_i2s_mp3.file = new AudioFileSourceLoopBuffer (audio_i2s_mp3.preallocateBuffer, _fsize); // use the id3 var to make the code shorter down the line
  } else {
    audio_i2s_mp3.file = new AudioFileSourceFS(*ufsp, fname);
  }
  audio_i2s_mp3.id3 = new AudioFileSourceID3(audio_i2s_mp3.file);

  if(I2SinitDecoder(decoder_type)){
    audio_i2s_mp3.decoder->begin(audio_i2s_mp3.id3, audio_i2s.out);
    audio_i2s_mp3.file->seek(audio_i2s_mp3.paused_position, 1); // seek to the position where we paused or have it set to 0, 1 = SEEK_CUR
  } else {
    return I2S_ERR_DECODER_FAILED_TO_INIT;
  }

  size_t play_tasksize = 8000; // suitable for ACC and MP3
  if(decoder_type == OPUS_DECODER){ // opus needs a ton of stack
    play_tasksize = 26000;
  }

  // Always use a task
  xTaskCreatePinnedToCore(I2sMp3Task, "PLAYFILE", play_tasksize, NULL, 3, &audio_i2s_mp3.mp3_task_handle, 1);
  return I2S_OK;
}

void mp3_delete(void) {
  delete audio_i2s_mp3.buff;
  delete audio_i2s_mp3.file;
  delete audio_i2s_mp3.id3;
  delete audio_i2s_mp3.decoder;
  audio_i2s_mp3.decoder = nullptr;
}

void i2s_clean_pause_data(void) {
  audio_i2s_mp3.current_file_type = MP3_DECODER;
  audio_i2s_mp3.paused_position = 0;
  audio_i2s_mp3.file_play_pausing = false;
}

#endif // USE_I2S_MP3

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

void CmndI2SMic(void) {
  if (I2SPrepareRx() != I2S_OK) {
    ResponseCmndChar("I2S Mic not configured");
    return;
  }

  esp_err_t err = ESP_OK;
  audio_i2s.in->startRx();
  if (audio_i2s.in->getRxRunning()) {
    uint8_t buf[128];

    size_t bytes_read = 0;
    int32_t btr = audio_i2s.in->readMic(buf, sizeof(buf), true /*dc_block*/, false /*apply_gain*/, true /*lowpass*/, nullptr /*peak_ptr*/);
    if (btr < 0) {
      AddLog(LOG_LEVEL_INFO, "I2S: Mic (err:%i)", -btr);
      ResponseCmndChar("I2S Mic read error");
      return;
    }
    // esp_err_t err = i2s_channel_read(audio_i2s.in->getRxHandle(), buf, sizeof(buf), &bytes_read, 0);
    // if (err == ESP_ERR_TIMEOUT) { err = ESP_OK; }
    AddLog(LOG_LEVEL_INFO, "I2S: Mic (%i) %*_H", btr, btr, buf);
  }

  // err = xTaskCreatePinnedToCore(I2sMicTask, "MIC", stack, NULL, 3, &audio_i2s_mp3.mic_task_handle, 1);

  ResponseCmndDone();
}

void CmndI2SBeep(void) {
  uint32_t duration_ms = (XdrvMailbox.payload > 0) ? XdrvMailbox.payload : 1000;
  if (duration_ms > 10000) { duration_ms = 10000; }
#if defined(ESP32S3_RLCD_4_2)
  uint32_t tdm_variant = 0;
#if defined(RLCD_ENABLE_LEGACY_I2S_TEST)
  if (RlcdLegacyBeep(duration_ms, 1000)) {
    ResponseCmndDone();
  } else {
    ResponseCmndChar("Legacy beep failed");
  }
  return;
#endif
#endif

  if (I2SPrepareTx() != I2S_OK) {
    ResponseCmndChar("I2S output not configured");
    return;
  }

  const uint32_t sample_rate =
#if defined(ESP32S3_RLCD_4_2)
    24000;
#else
    16000;
#endif
  const uint32_t tone_hz = 1000;
#if defined(ESP32S3_RLCD_4_2)
  // esp_codec_dev sets TDM slot format before changing the clock.
  audio_i2s.out->SetBitsPerSample(16);
  audio_i2s.out->SetChannels(2);
  audio_i2s.out->SetTxRate(sample_rate);
#else
  audio_i2s.out->SetTxRate(sample_rate);
  audio_i2s.out->SetBitsPerSample(16);
  audio_i2s.out->SetChannels(2);
#endif
  audio_i2s.out->SetGain(((float)(audio_i2s.Settings->tx.gain + 1) / 100.0));
#if defined(ESP32S3_RLCD_4_2)
  if (audio_i2s.in) {
    uint32_t rx_err = audio_i2s.in->startRx();
    AddLog(LOG_LEVEL_INFO, "I2S: beep enabled paired RX during TX err=0x%04X", rx_err);
  }
#endif
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
  S3boxForcePlaybackCodec(sample_rate);
#endif
  I2SAudioPower(true);

#if defined(ESP32S3_RLCD_4_2)
  i2s_chan_handle_t tx_handle = audio_i2s.out->getTxHandle();
	  if (!tx_handle) {
	    I2SAudioPower(false);
	    ResponseCmndChar("No TX handle");
	    return;
	  }

  i2s_data_bit_width_t data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
  i2s_slot_bit_width_t slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
  uint32_t total_slot = (tdm_variant == 0) ? 2 : 4;
  i2s_tdm_slot_mask_t slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1);
  if (tdm_variant == 2) { slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT1 | I2S_TDM_SLOT2); }
  if (tdm_variant == 3) { slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT2 | I2S_TDM_SLOT3); }
  if (tdm_variant == 4) { slot_mask = (i2s_tdm_slot_mask_t)(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3); }
  i2s_tdm_slot_config_t slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(data_bit_width, I2S_SLOT_MODE_STEREO, slot_mask);
  slot_cfg.total_slot = total_slot;
  slot_cfg.slot_bit_width = slot_bit_width;
  esp_err_t slot_err = i2s_channel_disable(tx_handle);
  AddLog(LOG_LEVEL_INFO, "I2S: beep TDM variant=%u disable err=0x%04X", tdm_variant, slot_err);
  slot_err = i2s_channel_reconfig_tdm_slot(tx_handle, &slot_cfg);
  AddLog(LOG_LEVEL_INFO, "I2S: beep TDM data=16 slot=16 total=%u mask=0x%X err=0x%04X",
         total_slot, slot_mask, slot_err);
  i2s_chan_info_t chan_info = {};
  i2s_channel_get_info(tx_handle, &chan_info);
  AddLog(LOG_LEVEL_INFO, "I2S: beep TX channel mode=%d role=%d dir=%d pair=%p",
         chan_info.mode, chan_info.role, chan_info.dir, chan_info.pair_chan);
  slot_err = i2s_channel_enable(tx_handle);
  AddLog(LOG_LEVEL_INFO, "I2S: beep TDM enable err=0x%04X", slot_err);
  RlcdLogAudioPinActivity("after-enable", 2000);

	  uint32_t total_frames = (sample_rate * duration_ms) / 1000;
	  uint32_t frame_index = 0;
  uint32_t total_bytes_written = 0;
  uint32_t writes = 0;
	  uint32_t samples_per_half_period = sample_rate / (tone_hz * 2);
	  if (!samples_per_half_period) { samples_per_half_period = 1; }

		  AddLog(LOG_LEVEL_INFO, "I2S: packed TDM beep %u ms at %u Hz sample_rate=%u variant=%u", duration_ms, tone_hz, sample_rate, tdm_variant);
	  while (frame_index < total_frames) {
	    int16_t pcm[256];
	    uint32_t frames = total_frames - frame_index;
	    if (frames > 64) { frames = 64; }

	    for (uint32_t i = 0; i < frames; i++) {
	      int16_t value = (((frame_index + i) / samples_per_half_period) & 1) ? kRlcdBeepAmplitude : -kRlcdBeepAmplitude;
        for (uint32_t slot = 0; slot < total_slot; slot++) {
          pcm[(i * total_slot) + slot] = 0;
        }
        if (tdm_variant == 0 || tdm_variant == 1) {
          pcm[(i * total_slot)] = value;
          pcm[(i * total_slot) + 1] = value;
        } else if (tdm_variant == 2) {
          pcm[(i * total_slot) + 1] = value;
          pcm[(i * total_slot) + 2] = value;
        } else if (tdm_variant == 3) {
          pcm[(i * total_slot) + 2] = value;
          pcm[(i * total_slot) + 3] = value;
        } else {
          for (uint32_t slot = 0; slot < total_slot; slot++) {
            pcm[(i * total_slot) + slot] = value;
          }
        }
	    }

	    size_t bytes_written = 0;
	    esp_err_t err = i2s_channel_write(tx_handle, pcm, frames * total_slot * sizeof(int16_t), &bytes_written, 1000);
    total_bytes_written += bytes_written;
    writes++;
    if (writes <= 2) {
      AddLog(LOG_LEVEL_INFO, "I2S: beep write #%u requested=%u wrote=%u err=0x%04X",
             writes, frames * total_slot * sizeof(int16_t), bytes_written, err);
      if (writes == 2) {
        RlcdLogAudioPinActivity("during-write", 2000);
      }
    }
    if (err != ESP_OK) {
      AddLog(LOG_LEVEL_INFO, "I2S: factory-style beep write err=0x%04X bytes=%u", err, bytes_written);
      break;
    }
    frame_index += frames;
  }
  AddLog(LOG_LEVEL_INFO, "I2S: beep wrote total=%u bytes in %u writes", total_bytes_written, writes);

  delay(250);
  if (audio_i2s.in && audio_i2s.in->getRxRunning()) {
    audio_i2s.in->stopRx();
  }
  audio_i2s.out->stopTx();
  I2SAudioPower(false);
  ResponseCmndDone();
  return;
#endif

  uint32_t total_samples = (sample_rate * duration_ms) / 1000;
  uint32_t sample_index = 0;
  uint32_t fallback_samples_per_half_period = sample_rate / (tone_hz * 2);
  if (!fallback_samples_per_half_period) { fallback_samples_per_half_period = 1; }

  AddLog(LOG_LEVEL_INFO, "I2S: beep %u ms at %u Hz sample_rate=%u", duration_ms, tone_hz, sample_rate);
  while (sample_index < total_samples) {
    int16_t sample[2];
    int16_t value = ((sample_index / fallback_samples_per_half_period) & 1) ? kRlcdBeepAmplitude : -kRlcdBeepAmplitude;
    sample[0] = value;
    sample[1] = value;
    while (!audio_i2s.out->ConsumeSample(sample)) {
      delay(1);
    }
    sample_index++;
  }

  audio_i2s.out->flush();
  audio_i2s.out->stopTx();
  I2SAudioPower(false);
  ResponseCmndDone();
}

void CmndI2SCodec(void) {
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
  EnsureES8311Initialized();
  S3boxDumpCodec("cmd");
  ResponseCmndDone();
#else
  ResponseCmndChar("No board codec");
#endif
}


void CmndI2SStop(void) {
  if (I2SPrepareTx() != I2S_OK) {
    ResponseCmndChar("I2S output not configured");
    return;
  }
  audio_i2s.out->setGain(0);
  i2s_clean_pause_data();
  ResponseCmndDone();
}

#ifdef USE_I2S_MP3
void CmndI2SLoop(void) {
    audio_i2s_mp3.task_loop_mode = 1;
    CmndI2SPlay();
}

void CmndI2SPause(void) {
  if(audio_i2s_mp3.task_running 
#ifdef MP3_MIC_STREAM
    && !audio_i2s_mp3.stream_active
#endif //MP3_MIC_STREAM
#ifdef USE_I2S_WEBRADIO
    && Audio_webradio.ifile == nullptr
#endif // USE_I2S_WEBRADIO
  ){
    audio_i2s_mp3.file_play_pausing = true;
    ResponseCmndChar("Player Paused");
  } else {
    ResponseCmndChar("Player not running"); // or webradio is using decoder
  }
}

void CmndI2SPlay(void) {
  if (XdrvMailbox.data_len > 0) {
    i2s_clean_pause_data(); // clean up any previous pause data, set start to 0
    uint32_t decoder_type = XdrvMailbox.index;
    if (decoder_type <= MP3_DECODER) {
      const char *ext = strrchr(XdrvMailbox.data, '.');
      if (ext && !strcasecmp(ext, ".wav")) {
        decoder_type = WAV_DECODER;
      }
#ifdef USE_I2S_OPUS
      else if (ext && (!strcasecmp(ext, ".opus") || !strcasecmp(ext, ".webm"))) {
        decoder_type = OPUS_DECODER;
      }
#endif // USE_I2S_OPUS
#ifdef USE_I2S_AAC
      else if (ext && (!strcasecmp(ext, ".aac") || !strcasecmp(ext, ".m4a"))) {
        decoder_type = AAC_DECODER;
      }
#endif // USE_I2S_AAC
      else {
        decoder_type = MP3_DECODER;
      }
    }
    int32_t err = I2SPlayFile(XdrvMailbox.data, decoder_type);
    // display return message
    switch (err) {
      case I2S_OK:
        ResponseCmndChar("Started");
        break;
      case I2S_ERR_OUTPUT_NOT_CONFIGURED:
        ResponseCmndChar("I2S output not configured");
        break;
      case I2S_ERR_DECODER_IN_USE:
        ResponseCmndChar("Decoder already in use");
        break;
      case I2S_ERR_DECODER_FAILED_TO_INIT:
        ResponseCmndChar("Decoder failed to init");
        break;
      case I2S_ERR_FILE_NOT_FOUND:
        ResponseCmndChar("File not found");
        break;
      case I2S_ERR_TX_FAILED:
        ResponseCmndChar("Unable to open sound output");
        break;
      default:
        ResponseCmndChar("Unknown error");
        break;
    }
  } else {
    if(audio_i2s_mp3.file_play_pausing == true){
      int32_t err = I2SPlayFile((const char *)audio_i2s_mp3.audio_title, (uint32_t)audio_i2s_mp3.current_file_type); // the line above should rule out basically any error, but we'll see ...
      AddLog(LOG_LEVEL_DEBUG, "I2S: Resume: %s, type: %i at %i , err: %i", audio_i2s_mp3.audio_title, audio_i2s_mp3.current_file_type, audio_i2s_mp3.paused_position, err);
      ResponseCmndChar("Player resumed");
    } else {
      ResponseCmndChar("Missing filename");
    }
  }
}
#endif // USE_I2S_MP3

void CmndI2SGain(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 100)) {
    if (audio_i2s.out) {
      audio_i2s.Settings->tx.gain = XdrvMailbox.payload;
      audio_i2s.out->SetGain(((float)(audio_i2s.Settings->tx.gain+1)/100.0));
    }
  }
  ResponseCmndNumber(audio_i2s.Settings->tx.gain);
}

void CmndI2SSay(void) {
  if (XdrvMailbox.data_len > 0) {
    if (I2SPrepareTx() != I2S_OK) {
      ResponseCmndChar("I2S output not configured");
      return;
    } else {
      I2SAudioPower(true);
      ESP8266SAM sam;
      sam.Say(audio_i2s.out, XdrvMailbox.data);
      audio_i2s.out->stopTx();
      I2SAudioPower(false);
      // end of scope, ESP8266SAM is destroyed
    }
  }
  ResponseCmndChar(XdrvMailbox.data);
}

void CmndI2SI2SRtttl(void) {
  if (I2SPrepareTx() != I2S_OK) {
    ResponseCmndChar("I2S output not configured");
    return;
  }
  if (XdrvMailbox.data_len > 0) {
    Rtttl(XdrvMailbox.data);
  }
  ResponseCmndChar(XdrvMailbox.data);
}

void CmndI2SMicRec(void) {
  if (I2SPrepareRx() != I2S_OK) {
    ResponseCmndChar("I2S Mic not configured");
    return;
  }

  if (XdrvMailbox.data_len > 0) {
    if (!strncmp(XdrvMailbox.data, "-?", 2)) {
      Response_P("{\"I2SREC-duration\":%d}", audio_i2s_mp3.recdur);
    } else {
      char rec_path[sizeof(audio_i2s_mp3.mic_path)];
      const char *path = XdrvMailbox.data;
      audio_i2s_mp3.mic_duration_limit = 0;

      char *comma = strchr(XdrvMailbox.data, ',');
      if (comma) {
        char *endptr = nullptr;
        uint32_t seconds = strtoul(XdrvMailbox.data, &endptr, 10);
        if (endptr == comma) {
          audio_i2s_mp3.mic_duration_limit = seconds;
          path = comma + 1;
        }
      }
      while (*path == ' ') { path++; }
      strlcpy(rec_path, path, sizeof(rec_path));

      uint32_t encoder_type = XdrvMailbox.index;
      if (encoder_type <= MP3_ENCODER) {
        const char *ext = strrchr(rec_path, '.');
        if (ext && !strcasecmp(ext, ".wav")) {
          encoder_type = WAV_ENCODER;
        }
#ifdef USE_I2S_OPUS
        else if (ext && (!strcasecmp(ext, ".opus") || !strcasecmp(ext, ".webm"))) {
          encoder_type = OPUS_ENCODER;
        }
#endif // USE_I2S_OPUS
        else {
          encoder_type = MP3_ENCODER;
        }
      }

      audio_i2s_mp3.use_stream = false;
      int err = I2sRecord(rec_path, encoder_type);
      // int err = I2sRecordShine(XdrvMailbox.data);
      if(err == pdPASS){
        ResponseCmndChar(rec_path);
      } else {
        ResponseCmndChar_P(PSTR("Did not launch recording task"));
      }
    }
  } else {
    if (audio_i2s_mp3.mic_task_handle) {
      // stop task
      audio_i2s_mp3.mic_stop = 1;
      while (audio_i2s_mp3.mic_stop) {
        delay(1);
      }
      ResponseCmndChar_P(PSTR("Stopped"));
    }
    else {
      ResponseCmndChar_P(PSTR("No running recording"));
    }
  }
}

void I2sEventHandler(){
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
  S3boxCodecPeriodic();
#endif
  if(audio_i2s_mp3.file_has_paused == true){
    audio_i2s_mp3.task_has_ended = false; //do not send ended event
    audio_i2s_mp3.file_has_paused = false;
    audio_i2s_mp3.task_running = false;
    MqttPublishPayloadPrefixTopicRulesProcess_P(RESULT_OR_STAT,PSTR(""),PSTR("{\"Event\":{\"I2SPlay\":\"Paused\"}}"));
    // Rule1 ON event#i2splay=paused DO <something> ENDON
    I2SAudioPower(false);
  }
  if(audio_i2s_mp3.task_has_ended == true){
    audio_i2s_mp3.task_has_ended = false;
      audio_i2s_mp3.task_running = false;
    MqttPublishPayloadPrefixTopicRulesProcess_P(RESULT_OR_STAT,PSTR(""),PSTR("{\"Event\":{\"I2SPlay\":\"Ended\"}}"));
    // Rule1 ON event#i2splay=ended DO <something> ENDON
    I2SAudioPower(false);
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

void I2sStreamLoop(void);

bool Xdrv42(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      I2sInit();
      break;
    case FUNC_EVERY_50_MSECOND:
      I2sEventHandler();
      break;
    case FUNC_COMMAND:
      result = DecodeCommand(kI2SAudio_Commands, I2SAudio_Command);
      break;
    case FUNC_WEB_ADD_MAIN_BUTTON:
      //MP3ShowStream();
      break;
    case FUNC_LOOP:
#if defined(USE_SHINE) && defined(MP3_MIC_STREAM)
      I2sStreamLoop();
#endif
#if defined(I2S_BRIDGE)
      i2s_bridge_loop();
#endif
      break;

#ifdef USE_WEBSERVER
    case FUNC_WEB_SENSOR:
      I2sWrShow(false);
      break;
#endif  // USE_WEBSERVER
    case FUNC_JSON_APPEND:
      I2sWrShow(true);
    break;
  }
  return result;
}

#endif // USE_I2S_AUDIO
#endif // defined(ESP32) && ESP_IDF_VERSION_MAJOR >= 5
