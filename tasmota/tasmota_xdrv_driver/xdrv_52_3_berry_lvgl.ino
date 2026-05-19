/*
  xdrv_52_3_berry_native.ino - Berry scripting language, native fucnctions

  Copyright (C) 2021 Stephan Hadinger, Berry language by Guan Wenliang https://github.com/Skiars/berry

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


#ifdef USE_BERRY
#ifdef USE_LVGL

#include <berry.h>
#include <limits.h>

// silence warning with Core3
#pragma GCC diagnostic push
#if defined(__GNUC__) && (__GNUC__ >= 10)
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#endif
#include "lvgl.h"
#ifdef USE_LVGL_PNG_DECODER
  #ifndef LODEPNG_NO_COMPILE_CPP
    #define LODEPNG_NO_COMPILE_CPP
  #endif
  #include "src/libs/lodepng/lodepng.h"
#endif
#pragma GCC diagnostic pop
#include "be_mapping.h"
#include "be_ctypes.h"
#include "lv_berry.h"
#ifdef USE_LVGL_HASPMOTA
  #include "be_lv_haspmota.h"
#endif // USE_LVGL_HASPMOTA

// Berry easy logging
extern "C" {
  extern void berry_log_C(const char * berry_buf, ...);
  extern const be_ntv_class_def_t lv_classes[];
  extern const size_t lv_classes_size;
}

// Forward declaration of helper functions
extern bool lvgl_started(void);
extern void lvgl_set_screenshot_file(File * file);
extern void lvgl_reset_screenshot_file(void);
File * lvgl_get_screenshot_file(void);
extern void lv_set_paint_cb(void* cb);
extern void* lv_get_paint_cb(void);
extern void lv_set_stream_cb(void* cb);

/********************************************************************
 * Structures used by LVGL_Berry
 *******************************************************************/

class LVBE_button {
public:
  bool pressed = false;       // what is the current state
  bool inverted = false;      // false: button pressed is HIGH, true: button pressed is LOW
  int8_t pin = -1;            // physical GPIO (-1 if unconfigured)

  uint32_t millis_last_state_change = 0; // last millis() time stamp when the state changed, used for debouncing
  const uint32_t debounce_time = 10;     // Needs to stabilize for 10ms before state change

  inline void set_inverted(bool inv) { inverted = inv; }
  inline bool get_inverted(void) const { return inverted; }

  inline bool valid(void) const { return pin >= 0; }

  bool read_gpio(void) const {
    bool cur_state = digitalRead(pin);
    if (inverted) { cur_state = !cur_state; }
    return cur_state;
  }

  void set_gpio(int8_t _pin) {      // is the button pressed
    pin = _pin;
    pressed = read_gpio();
    millis_last_state_change = millis();
  }

  bool state_changed(void) {        // do we need to report a change
    if (!valid()) { return false; }
    if (TimeReached(millis_last_state_change + debounce_time)) {
      // read current state of GPIO after debounce
      if (read_gpio() != pressed) {
        return true;
      }
    }
    return false;
  }

  bool clear_state_changed(void) {  // read and clear the state
    pressed = read_gpio();
    millis_last_state_change = millis();
    return pressed;
  }
};

class LVBE_globals {
public:
  lv_indev_t * indev;               // TODO still needed? or `indev_list` is enough?
  LList<lv_indev_t*> indev_list;
  // input devices
  LVBE_button btn[3];
};
LVBE_globals lvbe;

extern void start_lvgl(const char * uconfig);
extern void lv_ex_get_started_1(void);

extern "C" {


}

#ifdef USE_LVGL_PNG_DECODER
extern "C" void lodepng_free(void * ptr);
#endif // USE_LVGL_PNG_DECODER

/*********************************************************************************************\
 * TRMNL 1-bit PNG display helper
\*********************************************************************************************/
struct trmnl_png_i1_t {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint8_t * rows = nullptr;
  bool rows_from_lodepng = false;
};

static lv_obj_t * lv_trmnl_screen = nullptr;
static lv_draw_buf_t * lv_trmnl_draw_buf = nullptr;

static void trmnl_free_png(void * png_ptr);
static bool trmnl_source_bit(const void * src_ptr, uint32_t x, uint32_t y);
static void trmnl_map_pixel(uint32_t dx, uint32_t dy, uint32_t dst_w, uint32_t dst_h, uint32_t src_w, uint32_t src_h, int32_t rotation, uint32_t * sx, uint32_t * sy);

static uint32_t trmnl_read_be32(const uint8_t * p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void trmnl_set_error(char * err, size_t err_len, const char * msg) {
  if (err && err_len) {
    snprintf(err, err_len, "%s", msg);
  }
}

static bool trmnl_is_pbm_ws(uint8_t ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static void trmnl_release_lvgl(void) {
  if (lv_trmnl_screen) {
    lv_obj_delete(lv_trmnl_screen);
    lv_trmnl_screen = nullptr;
  }
  if (lv_trmnl_draw_buf) {
    lv_draw_buf_destroy(lv_trmnl_draw_buf);
    lv_trmnl_draw_buf = nullptr;
  }
}

static void trmnl_free_png(void * png_ptr) {
  trmnl_png_i1_t * png = (trmnl_png_i1_t*)png_ptr;
  if (png && png->rows) {
    if (png->rows_from_lodepng) {
#ifdef USE_LVGL_PNG_DECODER
      lodepng_free(png->rows);
#else
      free(png->rows);
#endif
    } else {
      free(png->rows);
    }
    png->rows = nullptr;
  }
}

static const uint8_t * trmnl_skip_pbm_ws(const uint8_t * p, const uint8_t * end) {
  while (p < end) {
    if (*p == '#') {
      while (p < end && *p != '\n' && *p != '\r') { p++; }
    } else if (trmnl_is_pbm_ws(*p)) {
      p++;
    } else {
      break;
    }
  }
  return p;
}

static bool trmnl_read_pbm_uint(const uint8_t ** cursor, const uint8_t * end, uint32_t * value) {
  const uint8_t * p = trmnl_skip_pbm_ws(*cursor, end);
  if (p >= end || *p < '0' || *p > '9') { return false; }
  uint32_t v = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    uint32_t digit = *p - '0';
    if (v > ((UINT32_MAX - digit) / 10)) { return false; }
    v = (v * 10) + digit;
    p++;
  }
  *cursor = p;
  *value = v;
  return true;
}

static bool trmnl_load_pbm_p4(const uint8_t * data, size_t data_size, void * out_ptr, char * err, size_t err_len) {
  trmnl_png_i1_t * out = (trmnl_png_i1_t*)out_ptr;
  if (data_size < 4 || data[0] != 'P' || data[1] != '4') {
    trmnl_set_error(err, err_len, "not a binary PBM");
    return false;
  }

  const uint8_t * cursor = data + 2;
  const uint8_t * end = data + data_size;
  uint32_t width = 0;
  uint32_t height = 0;
  if (!trmnl_read_pbm_uint(&cursor, end, &width) || !trmnl_read_pbm_uint(&cursor, end, &height)) {
    trmnl_set_error(err, err_len, "bad PBM header");
    return false;
  }
  if (cursor >= end || !trmnl_is_pbm_ws(*cursor)) {
    trmnl_set_error(err, err_len, "bad PBM raster separator");
    return false;
  }
  cursor++;
  if (width == 0 || height == 0 || width > 2048 || height > 2048) {
    trmnl_set_error(err, err_len, "unsupported PBM dimensions");
    return false;
  }

  uint32_t stride = (width + 7) / 8;
  size_t rows_size = (size_t)stride * height;
  if ((size_t)(end - cursor) < rows_size) {
    trmnl_set_error(err, err_len, "short PBM raster");
    return false;
  }

  uint8_t * rows = (uint8_t*)special_malloc(rows_size + 4);
  if (!rows) {
    trmnl_set_error(err, err_len, "cannot allocate PBM rows");
    return false;
  }
  memcpy(rows, cursor, rows_size);
  out->width = width;
  out->height = height;
  out->stride = stride;
  out->rows = rows;
  out->rows_from_lodepng = false;
  return true;
}

static bool trmnl_load_pbm_p4_file(File &f, size_t file_size, void * out_ptr, char * err, size_t err_len) {
  uint8_t header[256];
  size_t header_size = file_size < sizeof(header) ? file_size : sizeof(header);
  if (!f.seek(0) || f.read(header, header_size) != header_size) {
    trmnl_set_error(err, err_len, "cannot read PBM header");
    return false;
  }
  if (header_size < 4 || header[0] != 'P' || header[1] != '4') {
    trmnl_set_error(err, err_len, "not a binary PBM");
    return false;
  }

  trmnl_png_i1_t * out = (trmnl_png_i1_t*)out_ptr;
  const uint8_t * cursor = header + 2;
  const uint8_t * end = header + header_size;
  uint32_t width = 0;
  uint32_t height = 0;
  if (!trmnl_read_pbm_uint(&cursor, end, &width) || !trmnl_read_pbm_uint(&cursor, end, &height)) {
    trmnl_set_error(err, err_len, "bad PBM header");
    return false;
  }
  if (cursor >= end || !trmnl_is_pbm_ws(*cursor)) {
    trmnl_set_error(err, err_len, "bad PBM raster separator");
    return false;
  }
  cursor++;
  if (width == 0 || height == 0 || width > 2048 || height > 2048) {
    trmnl_set_error(err, err_len, "unsupported PBM dimensions");
    return false;
  }

  uint32_t stride = (width + 7) / 8;
  size_t rows_size = (size_t)stride * height;
  size_t raster_offset = cursor - header;
  if (file_size < (raster_offset + rows_size)) {
    trmnl_set_error(err, err_len, "short PBM raster");
    return false;
  }

  uint8_t * rows = (uint8_t*)special_malloc(rows_size + 4);
  if (!rows) {
    trmnl_set_error(err, err_len, "cannot allocate PBM rows");
    return false;
  }
  if (!f.seek(raster_offset) || f.read(rows, rows_size) != rows_size) {
    free(rows);
    trmnl_set_error(err, err_len, "cannot read PBM raster");
    return false;
  }

  out->width = width;
  out->height = height;
  out->stride = stride;
  out->rows = rows;
  out->rows_from_lodepng = false;
  return true;
}

static bool trmnl_append_idat(uint8_t ** idat, size_t * idat_size, const uint8_t * chunk, size_t chunk_size) {
  if (!idat || !idat_size || !chunk) { return false; }
  if (chunk_size > (SIZE_MAX - *idat_size)) { return false; }

  size_t new_size = *idat_size + chunk_size;
  uint8_t * next = (uint8_t*)realloc(*idat, new_size);
  if (!next) { return false; }
  memcpy(next + *idat_size, chunk, chunk_size);
  *idat = next;
  *idat_size = new_size;
  return true;
}

static bool trmnl_unfilter_png_rows(uint8_t * scanlines, size_t scanlines_size, uint32_t width, uint32_t height, uint32_t row_bytes, char * err, size_t err_len) {
  const uint32_t filtered_row_bytes = row_bytes + 1;
  if (scanlines_size != ((size_t)filtered_row_bytes * height)) {
    trmnl_set_error(err, err_len, "unexpected decompressed size");
    return false;
  }

  for (uint32_t y = 0; y < height; y++) {
    uint8_t * filtered = scanlines + ((size_t)y * filtered_row_bytes);
    uint8_t filter = filtered[0];
    uint8_t * raw = filtered + 1;
    uint8_t * row = scanlines + ((size_t)y * row_bytes);
    const uint8_t * prev = (y == 0) ? nullptr : (scanlines + ((size_t)(y - 1) * row_bytes));

    for (uint32_t x = 0; x < row_bytes; x++) {
      uint8_t left = (x == 0) ? 0 : row[x - 1];
      uint8_t up = prev ? prev[x] : 0;
      uint8_t up_left = (prev && x > 0) ? prev[x - 1] : 0;
      uint8_t val = raw[x];

      switch (filter) {
        case 0: // None
          break;
        case 1: // Sub
          val = (uint8_t)(val + left);
          break;
        case 2: // Up
          val = (uint8_t)(val + up);
          break;
        case 3: // Average
          val = (uint8_t)(val + ((uint16_t)left + up) / 2);
          break;
        case 4: { // Paeth
          int32_t p = (int32_t)left + up - up_left;
          int32_t pa = abs(p - left);
          int32_t pb = abs(p - up);
          int32_t pc = abs(p - up_left);
          uint8_t predictor = (pa <= pb && pa <= pc) ? left : ((pb <= pc) ? up : up_left);
          val = (uint8_t)(val + predictor);
          break;
        }
        default:
          trmnl_set_error(err, err_len, "unsupported PNG row filter");
          return false;
      }
      row[x] = val;
    }
  }

  return true;
}

static bool trmnl_load_png_1bit(const char * path, void * out_ptr, char * err, size_t err_len) {
  trmnl_png_i1_t * out = (trmnl_png_i1_t*)out_ptr;
  if (!path || !out) {
    trmnl_set_error(err, err_len, "missing path");
    return false;
  }

  memset(out, 0, sizeof(*out));

  File f = dfsp->open(path, "r");
  if (!f) {
    trmnl_set_error(err, err_len, "cannot open PNG");
    return false;
  }

  size_t png_size = f.size();
  if (png_size < 33 || png_size > 262144) {
    f.close();
    trmnl_set_error(err, err_len, "PNG file size is out of range");
    return false;
  }

  uint8_t magic[2];
  if (f.read(magic, sizeof(magic)) != sizeof(magic)) {
    f.close();
    trmnl_set_error(err, err_len, "short image read");
    return false;
  }
  if (magic[0] == 'P' && magic[1] == '4') {
    bool ok = trmnl_load_pbm_p4_file(f, png_size, out, err, err_len);
    f.close();
    return ok;
  }

  uint8_t * png = (uint8_t*)malloc(png_size);
  if (!png) {
    f.close();
    trmnl_set_error(err, err_len, "cannot allocate PNG buffer");
    return false;
  }

  if (!f.seek(0)) {
    free(png);
    f.close();
    trmnl_set_error(err, err_len, "cannot rewind image");
    return false;
  }
  size_t got = f.read(png, png_size);
  f.close();
  if (got != png_size) {
    free(png);
    trmnl_set_error(err, err_len, "short PNG read");
    return false;
  }

  if (png_size >= 2 && png[0] == 'P' && png[1] == '4') {
    bool ok = trmnl_load_pbm_p4(png, png_size, out, err, err_len);
    free(png);
    return ok;
  }

#ifndef USE_LVGL_PNG_DECODER
  free(png);
  trmnl_set_error(err, err_len, "LVGL PNG decoder is not enabled");
  return false;
#else
  static const uint8_t png_signature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
  if (memcmp(png, png_signature, sizeof(png_signature)) != 0) {
    free(png);
    trmnl_set_error(err, err_len, "not a PNG");
    return false;
  }

  uint8_t * idat = nullptr;
  size_t idat_size = 0;
  bool seen_ihdr = false;
  bool seen_iend = false;
  uint8_t bit_depth = 0;
  uint8_t color_type = 0;
  uint8_t interlace = 0;

  size_t pos = sizeof(png_signature);
  while ((pos + 12) <= png_size) {
    uint32_t len = trmnl_read_be32(png + pos);
    if ((size_t)len > png_size || (pos + 12 + len) > png_size) {
      free(idat);
      free(png);
      trmnl_set_error(err, err_len, "bad PNG chunk length");
      return false;
    }

    const uint8_t * type = png + pos + 4;
    const uint8_t * data = png + pos + 8;

    if (memcmp(type, "IHDR", 4) == 0) {
      if (len != 13) {
        free(idat);
        free(png);
        trmnl_set_error(err, err_len, "bad IHDR length");
        return false;
      }
      out->width = trmnl_read_be32(data);
      out->height = trmnl_read_be32(data + 4);
      bit_depth = data[8];
      color_type = data[9];
      interlace = data[12];
      seen_ihdr = true;
    } else if (memcmp(type, "IDAT", 4) == 0) {
      if (!trmnl_append_idat(&idat, &idat_size, data, len)) {
        free(idat);
        free(png);
        trmnl_set_error(err, err_len, "cannot allocate IDAT buffer");
        return false;
      }
    } else if (memcmp(type, "IEND", 4) == 0) {
      seen_iend = true;
      break;
    }

    pos += 12 + len;
  }
  free(png);

  if (!seen_ihdr || !seen_iend || idat_size == 0) {
    free(idat);
    trmnl_set_error(err, err_len, "incomplete PNG");
    return false;
  }
  if (out->width == 0 || out->height == 0 || out->width > 2048 || out->height > 2048) {
    free(idat);
    trmnl_set_error(err, err_len, "unsupported PNG dimensions");
    return false;
  }
  if (bit_depth != 1 || color_type != 0 || interlace != 0) {
    free(idat);
    trmnl_set_error(err, err_len, "expected non-interlaced 1-bit grayscale PNG");
    return false;
  }

  out->stride = (out->width + 7) / 8;
  size_t expected_scanlines = ((size_t)out->stride + 1) * out->height;

  LodePNGDecompressSettings settings;
  lodepng_decompress_settings_init(&settings);
  unsigned char * scanlines = nullptr;
  size_t scanlines_size = 0;
  unsigned lode_error = lodepng_zlib_decompress(&scanlines, &scanlines_size, idat, idat_size, &settings);
  free(idat);

  if (lode_error) {
    if (scanlines) { lodepng_free(scanlines); }
    snprintf(err, err_len, "PNG inflate failed: %u %s", lode_error, lodepng_error_text(lode_error));
    return false;
  }

  if (!trmnl_unfilter_png_rows(scanlines, scanlines_size, out->width, out->height, out->stride, err, err_len)) {
    lodepng_free(scanlines);
    return false;
  }

  out->rows = scanlines;
  out->rows_from_lodepng = true;
  return true;
#endif // USE_LVGL_PNG_DECODER
}

static bool trmnl_source_bit(const void * src_ptr, uint32_t x, uint32_t y) {
  const trmnl_png_i1_t * src = (const trmnl_png_i1_t*)src_ptr;
  if (x >= src->width) { x = src->width - 1; }
  if (y >= src->height) { y = src->height - 1; }
  const uint8_t * row = src->rows + ((size_t)y * src->stride);
  return (row[x >> 3] & (0x80 >> (x & 7))) != 0;
}

static void trmnl_map_pixel(uint32_t dx, uint32_t dy, uint32_t dst_w, uint32_t dst_h, uint32_t src_w, uint32_t src_h, int32_t rotation, uint32_t * sx, uint32_t * sy) {
  rotation %= 360;
  if (rotation < 0) { rotation += 360; }

  if (rotation == 90 || rotation == 270) {
    uint32_t rx = ((uint64_t)dx * src_h) / dst_w;
    uint32_t ry = ((uint64_t)dy * src_w) / dst_h;
    if (rotation == 90) {
      *sx = ry;
      *sy = src_h - 1 - rx;
    } else {
      *sx = src_w - 1 - ry;
      *sy = rx;
    }
  } else {
    uint32_t rx = ((uint64_t)dx * src_w) / dst_w;
    uint32_t ry = ((uint64_t)dy * src_h) / dst_h;
    if (rotation == 180) {
      *sx = src_w - 1 - rx;
      *sy = src_h - 1 - ry;
    } else {
      *sx = rx;
      *sy = ry;
    }
  }
}

static bool trmnl_show_png_1bit(const char * path, int32_t rotation, char * result, size_t result_len) {
  if (!lvgl_started()) {
    start_lvgl(nullptr);
  }
  if (!lvgl_started()) {
    trmnl_set_error(result, result_len, "LVGL is not started");
    return false;
  }

  trmnl_png_i1_t src;
  char err[96];
  err[0] = 0;
  if (!trmnl_load_png_1bit(path, &src, err, sizeof(err))) {
    trmnl_set_error(result, result_len, err);
    return false;
  }

  uint32_t dst_w = lv_display_get_horizontal_resolution(nullptr);
  uint32_t dst_h = lv_display_get_vertical_resolution(nullptr);
  if (dst_w == 0 || dst_h == 0) {
    trmnl_free_png(&src);
    trmnl_set_error(result, result_len, "display has no resolution");
    return false;
  }

  trmnl_release_lvgl();

  lv_trmnl_draw_buf = lv_draw_buf_create(dst_w, dst_h, LV_COLOR_FORMAT_I1, LV_STRIDE_AUTO);
  if (!lv_trmnl_draw_buf) {
    trmnl_free_png(&src);
    trmnl_set_error(result, result_len, "cannot allocate LVGL draw buffer");
    return false;
  }

  lv_draw_buf_set_palette(lv_trmnl_draw_buf, 0, lv_color_to_32(lv_color_white(), LV_OPA_COVER));
  lv_draw_buf_set_palette(lv_trmnl_draw_buf, 1, lv_color_to_32(lv_color_black(), LV_OPA_COVER));

  uint8_t * dst_rows = (uint8_t*)lv_draw_buf_goto_xy(lv_trmnl_draw_buf, 0, 0);
  uint32_t dst_stride = lv_trmnl_draw_buf->header.stride;
  memset(dst_rows, 0, (size_t)dst_stride * dst_h);

  for (uint32_t dy = 0; dy < dst_h; dy++) {
    uint8_t * dst_row = dst_rows + ((size_t)dy * dst_stride);
    for (uint32_t dx = 0; dx < dst_w; dx++) {
      uint32_t sx = 0;
      uint32_t sy = 0;
      trmnl_map_pixel(dx, dy, dst_w, dst_h, src.width, src.height, rotation, &sx, &sy);
      bool source_white = trmnl_source_bit(&src, sx, sy);
      if (!source_white) {
        dst_row[dx >> 3] |= (0x80 >> (dx & 7));
      }
    }
  }

  trmnl_free_png(&src);

  lv_trmnl_screen = lv_obj_create(nullptr);
  lv_obj_set_size(lv_trmnl_screen, dst_w, dst_h);
  lv_obj_set_style_bg_color(lv_trmnl_screen, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(lv_trmnl_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_t * canvas = lv_canvas_create(lv_trmnl_screen);
  lv_canvas_set_draw_buf(canvas, lv_trmnl_draw_buf);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);

  lv_screen_load(lv_trmnl_screen);
  lv_obj_invalidate(lv_trmnl_screen);
  lv_refr_now(lv_display_get_default());

  snprintf(result, result_len, "%lux%lu -> %lux%lu rotation=%ld",
           (unsigned long)src.width, (unsigned long)src.height,
           (unsigned long)dst_w, (unsigned long)dst_h,
           (long)rotation);
  return true;
}

/*********************************************************************************************\
 * Native functions mapped to Berry functions
 *
 * import power
 *
 * power.read() -> map
 *
\*********************************************************************************************/
extern "C" {


  /*********************************************************************************************\
   * Support for Freetype fonts
  \*********************************************************************************************/
  // load freetype font by name in file-system
  int lv0_load_freetype_font(bvm *vm) {
#ifdef USE_LVGL_FREETYPE
    int argc = be_top(vm);
    if (argc == 3 && be_isstring(vm, 1) && be_isint(vm, 2) && be_isint(vm, 3)) {

// lv_font_t * lv_freetype_font_create(const char * pathname, lv_freetype_font_render_mode_t render_mode, uint32_t size,
//                                     lv_freetype_font_style_t style);
      // lv_ft_info_t info = {};
      const char * name = be_tostring(vm, 1);
      int32_t weight = be_toint(vm, 2);
      lv_freetype_font_style_t style = (lv_freetype_font_style_t) be_toint(vm, 3);
      lv_font_t * font = lv_freetype_font_create(name, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, weight, style);
      // lv_ft_font_init(&info);
      // lv_font_t * font = info.font;

      if (font != nullptr) {
        be_find_global_or_module_member(vm, "lv.lv_font");
        be_pushcomptr(vm, font);
        be_call(vm, 1);
        be_pop(vm, 1);
        be_return(vm);
      } else {
        be_return_nil(vm);
      }
    }
    be_raise(vm, kTypeError, nullptr);
#else // USE_LVGL_FREETYPE
    be_raise(vm, "feature_error", "FreeType fonts are not available, use '#define USE_LVGL_FREETYPE 1'");
#endif // USE_LVGL_FREETYPE
  }

  /*********************************************************************************************\
   * Display a TRMNL 1-bit grayscale PNG as a resized LVGL I1 canvas.
  \*********************************************************************************************/
  int lv0_trmnl_show_png(bvm *vm);
  int lv0_trmnl_show_png(bvm *vm) {
    int32_t argc = be_top(vm);
    if (argc >= 1 && be_isstring(vm, 1)) {
      const char * path = be_tostring(vm, 1);
      int32_t rotation = 270;
      if (argc >= 2 && be_isint(vm, 2)) {
        rotation = be_toint(vm, 2);
      }

      char result[128];
      if (trmnl_show_png_1bit(path, rotation, result, sizeof(result))) {
        be_pushstring(vm, result);
        be_return(vm);
      }
      be_raise(vm, "value_error", result);
    }
    be_raise(vm, kTypeError, nullptr);
  }

  /*********************************************************************************************\
   * Support for embedded fonts in Flash
  \*********************************************************************************************/
  // We create tables for Font matching
  // Size of `0` indicates end of table
  typedef struct {
    int16_t size;
    const lv_font_t *font;
  } lv_font_table_t;

  typedef struct {
    const char * name;
    const lv_font_table_t * table;
  } lv_font_names_t;

  // Montserrat Font
  const lv_font_table_t lv_montserrat_fonts[] = {
  #if LV_FONT_MONTSERRAT_8
    {  8, &lv_font_montserrat_8 },
  #endif
  #if LV_FONT_MONTSERRAT_10
    { 10, &lv_font_montserrat_10 },
  #endif
  #if LV_FONT_MONTSERRAT_TASMOTA_10
    { 10, &lv_font_montserrat_tasmota_10 },
  #endif
  #if LV_FONT_MONTSERRAT_12
    { 12, &lv_font_montserrat_12 },
  #endif
  #if LV_FONT_MONTSERRAT_14
    { 14, &lv_font_montserrat_14 },
  #endif
  #if LV_FONT_MONTSERRAT_TASMOTA_14
    { 14, &lv_font_montserrat_tasmota_14 },
  #endif
  #if LV_FONT_MONTSERRAT_16
    { 16, &lv_font_montserrat_16 },
  #endif
  #if LV_FONT_MONTSERRAT_18
    { 18, &lv_font_montserrat_18 },
  #endif
  #if LV_FONT_MONTSERRAT_20
    { 20, &lv_font_montserrat_20 },
  #endif
  #if LV_FONT_MONTSERRAT_TASMOTA_20
    { 20, &lv_font_montserrat_tasmota_20 },
  #endif
  #if LV_FONT_MONTSERRAT_22
    { 22, &lv_font_montserrat_22 },
  #endif
  #if LV_FONT_MONTSERRAT_24
    { 24, &lv_font_montserrat_24 },
  #endif
  #if LV_FONT_MONTSERRAT_26
    { 26, &lv_font_montserrat_26 },
  #endif
  #if LV_FONT_MONTSERRAT_28
    { 28, &lv_font_montserrat_28 },
  #endif
  #if LV_FONT_MONTSERRAT_TASMOTA_28
    { 28, &lv_font_montserrat_tasmota_28 },
  #endif
  #if LV_FONT_MONTSERRAT_28_COMPRESSED
    { 28, &lv_font_montserrat_28_compressed, },
  #endif
  #if LV_FONT_MONTSERRAT_30
    { 30, &lv_font_montserrat_30 },
  #endif
  #if LV_FONT_MONTSERRAT_32
    { 32, &lv_font_montserrat_32 },
  #endif
  #if LV_FONT_MONTSERRAT_34
    { 34, &lv_font_montserrat_34 },
  #endif
  #if LV_FONT_MONTSERRAT_36
    { 36, &lv_font_montserrat_36 },
  #endif
  #if LV_FONT_MONTSERRAT_38
    { 38, &lv_font_montserrat_38 },
  #endif
  #if LV_FONT_MONTSERRAT_40
    { 40, &lv_font_montserrat_40 },
  #endif
  #if LV_FONT_MONTSERRAT_42
    { 42, &lv_font_montserrat_42 },
  #endif
  #if LV_FONT_MONTSERRAT_44
    { 44, &lv_font_montserrat_44 },
  #endif
  #if LV_FONT_MONTSERRAT_46
    { 46, &lv_font_montserrat_46 },
  #endif
  #if LV_FONT_MONTSERRAT_48
    { 48, &lv_font_montserrat_48 },
  #endif
    { 0, nullptr}
  };

  // unscii fonts
  const lv_font_table_t lv_unscii_fonts[] = {
  #if LV_FONT_UNSCII_8
    {  8, &lv_font_unscii_8 },
  #endif
  #if LV_FONT_UNSCII_16
    { 16, &lv_font_unscii_16 },
  #endif
    { 0, nullptr}
  };

  // Seg7 Font
  const lv_font_table_t lv_seg7_fonts[] = {
    {  8, &seg7_8 },
    { 10, &seg7_10 },
    { 12, &seg7_12 },
    { 14, &seg7_14 },
    { 16, &seg7_16 },
    { 18, &seg7_18 },
    { 20, &seg7_20 },
    { 24, &seg7_24 },
    { 28, &seg7_28 },
    { 36, &seg7_36 },
    { 48, &seg7_48 },
    { 0, nullptr}
  };

  // icons Font for sizes not covered by montserrat
  // if montserrat is defined, use it, else import icons font
  const lv_font_table_t lv_icons_fonts[] = {
#if LV_FONT_MONTSERRAT_TASMOTA_10
    { 10, &lv_font_montserrat_tasmota_10 },
#elif defined(FONT_ICONS_10)
    { 10, &lv_font_icons_10 },
#endif

#if LV_FONT_MONTSERRAT_TASMOTA_12
    { 12, &lv_font_montserrat_tasmota_12 },
#elif defined(FONT_ICONS_12)
    { 12, &lv_font_icons_12 },
#endif

#if LV_FONT_MONTSERRAT_TASMOTA_14
    { 14, &lv_font_montserrat_tasmota_14 },
#elif defined(FONT_ICONS_14)
    { 14, &lv_font_icons_14 },
#endif

#if LV_FONT_MONTSERRAT_TASMOTA_16
    { 16, &lv_font_montserrat_tasmota_16 },
#elif defined(FONT_ICONS_16)
    { 16, &lv_font_icons_16 },
#endif

#if LV_FONT_MONTSERRAT_TASMOTA_18
    { 18, &lv_font_montserrat_tasmota_18 },
#elif defined(FONT_ICONS_18)
    { 18, &lv_font_icons_18 },
#endif

#if LV_FONT_MONTSERRAT_TASMOTA_20
    { 20, &lv_font_montserrat_tasmota_20 },
#elif defined(FONT_ICONS_20)
    { 20, &lv_font_icons_20 },
#endif

#if LV_FONT_MONTSERRAT_TASMOTA_22
    { 22, &lv_font_montserrat_tasmota_22 },
#elif defined(FONT_ICONS_22)
    { 22, &lv_font_icons_22 },
#endif

#if LV_FONT_MONTSERRAT_TASMOTA_24
    { 24, &lv_font_montserrat_tasmota_24 },
#elif defined(FONT_ICONS_24)
    { 24, &lv_font_icons_24 },
#endif

#if LV_FONT_MONTSERRAT_TASMOTA_28
    { 28, &lv_font_montserrat_tasmota_28 },
#elif defined(FONT_ICONS_28)
    { 28, &lv_font_icons_28 },
#endif

    { 0, nullptr}
  };

  // // typicons Font
  // const lv_font_table_t lv_typicons_fonts[] = {
  //   { 24, &typicons24 },
  //   { 0, nullptr}
  // };

  // robotocondensed-latin1
  const lv_font_table_t lv_robotocondensed_fonts[] = {
#if ROBOTOCONDENSED_REGULAR_12_LATIN1
    { 12, &robotocondensed_regular_12_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_14_LATIN1
    { 14, &robotocondensed_regular_14_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_16_LATIN1
    { 16, &robotocondensed_regular_16_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_20_LATIN1
    { 20, &robotocondensed_regular_20_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_22_LATIN1
    { 22, &robotocondensed_regular_22_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_24_LATIN1
    { 24, &robotocondensed_regular_24_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_28_LATIN1
    { 28, &robotocondensed_regular_28_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_32_LATIN1
    { 32, &robotocondensed_regular_32_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_36_LATIN1
    { 36, &robotocondensed_regular_36_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_38_LATIN1
    { 38, &robotocondensed_regular_38_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_40_LATIN1
    { 40, &robotocondensed_regular_40_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_44_LATIN1
    { 44, &robotocondensed_regular_44_latin1 },
#endif
#if ROBOTOCONDENSED_REGULAR_48_LATIN1
    { 48, &robotocondensed_regular_48_latin1 },
#endif
    { 0, nullptr}
  };

  // register all included fonts
  const lv_font_names_t lv_embedded_fonts[] = {
    { "icons", lv_icons_fonts },
    { "montserrat", lv_montserrat_fonts },
    { "seg7", lv_seg7_fonts },
    // { "typicons", lv_typicons_fonts },
#ifdef USE_LVGL_HASPMOTA
    { "robotocondensed", lv_robotocondensed_fonts },
#endif
    { "unscii", lv_unscii_fonts},
    { nullptr, nullptr}
  };

  // If size is zero, it is read at arg 1
  int lv_load_embedded_font(bvm *vm, const char * name, int16_t size) {
    if (0 == size) {
      if (be_top(vm) >= 1 && be_isint(vm, 1)) {
        size = be_toindex(vm, 1);
      }
    }
    if (name == nullptr || 0 == size) {
      be_raise(vm, "value_error", "");
    }
    // first look for font
    const lv_font_names_t * font_name_cursor = lv_embedded_fonts;
    for (font_name_cursor = lv_embedded_fonts; font_name_cursor->name; font_name_cursor++) {
      if (strcmp(name, font_name_cursor->name) == 0) break;   // found
    }
    if (font_name_cursor->name == nullptr) {
      be_raisef(vm, "value_error", "unknown font '%s'", name);
    }
    // scan for font size
    const lv_font_table_t * font_entry = font_name_cursor->table;
    for (font_entry = font_name_cursor->table; font_entry->size; font_entry++) {
      if (font_entry->size == size) break;    // found
    }
    if (font_entry->size == 0) {
      be_raisef(vm, "value_error", "unknown font size '%s-%i'", name, size);
    }

    be_find_global_or_module_member(vm, "lv.lv_font");
    be_pushcomptr(vm, (void*)font_entry->font);
    be_call(vm, 1);
    be_pop(vm, 1);
    be_return(vm);
  }

  int lv0_load_montserrat_font(bvm *vm) {
    return lv_load_embedded_font(vm, "montserrat", 0);
  }

  int lv0_load_seg7_font(bvm *vm) {
    return lv_load_embedded_font(vm, "seg7", 0);
  }

  int lv0_load_robotocondensed_latin1_font(bvm *vm) {
#ifdef USE_LVGL_HASPMOTA
    return lv_load_embedded_font(vm, "robotocondensed", 0);
#endif // USE_LVGL_HASPMOTA
    be_raise(vm, kTypeError, nullptr);
  }

  int lv0_load_font_embedded(bvm *vm) {
    if ((be_top(vm) >= 2) && (be_isstring(vm, 1)) && (be_isint(vm, 2))) {
      return lv_load_embedded_font(vm, be_tostring(vm, 1), be_toint(vm, 2));
    }
    be_return_nil(vm);
  }

  /*********************************************************************************************\
   * Tasmota Logo
  \*********************************************************************************************/
  extern const lv_img_dsc_t TASMOTA_Symbol_64;
  extern const lv_img_dsc_t TASMOTA_Symbol_36_white;

  void lv_image_set_tasmota_logo(lv_obj_t * img) {
    lv_image_set_src(img, &TASMOTA_Symbol_64);
  }
  void lv_image_set_tasmota_logo36(lv_obj_t * img) {    // 36x36 for splash screen
    lv_image_set_src(img, &TASMOTA_Symbol_36_white);
  }

  /*********************************************************************************************\
   * LVGL Start
   *
   * Calls uDisplay and starts LVGL
  \*********************************************************************************************/
  // lv.start(instance, instance) -> nil
  int lv0_start(bvm *vm);
  int lv0_start(bvm *vm) {
    int32_t argc = be_top(vm); // Get the number of arguments
    if (argc == 0 || (argc == 1 && be_isstring(vm, 1))) {
      const char * uconfig = nullptr;
      if (argc == 1) {
        uconfig = be_tostring(vm, 1);
      }
      start_lvgl(uconfig);

      // call lv.splash_remove() to kill any current splash screen
      if (be_getglobal(vm, "lv")) {
        if (be_getmember(vm, -1, "splash_remove")) {
          // call it
          be_call(vm, 0);
        }
        be_pop(vm, 1);
      }
      be_pop(vm, 1);

      be_return_nil(vm);
    }
    be_raise(vm, kTypeError, nullptr);
  }

  /*********************************************************************************************\
   * LVGL Input Devices
   *
   * Calls uDisplay and starts LVGL
   *
   * lv.register_button_encoder([inv: bool]) -> nil
  \*********************************************************************************************/
  void lvbe_encoder_with_keys_read(lv_indev_t * drv, lv_indev_data_t*data);

  int lv0_register_button_encoder(bvm *vm);   // add buttons with encoder logic
  int lv0_register_button_encoder(bvm *vm) {
    int32_t argc = be_top(vm); // Get the number of arguments
    bool inverted = false;
    // berry_log_C("lv0_register_button_encoder argc=%d inverted=%d", argc, be_tobool(vm, 1));
    if (argc >= 1) {
      inverted = be_tobool(vm, 1);    // get the inverted flag
    }
    // we need 3 buttons from the template
    int32_t btn0 = Pin(GPIO_INPUT, 0);
    int32_t btn1 = Pin(GPIO_INPUT, 1);
    int32_t btn2 = Pin(GPIO_INPUT, 2);
    if (btn0 < 0 || btn1 < 0 || btn2 < 0) {
      be_raise(vm, "template_error", "You need to configure GPIO Inputs 1/2/3");
    }
    lvbe.btn[0].set_gpio(btn0);
    lvbe.btn[0].set_inverted(inverted);
    lvbe.btn[1].set_gpio(btn1);
    lvbe.btn[1].set_inverted(inverted);
    lvbe.btn[2].set_gpio(btn2);
    lvbe.btn[2].set_inverted(inverted);
    berry_log_C(D_LOG_LVGL "Button Rotary encoder using GPIOs %d,%d,%d%s", btn0, btn1, btn2, inverted ? " (inverted)" : "");

    lvbe.indev = lv_indev_create();
    lv_indev_set_type(lvbe.indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(lvbe.indev, lvbe_encoder_with_keys_read);

    lvbe.indev_list.addHead(lvbe.indev);   // keep track of indevs      // TODO what do we do with it?

    be_find_global_or_module_member(vm, "lv.indev");
    be_pushcomptr(vm, (void*)lvbe.indev);
    be_call(vm, 1);
    be_pop(vm, 1);

    be_return(vm);
  }

  /*********************************************************************************************\
   * LVGL Input Devices - callbacks
  \*********************************************************************************************/

  // typedef struct {
  //   lv_point_t point; /**< For LV_INDEV_TYPE_POINTER the currently pressed point*/
  //   uint32_t key;     /**< For LV_INDEV_TYPE_KEYPAD the currently pressed key*/
  //   uint32_t btn_id;  /**< For LV_INDEV_TYPE_BUTTON the currently pressed button*/
  //   int16_t enc_diff; /**< For LV_INDEV_TYPE_ENCODER number of steps since the previous read*/

  //   lv_indev_state_t state; /**< LV_INDEV_STATE_REL or LV_INDEV_STATE_PR*/
  // } lv_indev_data_t;

  void lvbe_encoder_with_keys_read(lv_indev_t * drv, lv_indev_data_t *data){
    // scan through buttons if we need to report something
    uint32_t i;
    for (i = 0; i < 3; i++) {
      if (lvbe.btn[i].state_changed()) {
        switch (i) {
          case 0: data->key = LV_KEY_LEFT; break;
          case 1: data->key = LV_KEY_ENTER; break;
          case 2: data->key = LV_KEY_RIGHT; break;
          default: break;
        }
        bool state = lvbe.btn[i].clear_state_changed();
        data->state = state ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        // berry_log_C("Button event key %d state %d,%d", data->key, state, data->state);
        break;
      }
    }

    // do we have more to report?
    data->continue_reading = false;
    for (/* continue where we left */; i < 3; i++) {
      if (lvbe.btn[i].state_changed()) {
        data->continue_reading = true;
      }
    }
  }

  /*********************************************************************************************\
   * Screenshot in raw format
  \********************************************************************************************/
  int lv0_screenshot(bvm *vm);
  int lv0_screenshot(bvm *vm) {
    if (!lvgl_started()) { be_return_nil(vm); }

    char fname[32];
    snprintf(fname, sizeof(fname), "/screenshot-%d.bmp", Rtc.utc_time);
    File f = dfsp->open(fname, "w");
    if (f) {
      lvgl_set_screenshot_file(&f);

      uint32_t bmp_width = lv_display_get_horizontal_resolution(nullptr);
      uint32_t bmp_height = lv_display_get_vertical_resolution(nullptr);

      // write BMP header
      static const uint8_t bmp_sign[] = { 0x42, 0x4d };   // BM = Windows
      f.write(bmp_sign, sizeof(bmp_sign));
      size_t bmp_size = bmp_width * bmp_height * LV_COLOR_DEPTH / 8 + 0x42;
      f.write((uint8_t*)&bmp_size, sizeof(bmp_size));
      uint32_t zero = 0;
      f.write((uint8_t*) &zero, sizeof(zero));  // reserved 4-bytes
      uint32_t bmp_offset_to_pixels = 0x42;  // TODO
      f.write((uint8_t*) &bmp_offset_to_pixels, sizeof(bmp_offset_to_pixels));

      size_t bmp_dib_header_size = 0x28;
      f.write((uint8_t*) &bmp_dib_header_size, sizeof(bmp_dib_header_size));

      f.write((uint8_t*) &bmp_width, sizeof(bmp_width));
      f.write((uint8_t*) &bmp_height, sizeof(bmp_height));

      // rest of header
      // BITMAPV2INFOHEADER = 52 bytes header, 40 bytes sub-header
      static const uint8_t bmp_dib_header1[] = {
        0x01, 0x00,                       // planes
          16, 0x00,                       // bits per pixel = 16
        0x03, 0x00, 0x00, 0x00,           // compression = BI_BITFIELDS uncrompressed
      };

      static const uint8_t bmp_dib_header2[] = {
        0xC4, 0xE0, 0x00, 0x00,           // X pixels per meter
        0xC4, 0xE0, 0x00, 0x00,           // Y pixels per meter
        0x00, 0x00, 0x00, 0x00,           // Colors in table
        0x00, 0x00, 0x00, 0x00,           // Important color count

        // RGB masks
        0x00, 0xF8, 0x00, 0x00,           // Red channel mask
        0xE0, 0x07, 0x00, 0x00,           // Green channel mask
        0x1F, 0x00, 0x00, 0x00,           // Blue channel mask
      };
      f.write(bmp_dib_header1, sizeof(bmp_dib_header1));
      f.write((uint8_t*)&bmp_size, sizeof(bmp_size));
      f.write(bmp_dib_header2, sizeof(bmp_dib_header2));
      // now we can write the pixels array

      // redraw screen
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(lv_disp_get_default());

      lvgl_reset_screenshot_file();
      f.close();
    }
    be_pushstring(vm, fname);
    be_return(vm);
  }

  /*********************************************************************************************\
   * Screenshot in raw format
  \********************************************************************************************/
  int lv0_set_paint_cb(bvm *vm);
  int lv0_set_paint_cb(bvm *vm) {
    int32_t argc = be_top(vm); // Get the number of arguments
    if (argc >= 1 && be_iscomptr(vm, 1)) {
      lv_set_paint_cb(be_tocomptr(vm, 1));
    }
    be_pushcomptr(vm, lv_get_paint_cb());
    be_return(vm);
  }

  int lv0_set_stream_cb(bvm *vm);
  int lv0_set_stream_cb(bvm *vm) {
    int32_t argc = be_top(vm); // Get the number of arguments
    if (argc >= 1 && be_iscomptr(vm, 1)) {
      lv_set_stream_cb(be_tocomptr(vm, 1));
      be_return_nil(vm);
    }
    be_pushcomptr(vm, lv_get_paint_cb());
    be_return(vm);
  }
}

#else // USE_LVGL

//   // define weak aliases
//   int32_t b_nrg_read(struct bvm *vm) __attribute__ ((weak, alias ("b_wire_energymissing")));



#endif // USE_LVGL

#endif  // USE_BERRY
