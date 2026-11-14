#pragma once

#define MSNS_FW_VERSION "0.2.0"

// Injected by scripts/version.py at build time.
#ifndef MSNS_GIT_SHA
#define MSNS_GIT_SHA "unknown"
#endif

#ifndef MSNS_BOARD_ID
#define MSNS_BOARD_ID "esp32dev"
#endif
