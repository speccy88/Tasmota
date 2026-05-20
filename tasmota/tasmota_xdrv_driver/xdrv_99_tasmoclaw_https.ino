/*
  xdrv_99_tasmoclaw_https.ino

  TasmoClaw ESP-IDF HTTPS bridge
  Provides idf_https_post(url, headers_json, body) to Berry.
  Uses esp_http_client + esp_tls + mbedTLS + esp_crt_bundle.
*/

#if defined(USE_TASMOCLAW_HTTPS) && defined(USE_BERRY) && defined(ESP32)

#include <berry.h>
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "mbedtls/ssl_ciphersuites.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#ifdef USE_UFILESYS
#include <FS.h>
extern FS *ufsp;
#endif

#define TASMOCLAW_HTTPS_MAX_BODY      32768
#define TASMOCLAW_HTTPS_RX_BUFFER     4096
#define TASMOCLAW_HTTPS_TX_BUFFER     4096
#define TASMOCLAW_HTTPS_TIMEOUT_MS    30000
#define TASMOCLAW_UFS_MAX_READ        32768

extern "C" int tasmoclaw_mbedtls_ssl_setup_ret;
extern "C" int tasmoclaw_mbedtls_ssl_setup_in_len;
extern "C" int tasmoclaw_mbedtls_ssl_setup_out_len;
extern "C" int tasmoclaw_mbedtls_ssl_handshake_ret;
extern "C" int tasmoclaw_mbedtls_ssl_handshake_state;

static const int kTasmoClawHttpsCiphersuites[] = {
  MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
  MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
  0
};

static int TasmoClawMbedSend(void *ctx, const unsigned char *buf, size_t len) {
  const int fd = *((int*)ctx);
  const int ret = (int)send(fd, buf, len, 0);
  if (ret >= 0) { return ret; }
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { return MBEDTLS_ERR_SSL_WANT_WRITE; }
  return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int TasmoClawMbedRecv(void *ctx, unsigned char *buf, size_t len) {
  const int fd = *((int*)ctx);
  const int ret = (int)recv(fd, buf, len, 0);
  if (ret >= 0) { return ret; }
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { return MBEDTLS_ERR_SSL_WANT_READ; }
  return MBEDTLS_ERR_NET_RECV_FAILED;
}

static char* TasmoClawAllocBody(size_t size) {
  char *p = (char*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) {
    p = (char*)heap_caps_malloc(size, MALLOC_CAP_8BIT);
  }
  return p;
}

struct TasmoClawHttpsResponse {
  char *body = nullptr;
  size_t len = 0;
  size_t cap = 0;
  bool truncated = false;
};

static const char* TasmoClawEspErrName(esp_err_t err) {
  switch (err) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_HTTP_MAX_REDIRECT: return "ESP_ERR_HTTP_MAX_REDIRECT";
    case ESP_ERR_HTTP_CONNECT: return "ESP_ERR_HTTP_CONNECT";
    case ESP_ERR_HTTP_WRITE_DATA: return "ESP_ERR_HTTP_WRITE_DATA";
    case ESP_ERR_HTTP_FETCH_HEADER: return "ESP_ERR_HTTP_FETCH_HEADER";
    case ESP_ERR_HTTP_INVALID_TRANSPORT: return "ESP_ERR_HTTP_INVALID_TRANSPORT";
    case ESP_ERR_HTTP_CONNECTING: return "ESP_ERR_HTTP_CONNECTING";
    case ESP_ERR_HTTP_EAGAIN: return "ESP_ERR_HTTP_EAGAIN";
    case ESP_ERR_HTTP_CONNECTION_CLOSED: return "ESP_ERR_HTTP_CONNECTION_CLOSED";
    case ESP_ERR_HTTP_NOT_MODIFIED: return "ESP_ERR_HTTP_NOT_MODIFIED";
    case ESP_ERR_HTTP_RANGE_NOT_SATISFIABLE: return "ESP_ERR_HTTP_RANGE_NOT_SATISFIABLE";
    case ESP_ERR_HTTP_READ_TIMEOUT: return "ESP_ERR_HTTP_READ_TIMEOUT";
    case ESP_ERR_HTTP_INCOMPLETE_DATA: return "ESP_ERR_HTTP_INCOMPLETE_DATA";
    default: return esp_err_to_name(err);
  }
}

static String TasmoClawJsonEscape(const char *s, size_t len) {
  String out;
  out.reserve(len + 32);
  for (size_t i = 0; i < len; i++) {
    const uint8_t c = (uint8_t)s[i];
    switch (c) {
      case '\\': out += F("\\\\"); break;
      case '"': out += F("\\\""); break;
      case '\n': out += F("\\n"); break;
      case '\r': out += F("\\r"); break;
      case '\t': out += F("\\t"); break;
      default:
        if (c < 0x20) {
          char buf[7];
          snprintf_P(buf, sizeof(buf), PSTR("\\u%04x"), c);
          out += buf;
        } else {
          out += (char)c;
        }
        break;
    }
  }
  return out;
}

static String TasmoClawJsonEscape(const char *s) {
  return s ? TasmoClawJsonEscape(s, strlen(s)) : String();
}

static String TasmoClawHttpsError(const char *stage, const char *error, esp_err_t err = ESP_OK, int tls_code = 0, int tls_flags = 0) {
  String out;
  out.reserve(340);
  out += F("{\"ok\":false,\"status\":0,\"error\":\"");
  out += TasmoClawJsonEscape(error ? error : "error");
  out += F("\",\"esp_err\":");
  out += String((int32_t)err);
  if (tls_code != 0) {
    out += F(",\"tls_code\":");
    out += String((int32_t)tls_code);
  }
  if (tls_flags != 0) {
    out += F(",\"tls_flags\":");
    out += String((int32_t)tls_flags);
  }
  if (tasmoclaw_mbedtls_ssl_setup_ret != 0) {
    out += F(",\"mbedtls_ssl_setup_ret\":");
    out += String((int32_t)tasmoclaw_mbedtls_ssl_setup_ret);
  }
  if (tasmoclaw_mbedtls_ssl_setup_in_len != 0 || tasmoclaw_mbedtls_ssl_setup_out_len != 0) {
    out += F(",\"mbedtls_ssl_in_len\":");
    out += String((int32_t)tasmoclaw_mbedtls_ssl_setup_in_len);
    out += F(",\"mbedtls_ssl_out_len\":");
    out += String((int32_t)tasmoclaw_mbedtls_ssl_setup_out_len);
  }
  if (tasmoclaw_mbedtls_ssl_handshake_ret != 0) {
    out += F(",\"mbedtls_ssl_handshake_ret\":");
    out += String((int32_t)tasmoclaw_mbedtls_ssl_handshake_ret);
    out += F(",\"mbedtls_ssl_handshake_state\":");
    out += String((int32_t)tasmoclaw_mbedtls_ssl_handshake_state);
  }
  out += F(",\"free_heap\":");
  out += String((uint32_t)esp_get_free_heap_size());
  out += F(",\"largest_8bit_block\":");
  out += String((uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  out += F(",\"free_spiram\":");
  out += String((uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  out += F(",\"largest_spiram_block\":");
  out += String((uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  out += F(",\"stage\":\"");
  out += TasmoClawJsonEscape(stage ? stage : "unknown");
  out += F("\",\"body\":\"\"}");
  return out;
}

static String TasmoClawHttpsSuccess(int status, const char *body, size_t body_len, bool truncated, const char *backend) {
  const String escaped_body = TasmoClawJsonEscape(body, body_len);
  String out;
  out.reserve(escaped_body.length() + 160);
  out += F("{\"ok\":true,\"status\":");
  out += String(status);
  out += F(",\"body\":\"");
  out += escaped_body;
  out += F("\",\"bytes\":");
  out += String((uint32_t)body_len);
  if (backend && backend[0]) {
    out += F(",\"backend\":\"");
    out += TasmoClawJsonEscape(backend);
    out += '"';
  }
  if (truncated) {
    out += F(",\"truncated\":true");
  }
  out += '}';
  return out;
}

static bool TasmoClawJsonExtractString(const char *json, const char *key, String &value) {
  if (!json || !key) { return false; }

  String needle = "\"";
  needle += key;
  needle += "\"";

  const char *p = strstr(json, needle.c_str());
  if (!p) { return false; }

  p += needle.length();
  while (*p && isspace((unsigned char)*p)) { p++; }
  if (*p != ':') { return false; }
  p++;
  while (*p && isspace((unsigned char)*p)) { p++; }
  if (*p != '"') { return false; }
  p++;

  value = "";
  while (*p) {
    char c = *p++;
    if (c == '"') { return true; }
    if (c == '\\') {
      char e = *p++;
      switch (e) {
        case '"': value += '"'; break;
        case '\\': value += '\\'; break;
        case '/': value += '/'; break;
        case 'b': value += '\b'; break;
        case 'f': value += '\f'; break;
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        default: value += e; break;
      }
    } else {
      value += c;
    }
  }
  return false;
}

static void TasmoClawSetHeader(void *client_ptr, const char *headers_json, const char *key) {
  esp_http_client_handle_t client = (esp_http_client_handle_t)client_ptr;
  String value;
  if (TasmoClawJsonExtractString(headers_json, key, value) && value.length() > 0) {
    esp_http_client_set_header(client, key, value.c_str());
  }
}

static bool TasmoClawAppendBody(void *res_ptr, const char *data, size_t len) {
  TasmoClawHttpsResponse *res = (TasmoClawHttpsResponse*)res_ptr;
  if (!res || !res->body || res->cap == 0 || !data || len == 0) { return true; }

  size_t room = (res->cap > res->len) ? (res->cap - res->len - 1) : 0;
  if (room == 0) {
    res->truncated = true;
    return false;
  }

  size_t copy_len = len;
  if (copy_len > room) {
    copy_len = room;
    res->truncated = true;
  }

  memcpy(res->body + res->len, data, copy_len);
  res->len += copy_len;
  res->body[res->len] = 0;
  return copy_len == len;
}

static esp_err_t TasmoClawHttpsEvent(void *evt_arg) {
  esp_http_client_event_t *evt = (esp_http_client_event_t*)evt_arg;
  if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data || !evt->data || evt->data_len <= 0) {
    return ESP_OK;
  }

  TasmoClawHttpsResponse *res = (TasmoClawHttpsResponse*)evt->user_data;
  if (!res->body || res->cap == 0) { return ESP_OK; }

  size_t room = (res->cap > res->len) ? (res->cap - res->len - 1) : 0;
  if (room == 0) {
    res->truncated = true;
    return ESP_OK;
  }

  size_t copy_len = (size_t)evt->data_len;
  if (copy_len > room) {
    copy_len = room;
    res->truncated = true;
  }

  memcpy(res->body + res->len, evt->data, copy_len);
  res->len += copy_len;
  res->body[res->len] = 0;
  return ESP_OK;
}

static bool TasmoClawParseHttpsUrl(const char *url, String &host, String &host_header, String &path, int &port) {
  if (!url || strncmp(url, "https://", 8) != 0) { return false; }

  const char *p = url + 8;
  const char *slash = strchr(p, '/');
  const char *end = slash ? slash : (url + strlen(url));
  if (end <= p) { return false; }

  host_header = String(p).substring(0, end - p);
  host = host_header;
  port = 443;

  int colon = host.indexOf(':');
  if (colon >= 0) {
    port = atoi(host.substring(colon + 1).c_str());
    if (port <= 0) { return false; }
    host = host.substring(0, colon);
  }

  path = slash ? String(slash) : String("/");
  return host.length() > 0;
}

static const char* TasmoClawStrCaseStr(const char *haystack, const char *needle) {
  if (!haystack || !needle || !needle[0]) { return nullptr; }
  const size_t needle_len = strlen(needle);
  for (const char *p = haystack; *p; p++) {
    if (strncasecmp(p, needle, needle_len) == 0) {
      return p;
    }
  }
  return nullptr;
}

static size_t TasmoClawDecodeChunked(char *data, size_t len) {
  if (!data || len == 0) { return 0; }
  size_t src = 0;
  size_t dst = 0;
  while (src < len) {
    while (src < len && (data[src] == '\r' || data[src] == '\n')) { src++; }
    if (src >= len) { break; }

    size_t chunk_size = 0;
    bool have_digit = false;
    while (src < len) {
      const char c = data[src++];
      if (c == ';') {
        while (src < len && data[src] != '\n') { src++; }
        break;
      }
      if (c == '\r' || c == '\n') { break; }
      int v = -1;
      if (c >= '0' && c <= '9') { v = c - '0'; }
      else if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; }
      else if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; }
      else { return dst; }
      have_digit = true;
      chunk_size = (chunk_size << 4) | (size_t)v;
    }
    while (src < len && (data[src] == '\r' || data[src] == '\n')) { src++; }
    if (!have_digit || chunk_size == 0) { break; }
    if (chunk_size > len - src) { chunk_size = len - src; }
    memmove(data + dst, data + src, chunk_size);
    dst += chunk_size;
    src += chunk_size;
    while (src < len && (data[src] == '\r' || data[src] == '\n')) { src++; }
  }
  data[dst] = 0;
  return dst;
}

static void TasmoClawExtractHttpPayload(char *raw_body, size_t raw_len, const char **payload, size_t *payload_len) {
  *payload = "";
  *payload_len = 0;
  if (!raw_body || raw_len == 0) { return; }

  char *body_start = strstr(raw_body, "\r\n\r\n");
  if (!body_start) { return; }

  const bool chunked = TasmoClawStrCaseStr(raw_body, "Transfer-Encoding: chunked") != nullptr;
  *body_start = 0;
  char *body = body_start + 4;
  size_t body_len = raw_len - (body - raw_body);
  if (chunked) {
    body_len = TasmoClawDecodeChunked(body, body_len);
  }
  *payload = body;
  *payload_len = body_len;
}

static String TasmoClawMbedTlsError(const char *stage, const char *message, int ret) {
  char err_buf[96] = {0};
  if (ret != 0) {
    mbedtls_strerror(ret, err_buf, sizeof(err_buf));
  }
  String error = message ? String(message) : String("mbedTLS error");
  if (err_buf[0]) {
    error += F(": ");
    error += err_buf;
  }
  String out = TasmoClawHttpsError(stage, error.c_str(), (esp_err_t)ret);
  const int insert_at = out.lastIndexOf('}');
  if (insert_at > 0) {
    out.remove(insert_at);
    out += F(",\"backend\":\"mbedtls\",\"mbedtls_ret\":");
    out += String((int32_t)ret);
    out += '}';
  }
  return out;
}

static int TasmoClawTcpConnect(const char *host, int port) {
  char port_s[8];
  snprintf_P(port_s, sizeof(port_s), PSTR("%d"), port);

  struct addrinfo hints = {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *res = nullptr;
  if (getaddrinfo(host, port_s, &hints, &res) != 0 || !res) {
    return -1;
  }

  int fd = -1;
  for (struct addrinfo *p = res; p; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) { continue; }

    struct timeval tv = {};
    tv.tv_sec = TASMOCLAW_HTTPS_TIMEOUT_MS / 1000;
    tv.tv_usec = (TASMOCLAW_HTTPS_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      break;
    }
    close(fd);
    fd = -1;
  }

  freeaddrinfo(res);
  return fd;
}

static String TasmoClawMbedTlsPost(const char *url, const char *headers_json, const char *body, size_t body_len) {
  String host;
  String host_header;
  String path;
  int port = 443;
  if (!TasmoClawParseHttpsUrl(url, host, host_header, path, port)) {
    return TasmoClawHttpsError("url", "expected https:// URL");
  }

  int fd = TasmoClawTcpConnect(host.c_str(), port);
  if (fd < 0) {
    return TasmoClawHttpsError("tcp_connect", "TCP connect failed", (esp_err_t)errno);
  }

  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_entropy_context entropy;

  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_entropy_init(&entropy);

  int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                  (const unsigned char*)"TasmoClaw", 9);
  if (ret != 0) {
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return TasmoClawMbedTlsError("ctr_drbg_seed", "mbedtls_ctr_drbg_seed failed", ret);
  }

  ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0) {
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return TasmoClawMbedTlsError("config_defaults", "mbedtls_ssl_config_defaults failed", ret);
  }

  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ciphersuites(&conf, kTasmoClawHttpsCiphersuites);

  esp_err_t bundle_err = esp_crt_bundle_attach(&conf);
  if (bundle_err != ESP_OK) {
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return TasmoClawHttpsError("crt_bundle", "esp_crt_bundle_attach failed", bundle_err);
  }

  ret = mbedtls_ssl_setup(&ssl, &conf);
  if (ret != 0) {
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return TasmoClawMbedTlsError("ssl_setup", "mbedtls_ssl_setup failed", ret);
  }

  ret = mbedtls_ssl_set_hostname(&ssl, host.c_str());
  if (ret != 0) {
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return TasmoClawMbedTlsError("set_hostname", "mbedtls_ssl_set_hostname failed", ret);
  }

  mbedtls_ssl_set_bio(&ssl, &fd, TasmoClawMbedSend, TasmoClawMbedRecv, nullptr);

  uint32_t started = millis();
  while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      close(fd);
      mbedtls_entropy_free(&entropy);
      mbedtls_ctr_drbg_free(&ctr_drbg);
      mbedtls_ssl_config_free(&conf);
      mbedtls_ssl_free(&ssl);
      return TasmoClawMbedTlsError("handshake", "mbedtls_ssl_handshake failed", ret);
    }
    if (millis() - started > TASMOCLAW_HTTPS_TIMEOUT_MS) {
      close(fd);
      mbedtls_entropy_free(&entropy);
      mbedtls_ctr_drbg_free(&ctr_drbg);
      mbedtls_ssl_config_free(&conf);
      mbedtls_ssl_free(&ssl);
      return TasmoClawHttpsError("handshake", "mbedtls_ssl_handshake timeout");
    }
    delay(10);
  }

  const uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
  if (flags != 0) {
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return TasmoClawHttpsError("verify", "certificate verification failed", (esp_err_t)flags);
  }

  String content_type = "application/json";
  String accept = "application/json";
  String connection = "close";
  String user_agent = "TasmoClaw/0.1";
  String authorization;

  TasmoClawJsonExtractString(headers_json, "Content-Type", content_type);
  TasmoClawJsonExtractString(headers_json, "Accept", accept);
  TasmoClawJsonExtractString(headers_json, "Connection", connection);
  TasmoClawJsonExtractString(headers_json, "User-Agent", user_agent);
  TasmoClawJsonExtractString(headers_json, "Authorization", authorization);

  String req;
  req.reserve(path.length() + host_header.length() + body_len + authorization.length() + 256);
  req += F("POST ");
  req += path;
  req += F(" HTTP/1.1\r\nHost: ");
  req += host_header;
  req += F("\r\nContent-Type: ");
  req += content_type;
  req += F("\r\nAccept: ");
  req += accept;
  req += F("\r\nConnection: ");
  req += connection;
  req += F("\r\nUser-Agent: ");
  req += user_agent;
  if (authorization.length() > 0) {
    req += F("\r\nAuthorization: ");
    req += authorization;
  }
  req += F("\r\nContent-Length: ");
  req += String((uint32_t)body_len);
  req += F("\r\n\r\n");
  if (body_len > 0) {
    req.concat(body, body_len);
  }

  const unsigned char *req_data = (const unsigned char*)req.c_str();
  size_t written = 0;
  started = millis();
  while (written < req.length()) {
    ret = mbedtls_ssl_write(&ssl, req_data + written, req.length() - written);
    if (ret > 0) {
      written += (size_t)ret;
      continue;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      if (millis() - started > TASMOCLAW_HTTPS_TIMEOUT_MS) { break; }
      delay(10);
      continue;
    }
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return TasmoClawMbedTlsError("write", "mbedtls_ssl_write failed", ret);
  }

  TasmoClawHttpsResponse raw;
  raw.cap = TASMOCLAW_HTTPS_MAX_BODY + 1;
  raw.body = TasmoClawAllocBody(raw.cap);
  if (!raw.body) {
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return TasmoClawHttpsError("alloc", "malloc failed");
  }
  raw.body[0] = 0;

  unsigned char chunk[TASMOCLAW_HTTPS_RX_BUFFER];
  started = millis();
  while (millis() - started <= TASMOCLAW_HTTPS_TIMEOUT_MS) {
    ret = mbedtls_ssl_read(&ssl, chunk, sizeof(chunk));
    if (ret > 0) {
      TasmoClawAppendBody(&raw, (const char*)chunk, (size_t)ret);
      started = millis();
      continue;
    }
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      break;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      delay(10);
      continue;
    }
    String err = TasmoClawMbedTlsError("read", "mbedtls_ssl_read failed", ret);
    free(raw.body);
    close(fd);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ssl_free(&ssl);
    return err;
  }

  mbedtls_ssl_close_notify(&ssl);
  close(fd);
  mbedtls_entropy_free(&entropy);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_ssl_config_free(&conf);
  mbedtls_ssl_free(&ssl);

  int status = 0;
  char *body_start = strstr(raw.body, "\r\n\r\n");
  if (strncmp(raw.body, "HTTP/", 5) == 0) {
    char *space = strchr(raw.body, ' ');
    if (space) { status = atoi(space + 1); }
  }

  const char *payload = "";
  size_t payload_len = 0;
  TasmoClawExtractHttpPayload(raw.body, raw.len, &payload, &payload_len);

  String out = TasmoClawHttpsSuccess(status, payload, payload_len, raw.truncated, "mbedtls");
  free(raw.body);
  return out;
}

static String TasmoClawTlsLastError(void *tls_ptr, const char *stage, const char *message, esp_err_t fallback_err) {
  esp_tls_t *tls = (esp_tls_t*)tls_ptr;
  int tls_code = 0;
  int tls_flags = 0;
  esp_err_t last_err = fallback_err;
  esp_tls_error_handle_t error_handle = nullptr;
  if (tls && esp_tls_get_error_handle(tls, &error_handle) == ESP_OK && error_handle) {
    esp_err_t captured = esp_tls_get_and_clear_last_error(error_handle, &tls_code, &tls_flags);
    if (captured != ESP_OK) {
      last_err = captured;
    }
  }
  return TasmoClawHttpsError(stage, message, last_err, tls_code, tls_flags);
}

static String TasmoClawEspTlsPost(const char *url, const char *headers_json, const char *body, size_t body_len) {
  String host;
  String host_header;
  String path;
  int port = 443;
  if (!TasmoClawParseHttpsUrl(url, host, host_header, path, port)) {
    return TasmoClawHttpsError("url", "expected https:// URL");
  }

  esp_tls_cfg_t cfg = {};
  cfg.timeout_ms = TASMOCLAW_HTTPS_TIMEOUT_MS;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.common_name = host.c_str();
  cfg.tls_version = ESP_TLS_VER_TLS_1_2;
  cfg.ciphersuites_list = kTasmoClawHttpsCiphersuites;

  esp_tls_t *tls = esp_tls_init();
  if (!tls) {
    return TasmoClawHttpsError("tls_init", "esp_tls_init failed");
  }

  const int connected = esp_tls_conn_new_sync(host.c_str(), host.length(), port, &cfg, tls);
  if (connected != 1) {
    String err = TasmoClawTlsLastError(tls, "tls_connect", "esp_tls_conn_new_sync failed", (esp_err_t)connected);
    esp_tls_conn_destroy(tls);
    String direct = TasmoClawMbedTlsPost(url, headers_json, body, body_len);
    if (direct.indexOf("\"ok\":true") >= 0) {
      return direct;
    }
    return direct.length() > 0 ? direct : err;
  }

  String content_type = "application/json";
  String accept = "application/json";
  String connection = "close";
  String user_agent = "TasmoClaw/0.1";
  String authorization;

  TasmoClawJsonExtractString(headers_json, "Content-Type", content_type);
  TasmoClawJsonExtractString(headers_json, "Accept", accept);
  TasmoClawJsonExtractString(headers_json, "Connection", connection);
  TasmoClawJsonExtractString(headers_json, "User-Agent", user_agent);
  TasmoClawJsonExtractString(headers_json, "Authorization", authorization);

  String req;
  req.reserve(path.length() + host_header.length() + body_len + authorization.length() + 256);
  req += F("POST ");
  req += path;
  req += F(" HTTP/1.1\r\nHost: ");
  req += host_header;
  req += F("\r\nContent-Type: ");
  req += content_type;
  req += F("\r\nAccept: ");
  req += accept;
  req += F("\r\nConnection: ");
  req += connection;
  req += F("\r\nUser-Agent: ");
  req += user_agent;
  if (authorization.length() > 0) {
    req += F("\r\nAuthorization: ");
    req += authorization;
  }
  req += F("\r\nContent-Length: ");
  req += String((uint32_t)body_len);
  req += F("\r\n\r\n");
  if (body_len > 0) {
    req.concat(body, body_len);
  }

  const char *req_data = req.c_str();
  size_t written = 0;
  while (written < req.length()) {
    ssize_t w = esp_tls_conn_write(tls, req_data + written, req.length() - written);
    if (w <= 0) {
      esp_tls_conn_destroy(tls);
      return TasmoClawHttpsError("tls_write", "esp_tls_conn_write failed", (esp_err_t)w);
    }
    written += (size_t)w;
  }

  TasmoClawHttpsResponse raw;
  raw.cap = TASMOCLAW_HTTPS_MAX_BODY + 1;
  raw.body = TasmoClawAllocBody(raw.cap);
  if (!raw.body) {
    esp_tls_conn_destroy(tls);
    return TasmoClawHttpsError("alloc", "malloc failed");
  }
  raw.body[0] = 0;

  char chunk[TASMOCLAW_HTTPS_RX_BUFFER];
  while (true) {
    ssize_t r = esp_tls_conn_read(tls, chunk, sizeof(chunk));
    if (r > 0) {
      TasmoClawAppendBody(&raw, chunk, (size_t)r);
      continue;
    }
    if (r == 0) { break; }
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
      delay(10);
      continue;
    }
    free(raw.body);
    esp_tls_conn_destroy(tls);
    return TasmoClawHttpsError("tls_read", "esp_tls_conn_read failed", (esp_err_t)r);
  }

  esp_tls_conn_destroy(tls);

  int status = 0;
  char *body_start = strstr(raw.body, "\r\n\r\n");
  if (strncmp(raw.body, "HTTP/", 5) == 0) {
    char *space = strchr(raw.body, ' ');
    if (space) { status = atoi(space + 1); }
  }

  const char *payload = "";
  size_t payload_len = 0;
  TasmoClawExtractHttpPayload(raw.body, raw.len, &payload, &payload_len);

  String out = TasmoClawHttpsSuccess(status, payload, payload_len, raw.truncated, "esp_tls");
  free(raw.body);
  return out;
}

extern "C" int tasmoclaw_idf_https_post(bvm *vm);
extern "C" int tasmoclaw_idf_https_post(bvm *vm) {
  const int32_t argc = be_top(vm);
  if (argc < 3 || !be_isstring(vm, 1) || !be_isstring(vm, 2) || !be_isstring(vm, 3)) {
    be_pushstring(vm, "{\"ok\":false,\"status\":0,\"error\":\"idf_https_post(url, headers_json, body) expects three strings\",\"esp_err\":0,\"stage\":\"args\",\"body\":\"\"}");
    be_return(vm);
  }

  const char *url = be_tostring(vm, 1);
  const char *headers_json = be_tostring(vm, 2);
  const char *body = be_tostring(vm, 3);
  const size_t body_len = body ? strlen(body) : 0;

  if (url && strncmp(url, "https://", 8) == 0) {
    String out = TasmoClawMbedTlsPost(url, headers_json, body, body_len);
    be_pushstring(vm, out.c_str());
    be_return(vm);
  }

  TasmoClawHttpsResponse response;
  response.cap = TASMOCLAW_HTTPS_MAX_BODY + 1;
  response.body = TasmoClawAllocBody(response.cap);
  if (!response.body) {
    String err = TasmoClawHttpsError("alloc", "malloc failed");
    be_pushstring(vm, err.c_str());
    be_return(vm);
  }
  response.body[0] = 0;

  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = TASMOCLAW_HTTPS_TIMEOUT_MS;
  config.buffer_size = TASMOCLAW_HTTPS_RX_BUFFER;
  config.buffer_size_tx = TASMOCLAW_HTTPS_TX_BUFFER;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  config.event_handler = (http_event_handle_cb)TasmoClawHttpsEvent;
  config.user_data = &response;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    free(response.body);
    String err = TasmoClawHttpsError("init", "esp_http_client_init failed");
    be_pushstring(vm, err.c_str());
    be_return(vm);
  }

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "Connection", "close");

  TasmoClawSetHeader(client, headers_json, "Content-Type");
  TasmoClawSetHeader(client, headers_json, "Accept");
  TasmoClawSetHeader(client, headers_json, "Authorization");
  TasmoClawSetHeader(client, headers_json, "Connection");
  TasmoClawSetHeader(client, headers_json, "User-Agent");

  if (body && body_len > 0) {
    esp_http_client_set_post_field(client, body, body_len);
  } else {
    esp_http_client_set_post_field(client, "", 0);
  }

  esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);

  String out;
  if (err != ESP_OK && err == ESP_ERR_HTTP_INVALID_TRANSPORT && url && strncmp(url, "https://", 8) == 0) {
    out = TasmoClawEspTlsPost(url, headers_json, body, body_len);
  } else if (err != ESP_OK) {
    out = TasmoClawHttpsError("perform", TasmoClawEspErrName(err), err);
  } else {
    out = TasmoClawHttpsSuccess(status, response.body, response.len, response.truncated, "esp_http_client");
  }

  esp_http_client_cleanup(client);
  free(response.body);

  be_pushstring(vm, out.c_str());
  be_return(vm);
}

#ifdef USE_UFILESYS
static String TasmoClawUfsPath(const char *path) {
  if (!path || !path[0]) { return String("/"); }
  String out(path);
  out.replace("\\", "_");
  if (out.indexOf("..") >= 0) {
    out.replace("..", "_");
  }
  if (!out.startsWith("/")) {
    out = "/" + out;
  }
  return out;
}

static String TasmoClawUfsError(const char *stage, const char *error, const char *path = nullptr) {
  String out;
  out.reserve(220);
  out += F("{\"ok\":false,\"error\":\"");
  out += TasmoClawJsonEscape(error ? error : "UFS error");
  out += F("\",\"stage\":\"");
  out += TasmoClawJsonEscape(stage ? stage : "unknown");
  out += '"';
  if (path) {
    out += F(",\"path\":\"");
    out += TasmoClawJsonEscape(path);
    out += '"';
  }
  out += '}';
  return out;
}

static String TasmoClawUfsReadJson(const char *path, int32_t max_bytes) {
  if (!ufsp) { return TasmoClawUfsError("fs", "UFS is not available", path); }
  if (max_bytes <= 0) { max_bytes = 4096; }
  if (max_bytes > TASMOCLAW_UFS_MAX_READ) { max_bytes = TASMOCLAW_UFS_MAX_READ; }

  String clean_path = TasmoClawUfsPath(path);
  File f = ufsp->open(clean_path.c_str(), "r");
  if (!f || f.isDirectory()) {
    return TasmoClawUfsError("open", "file not found or not readable", clean_path.c_str());
  }

  char *buf = TasmoClawAllocBody((size_t)max_bytes + 1);
  if (!buf) {
    f.close();
    return TasmoClawUfsError("alloc", "malloc failed", clean_path.c_str());
  }

  size_t len = f.read((uint8_t*)buf, (size_t)max_bytes);
  bool truncated = f.available() > 0;
  buf[len] = 0;
  f.close();

  String out;
  String escaped = TasmoClawJsonEscape(buf, len);
  out.reserve(escaped.length() + 150);
  out += F("{\"ok\":true,\"path\":\"");
  out += TasmoClawJsonEscape(clean_path.c_str());
  out += F("\",\"body\":\"");
  out += escaped;
  out += F("\",\"bytes\":");
  out += String((uint32_t)len);
  if (truncated) { out += F(",\"truncated\":true"); }
  out += '}';
  free(buf);
  return out;
}

static String TasmoClawUfsWriteJson(const char *path, const char *body) {
  if (!ufsp) { return TasmoClawUfsError("fs", "UFS is not available", path); }
  if (!body) { body = ""; }

  String clean_path = TasmoClawUfsPath(path);
  File f = ufsp->open(clean_path.c_str(), "w");
  if (!f) {
    return TasmoClawUfsError("open", "file is not writable", clean_path.c_str());
  }

  const size_t len = strlen(body);
  const size_t written = f.write((const uint8_t*)body, len);
  f.close();

  String out;
  out.reserve(150);
  out += F("{\"ok\":");
  out += (written == len) ? F("true") : F("false");
  out += F(",\"path\":\"");
  out += TasmoClawJsonEscape(clean_path.c_str());
  out += F("\",\"bytes\":");
  out += String((uint32_t)written);
  if (written != len) {
    out += F(",\"error\":\"short write\"");
  }
  out += '}';
  return out;
}

static String TasmoClawUfsListJson(const char *path) {
  if (!ufsp) { return TasmoClawUfsError("fs", "UFS is not available", path); }

  String clean_path = TasmoClawUfsPath(path);
  File dir = ufsp->open(clean_path.c_str(), "r");
  if (!dir) {
    return TasmoClawUfsError("open", "path not found", clean_path.c_str());
  }

  String out;
  out.reserve(1024);
  out += F("{\"ok\":true,\"path\":\"");
  out += TasmoClawJsonEscape(clean_path.c_str());
  out += F("\",\"entries\":[");

  bool first = true;
  bool truncated = false;
  if (dir.isDirectory()) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) { break; }
      if (out.length() > 6000) {
        truncated = true;
        entry.close();
        break;
      }
      if (!first) { out += ','; }
      const char *name = entry.name();
      out += F("{\"name\":\"");
      out += TasmoClawJsonEscape(name ? name : "");
      out += F("\",\"size\":");
      out += String((uint32_t)entry.size());
      out += F(",\"dir\":");
      out += entry.isDirectory() ? F("true") : F("false");
      out += '}';
      first = false;
      entry.close();
      yield();
    }
  } else {
    out += F("{\"name\":\"");
    out += TasmoClawJsonEscape(clean_path.c_str());
    out += F("\",\"size\":");
    out += String((uint32_t)dir.size());
    out += F(",\"dir\":false}");
  }

  dir.close();
  out += ']';
  if (truncated) { out += F(",\"truncated\":true"); }
  out += '}';
  return out;
}
#endif

extern "C" int tasmoclaw_ufs_read(bvm *vm);
extern "C" int tasmoclaw_ufs_read(bvm *vm) {
#ifdef USE_UFILESYS
  const int32_t argc = be_top(vm);
  if (argc < 1 || !be_isstring(vm, 1)) {
    be_pushstring(vm, "{\"ok\":false,\"error\":\"tasmo_ufs_read(path, max_bytes) expects a path string\",\"stage\":\"args\"}");
    be_return(vm);
  }
  const char *path = be_tostring(vm, 1);
  int32_t max_bytes = 4096;
  if (argc >= 2 && be_isint(vm, 2)) {
    max_bytes = be_toint(vm, 2);
  }
  String out = TasmoClawUfsReadJson(path, max_bytes);
  be_pushstring(vm, out.c_str());
#else
  be_pushstring(vm, "{\"ok\":false,\"error\":\"USE_UFILESYS is not enabled\",\"stage\":\"fs\"}");
#endif
  be_return(vm);
}

extern "C" int tasmoclaw_ufs_write(bvm *vm);
extern "C" int tasmoclaw_ufs_write(bvm *vm) {
#ifdef USE_UFILESYS
  const int32_t argc = be_top(vm);
  if (argc < 2 || !be_isstring(vm, 1) || !be_isstring(vm, 2)) {
    be_pushstring(vm, "{\"ok\":false,\"error\":\"tasmo_ufs_write(path, body) expects two strings\",\"stage\":\"args\"}");
    be_return(vm);
  }
  String out = TasmoClawUfsWriteJson(be_tostring(vm, 1), be_tostring(vm, 2));
  be_pushstring(vm, out.c_str());
#else
  be_pushstring(vm, "{\"ok\":false,\"error\":\"USE_UFILESYS is not enabled\",\"stage\":\"fs\"}");
#endif
  be_return(vm);
}

extern "C" int tasmoclaw_ufs_list(bvm *vm);
extern "C" int tasmoclaw_ufs_list(bvm *vm) {
#ifdef USE_UFILESYS
  const int32_t argc = be_top(vm);
  const char *path = "/";
  if (argc >= 1 && be_isstring(vm, 1)) {
    path = be_tostring(vm, 1);
  }
  String out = TasmoClawUfsListJson(path);
  be_pushstring(vm, out.c_str());
#else
  be_pushstring(vm, "{\"ok\":false,\"error\":\"USE_UFILESYS is not enabled\",\"stage\":\"fs\"}");
#endif
  be_return(vm);
}

extern "C" void be_load_tasmoclaw_https_lib(bvm *vm);
extern "C" void be_load_tasmoclaw_https_lib(bvm *vm) {
  be_pushntvfunction(vm, tasmoclaw_idf_https_post);
  be_setglobal(vm, "idf_https_post");
  be_pop(vm, 1);
  be_pushntvfunction(vm, tasmoclaw_ufs_read);
  be_setglobal(vm, "tasmo_ufs_read");
  be_pop(vm, 1);
  be_pushntvfunction(vm, tasmoclaw_ufs_write);
  be_setglobal(vm, "tasmo_ufs_write");
  be_pop(vm, 1);
  be_pushntvfunction(vm, tasmoclaw_ufs_list);
  be_setglobal(vm, "tasmo_ufs_list");
  be_pop(vm, 1);
}

#endif  // USE_TASMOCLAW_HTTPS && USE_BERRY && ESP32
