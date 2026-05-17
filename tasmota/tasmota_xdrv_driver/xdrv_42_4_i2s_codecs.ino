
/*
  audio is2 support for ESP32-S3 box and box lite

  Copyright (C) 2022  Gerhard Mutz and Theo Arends

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
#if defined(ESP32S3_BOX) || defined(ESP32S3_RLCD_4_2)
#include <Wire.h>
#include <es8156.h>
#include <es8311.h>
#include <es7243e.h>
#include <es7210.h>


#define S3BOX_APWR_GPIO 46
#define RLCD_I2C_BUS    0  // Tasmota bus index 0 == "bus1" in console output

static bool g_s3box_codec_ready = false;
static bool g_s3box_amp_on = false;
static bool g_es8311_present = false;
static bool g_es8311_init_done = false;
static bool g_es8311_init_pending = false;
static bool g_es7210_init_done = false;
static uint32_t g_es8311_last_init_attempt = 0;
static uint8_t g_s3box_tx_sample = AUDIO_HAL_16K_SAMPLES;
static uint8_t g_s3box_rx_sample = AUDIO_HAL_16K_SAMPLES;

static bool S3boxSampleFromHz(uint32_t hz, uint8_t &sample) {
  switch (hz) {
    case 8000: sample = AUDIO_HAL_08K_SAMPLES; return true;
    case 11025: sample = AUDIO_HAL_11K_SAMPLES; return true;
    case 16000: sample = AUDIO_HAL_16K_SAMPLES; return true;
    case 22050: sample = AUDIO_HAL_22K_SAMPLES; return true;
    case 24000: sample = AUDIO_HAL_24K_SAMPLES; return true;
    case 32000: sample = AUDIO_HAL_32K_SAMPLES; return true;
    case 44100: sample = AUDIO_HAL_44K_SAMPLES; return true;
    case 48000: sample = AUDIO_HAL_48K_SAMPLES; return true;
    default: return false;
  }
}

static TwoWire* S3boxGetWire(void) {
  TwoWire& myWire = I2cGetWire(RLCD_I2C_BUS);
  if (&myWire == nullptr) { return nullptr; }
  return &myWire;
}

static bool ES8311_WriteReg(uint8_t reg, uint8_t value) {
  TwoWire *wire = S3boxGetWire();
  if (!wire) { return false; }
  wire->beginTransmission(ES8311_ADDR);
  wire->write(reg);
  wire->write(value);
  return (0 == wire->endTransmission());
}

static int ES8311_ReadReg(uint8_t reg) {
  TwoWire *wire = S3boxGetWire();
  if (!wire) { return -1; }
  wire->beginTransmission(ES8311_ADDR);
  wire->write(reg);
  if (0 != wire->endTransmission(false)) { return -1; }
  if (1 != wire->requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1)) { return -1; }
  return wire->read();
}

static uint32_t ES8311_ApplyPostInit(void) {
  uint32_t ret_val = ESP_OK;
  // Keep explicit post-init writes so power, ADC/DAC paths, and unmute are deterministic.
  ret_val |= ES8311_WriteReg(ES8311_RESET_REG00, 0x3F) ? ESP_OK : ESP_FAIL;   // release reset, clocks running
  ret_val |= ES8311_WriteReg(ES8311_SYSTEM_REG0D, 0x01) ? ESP_OK : ESP_FAIL;  // analog power up
  ret_val |= ES8311_WriteReg(ES8311_SYSTEM_REG0E, 0x02) ? ESP_OK : ESP_FAIL;  // bias/reference power
  ret_val |= ES8311_WriteReg(ES8311_SYSTEM_REG12, 0x00) ? ESP_OK : ESP_FAIL;  // DAC on
  ret_val |= ES8311_WriteReg(ES8311_SYSTEM_REG14, 0x1A) ? ESP_OK : ESP_FAIL;  // ADC path + PGA baseline
  ret_val |= ES8311_WriteReg(ES8311_ADC_REG15, 0x40) ? ESP_OK : ESP_FAIL;     // ADC enable
  ret_val |= ES8311_WriteReg(ES8311_ADC_REG17, 0xBF) ? ESP_OK : ESP_FAIL;     // ADC digital volume
  ret_val |= ES8311_WriteReg(ES8311_DAC_REG37, 0x48) ? ESP_OK : ESP_FAIL;     // DAC ramp
  int dac31 = ES8311_ReadReg(ES8311_DAC_REG31);
  if (dac31 >= 0) {
    ret_val |= ES8311_WriteReg(ES8311_DAC_REG31, dac31 & ~(0x60)) ? ESP_OK : ESP_FAIL; // clear DAC mute bits
  } else {
    ret_val |= ESP_FAIL;
  }
  ret_val |= ES8311_WriteReg(ES8311_DAC_REG32, 0xC0) ? ESP_OK : ESP_FAIL;     // DAC digital volume
  return ret_val;
}

bool S3boxCodecReady(void) {
  return g_s3box_codec_ready;
}

void S3boxAudioPower(uint8_t pwr) {
  g_s3box_amp_on = (pwr != 0);
  digitalWrite(S3BOX_APWR_GPIO, pwr);
  AddLog(LOG_LEVEL_INFO, "I2S: amplifier GPIO%d -> %s", S3BOX_APWR_GPIO, g_s3box_amp_on ? "ON" : "OFF");
}

void S3boxSetTxSampleRate(uint32_t hz) {
  if (!g_s3box_codec_ready) { return; }
  uint8_t sample;
  if (!S3boxSampleFromHz(hz, sample)) {
    AddLog(LOG_LEVEL_DEBUG, "I2S: ES8311 skip unsupported TX sample rate %u", hz);
    return;
  }
  if (sample == g_s3box_tx_sample) { return; }

  audio_hal_codec_i2s_iface_t iface = {
    .mode = AUDIO_HAL_MODE_SLAVE,
    .fmt = AUDIO_HAL_I2S_NORMAL,
    .samples = (audio_hal_iface_samples_t)sample,
    .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
  };
  uint32_t ret_val = ESP_OK;
  ret_val |= es8311_codec_config_i2s(AUDIO_HAL_CODEC_MODE_BOTH, &iface);
  ret_val |= es8311_set_bits_per_sample(iface.bits);
  ret_val |= es8311_config_fmt((es_i2s_fmt_t)iface.fmt);
  if (ret_val == ESP_OK) {
    g_s3box_tx_sample = sample;
    AddLog(LOG_LEVEL_INFO, "I2S: ES8311 TX sample rate configured to %u Hz", hz);
  } else {
    AddLog(LOG_LEVEL_ERROR, "I2S: ES8311 TX sample rate config failed (0x%08X)", ret_val);
  }
}

void S3boxSetRxSampleRate(uint32_t hz) {
  if (!g_s3box_codec_ready) { return; }
  uint8_t sample;
  if (!S3boxSampleFromHz(hz, sample)) {
    AddLog(LOG_LEVEL_DEBUG, "I2S: ES7210 skip unsupported RX sample rate %u", hz);
    return;
  }
  if (sample == g_s3box_rx_sample) { return; }

  audio_hal_codec_config_t cfg = {
    .adc_input = AUDIO_HAL_ADC_INPUT_ALL,
    .codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE,
    .i2s_iface = {
      .mode = AUDIO_HAL_MODE_SLAVE,
      .fmt = AUDIO_HAL_I2S_NORMAL,
      .samples = (audio_hal_iface_samples_t)sample,
      .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
    },
  };
  uint32_t ret_val = es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface);
  if (ret_val == ESP_OK) {
    g_s3box_rx_sample = sample;
    AddLog(LOG_LEVEL_INFO, "I2S: ES7210 RX sample rate configured to %u Hz", hz);
  } else {
    AddLog(LOG_LEVEL_ERROR, "I2S: ES7210 RX sample rate config failed (0x%08X)", ret_val);
  }
}

// box lite dac init
uint32_t ES8156_init() {
  uint32_t ret_val = ESP_OK;
  TwoWire *wire = S3boxGetWire();
  if (!wire) { return ESP_FAIL; }

  if (I2cSetDevice(ES8156_ADDR, RLCD_I2C_BUS)) {
    I2cSetActiveFound(ES8156_ADDR, "ES8156-I2C", RLCD_I2C_BUS);
    audio_hal_codec_config_t cfg = {
       .i2s_iface = {
         .mode = AUDIO_HAL_MODE_SLAVE,
         .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
       }
    };
    ret_val |= es8156_codec_init(wire, &cfg);
    ret_val |= es8156_codec_set_voice_volume(75);
  }
  return ret_val;
}

// box lite adc init
uint32_t es7243e_init() {
    uint32_t ret_val = ESP_OK;
    TwoWire *wire = S3boxGetWire();
    if (!wire) { return ESP_FAIL; }

    if (I2cSetDevice(ES7243_ADDR, RLCD_I2C_BUS)) {
      I2cSetActiveFound(ES7243_ADDR, "ES7243e-I2C", RLCD_I2C_BUS);

      audio_hal_codec_config_t cfg = {
        .i2s_iface = {
          .mode = AUDIO_HAL_MODE_SLAVE,
          .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
        }
      };

      ret_val |= es7243e_adc_init(wire, &cfg);
    }

    return ret_val;
}
// box adc init
uint32_t es7210_init() {
  uint32_t ret_val = ESP_OK;
  TwoWire *wire = S3boxGetWire();
  if (!wire) { return ESP_FAIL; }

  if (I2cSetDevice(ES7210_ADDR, RLCD_I2C_BUS)) {
    I2cSetActiveFound(ES7210_ADDR, "ES7210-I2C", RLCD_I2C_BUS);
    audio_hal_codec_config_t cfg = {
        .adc_input = AUDIO_HAL_ADC_INPUT_ALL,
        .codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE,
        .i2s_iface = {
          .mode = AUDIO_HAL_MODE_SLAVE,
          .fmt = AUDIO_HAL_I2S_NORMAL,
          .samples = AUDIO_HAL_16K_SAMPLES,
          .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
        },
    };

    ret_val |= es7210_adc_init(wire, &cfg);
    ret_val |= es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface);
    ret_val |= es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2), (es7210_gain_value_t) GAIN_37_5DB);
    ret_val |= es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4), (es7210_gain_value_t) GAIN_0DB);
    ret_val |= es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START);
  }
  return ret_val;
}

// box dac init
uint32_t ES8311_init() {
  uint32_t ret_val = ESP_FAIL;

  TwoWire *wire = S3boxGetWire();
  if (!wire) {
    AddLog(LOG_LEVEL_ERROR, "I2S: ES8311 wire bus not ready");
    return ret_val;
  }
  if (!I2cSetDevice(ES8311_ADDR, RLCD_I2C_BUS)) {
    g_es8311_present = false;
    AddLog(LOG_LEVEL_ERROR, "I2S: ES8311 not detected at 0x%02X", ES8311_ADDR);
    return ret_val;
  }
  g_es8311_present = true;
  I2cSetActiveFound(ES8311_ADDR, "ES8311-I2C", RLCD_I2C_BUS);
  audio_hal_codec_config_t cfg = {
      .dac_output = AUDIO_HAL_DAC_OUTPUT_LINE1,
      .codec_mode = AUDIO_HAL_CODEC_MODE_DECODE,
      .i2s_iface = {
        .mode = AUDIO_HAL_MODE_SLAVE,
        .fmt = AUDIO_HAL_I2S_NORMAL,
        .samples = AUDIO_HAL_16K_SAMPLES,
        .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
      },
  };

  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 detected at 0x%02X", ES8311_ADDR);
  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 init start (slave, Philips, 16-bit)");
  ret_val = ESP_OK;
  ret_val |= es8311_codec_init(wire, &cfg);
  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 reset done");
  ret_val |= es8311_codec_config_i2s(AUDIO_HAL_CODEC_MODE_BOTH, &cfg.i2s_iface);
  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 clocks/I2S format configured");
  ret_val |= es8311_set_bits_per_sample(cfg.i2s_iface.bits);
  ret_val |= es8311_config_fmt((es_i2s_fmt_t)cfg.i2s_iface.fmt);
  ret_val |= es8311_set_voice_mute(false);
  ret_val |= es8311_codec_set_voice_volume(75);
  ret_val |= es8311_set_mic_gain(ES8311_MIC_GAIN_24DB);
  ret_val |= es8311_codec_ctrl_state(AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
  ret_val |= ES8311_ApplyPostInit();

  int mute = 1;
  es8311_get_voice_mute(&mute);
  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 DAC enabled");
  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 ADC enabled");
  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 output unmuted (mute=%d)", mute);
  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 volume set");
  AddLog(LOG_LEVEL_INFO, "I2S: ES8311 registers SYS0D=0x%02X SYS0E=0x%02X SYS12=0x%02X DAC31=0x%02X DAC32=0x%02X",
    ES8311_ReadReg(ES8311_SYSTEM_REG0D), ES8311_ReadReg(ES8311_SYSTEM_REG0E),
    ES8311_ReadReg(ES8311_SYSTEM_REG12), ES8311_ReadReg(ES8311_DAC_REG31), ES8311_ReadReg(ES8311_DAC_REG32));

  if (ret_val != ESP_OK) {
    AddLog(LOG_LEVEL_ERROR, "I2S: ES8311 init failed (0x%08X)", ret_val);
  } else {
    AddLog(LOG_LEVEL_INFO, "I2S: ES8311 init complete");
  }
  return ret_val;
}

bool EnsureES8311Initialized(void) {
  if (g_es8311_init_done && g_es7210_init_done) {
    return true;
  }

  uint32_t now = millis();
  if ((now - g_es8311_last_init_attempt) < 1000) {
    return false;
  }
  g_es8311_last_init_attempt = now;

  if (!TasmotaGlobal.i2c_enabled[RLCD_I2C_BUS]) {
    g_es8311_init_pending = true;
    AddLog(LOG_LEVEL_INFO, "I2S: ES8311 init deferred, I2C bus1 not ready");
    return false;
  }

  AddLog(LOG_LEVEL_INFO, "I2S: initializing ES8311 codec on I2C bus1 addr 0x%02X", ES8311_ADDR);

  ES8156_init();
  es7243e_init();
  uint32_t es8311_ret = ES8311_init();
  uint32_t es7210_ret = es7210_init();

  g_es8311_init_done = (es8311_ret == ESP_OK);
  g_es7210_init_done = (es7210_ret == ESP_OK);
  g_s3box_codec_ready = g_es8311_init_done && g_es7210_init_done;
  g_es8311_init_pending = !g_s3box_codec_ready;

  AddLog(LOG_LEVEL_INFO, "I2S: codec init summary ES8311=0x%08X ES7210=0x%08X ready=%d", es8311_ret, es7210_ret, g_s3box_codec_ready);

  return g_s3box_codec_ready;
}

void S3boxCodecPeriodic(void) {
  if (!g_es8311_init_pending) { return; }
  if ((millis() - g_es8311_last_init_attempt) < 2000) { return; }
  EnsureES8311Initialized();
}

void S3boxInit(void) {
  pinMode(S3BOX_APWR_GPIO, OUTPUT);
  S3boxAudioPower(0);

  g_es8311_init_pending = true;
  g_es8311_init_done = false;
  g_es7210_init_done = false;
  g_s3box_codec_ready = false;
  EnsureES8311Initialized();
}
#endif // ESP32S3_BOX || ESP32S3_RLCD_4_2


#ifdef USE_W8960

#include <wm8960.h>

void W8960_Init1(void) {
  TwoWire *wire = S3boxGetWire();
  if (wire && TasmotaGlobal.i2c_enabled[RLCD_I2C_BUS]) {
    if (I2cSetDevice(W8960_ADDR, RLCD_I2C_BUS)) {
      I2cSetActiveFound(W8960_ADDR, "W8960-I2C", RLCD_I2C_BUS);
      W8960_Init(wire);
    }
  }
}
#endif // USE_W8960

#endif // ESP32
