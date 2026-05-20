/*
  Thin global include wrapper for TasmoClaw's local mbedTLS client config.

  The full shim library directory contains internal headers such as common.h.
  Keep those out of the global include path so ESP-IDF components do not pick
  them up by accident while still allowing MBEDTLS_USER_CONFIG_FILE to resolve.
*/

#pragma once

#include "../library/tasmoclaw_mbedtls_user_config.h"
