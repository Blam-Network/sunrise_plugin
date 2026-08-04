#pragma once

#ifndef BAP_KEY_DUMP_H
#define BAP_KEY_DUMP_H

#include "stdafx.h"

// Hook Destiny AES-GCM session setup and append key/IV dumps to SR3:\destiny_bap_keys.txt
// (JSON lines). Correlate with destiny_netcap.dat via shared ms-since-capture timestamps.
VOID StartBapKeyDump();
VOID StopBapKeyDump();
BOOL IsBapKeyDumpActive();

#endif
