#pragma once

#ifndef PACKET_CAPTURE_H
#define PACKET_CAPTURE_H

#include "stdafx.h"

// Continuous ethernet capture to SR3:\destiny_netcap.dat (plugin USB folder).
// Format matches dashboard NetCapInfo.dat / xbcap2pcap.
VOID StartPacketCapture();
VOID StopPacketCapture();
BOOL IsPacketCaptureActive();

#endif
