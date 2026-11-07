// Plaintext packet capture for Destiny retail 2.3.1 (default_ttk_231, xex
// timestamp 0x579810CC, title 415608F8) on Bungie's release servers.
//
// The hooks sit where the game handles BAP frames and XHttp requests before
// encryption and after decryption, so everything is recorded in the clear.
// Hook code only copies the frame into a lock protected ring and returns;
// BAP runs on network job worker threads, bdXHTTP on its bd thread. The
// plugin has no threads: the game's own per-frame XamInputGetState call (its
// import is redirected) sees the start/stop combo and drains the ring to the file.
//
// Addresses come from the 2.3.1 IDB (IDA :13341), see web-tiger-day0
// docs/ttk231-capture-hooks.md. Names are descriptive, true names unknown.
// File format: docs/capture-format.md.

#include "stdafx.h"
#include "DestinyCapture.h"
#include "CaptureOverlay.h"
#include "Detour.h"
#include "Utilities.h"
#include "Sunrise3.h"
#include "CoreHooks.h"
#include <ppcintrinsics.h>

static const DWORD DestinyTitle = 0x415608F8;

// --- record layout (docs/capture-format.md) -----------------------------------

static const DWORD PCAP_MAGIC = 0xA1B2C3D4;
static const DWORD LINKTYPE_USER0 = 147;
static const DWORD SNAPLEN = 256 * 1024;
static const DWORD PCAP_RECORD_SIZE = 16;
static const DWORD RECORD_HEADER_SIZE = 40;
static const DWORD RECORD_VERSION = 1;
static const DWORD MAX_HEADER = 4096;

static const BYTE DIR_SEND = 0;
static const BYTE DIR_RECEIVE = 1;
static const BYTE DIR_LOCAL = 2;

static const BYTE CH_META = 0;
static const BYTE CH_BAP = 1;
static const BYTE CH_XHTTP = 2;

// flags
static const WORD FLAG_KIND_MASK = 0x0003;  // BAP envelope kind: 0 plain, 1 GCM, 2 secure hello
static const WORD FLAG_KIND_KNOWN = 0x0004; // the kind bits are set (BAP send only)
static const WORD FLAG_TRUNCATED = 0x0008;  // stored bytes are shorter than the frame
static const WORD FLAG_UNREADABLE = 0x0010; // a pointer failed validation, bytes dropped

// channel 2 types
static const WORD XHTTP_REQUEST = 1;
static const WORD XHTTP_REQUEST_BODY = 2;
static const WORD XHTTP_RESPONSE_BODY = 3;
static const WORD XHTTP_STATUS = 4;

// channel 0 types
static const WORD META_START = 1;
static const WORD META_STOP = 2;
static const WORD META_BAP_KEY = 3;
static const WORD META_DROPPED = 4;
static const WORD META_BAP_CONNECT = 5;

// --- game offsets (2.3.1) -------------------------------------------------------

// bap_connection (constructor sub_82CC5608).
static const DWORD CONN_INDEX = 0x10F4;       // "BAP connection %d"
static const DWORD CONN_STATE = 0x10F8;       // 2 connecting, 9 failed (bap_connection_connect)
static const DWORD CONN_REMOTE_IP = 0x15F0;   // u32, first octet in the high byte
static const DWORD CONN_REMOTE_PORT = 0x1600; // u16
static const DWORD CONN_REMOTE_TEXT = 0x1604; // "a.b.c.d:port"
static const DWORD CONN_RX_HEADER = 0x167C;   // 128 bytes
static const DWORD CONN_RX_HEADER_LEN = 0x16FC;
static const DWORD CONN_RX_WIRE = 0x1700;     // u16
static const DWORD CONN_RX_BODY_LEN = 0x178C;
static const DWORD CONN_RX_BODY = 0x1790;
static const DWORD CONN_CHANNEL = 0x1BF0;     // secure channel, r3 of encode and set_key

// Outgoing message, *(pending + 0x14) in bap_frame_encode.
static const DWORD PENDING_MESSAGE = 0x14;
static const DWORD MSG_WIRE = 0x0C;           // u16
static const DWORD MSG_HEADER = 0x90;         // 128 bytes
static const DWORD MSG_HEADER_LEN = 0x110;
static const DWORD MSG_BODY_LEN = 0x114;
static const DWORD MSG_BODY = 0x118;
static const DWORD BAP_HEADER_MAX = 0x80;

// bdXHTTP operation (bdXHTTP::sendXHTTPRequest sub_83168E08).
static const DWORD OP_RESPONSE_BUFFER = 0x42C; // caller supplied buffer, else stream below
static const DWORD OP_STREAM_BUFFER = 0x43C;
static const DWORD OP_VERB = 0x5C4;            // inline string
static const DWORD OP_STREAM = 0x5D8;
static const DWORD OP_REQUEST = 0x5F0;         // XHttpOpenRequest handle
static const DWORD OP_HOST = 0x624;            // char*
static const DWORD OP_PORT = 0x62C;            // u16
static const DWORD OP_PATH = 0x640;            // char*
static const DWORD OP_CONNECT_FLAGS = 0x650;
static const DWORD OP_HEADERS = 0x7D4;         // inline string
static const DWORD OP_RECEIVED = 0x47DC;       // bytes received so far

static const DWORD XHTTP_STATUS_HEADERS_AVAILABLE = 0x00020000;
static const DWORD XHTTP_STATUS_READ_COMPLETE = 0x00080000;
static const DWORD XHTTP_STATUS_WRITE_COMPLETE = 0x00100000;
static const DWORD XHTTP_STATUS_REQUEST_ERROR = 0x00200000;
static const DWORD XHTTP_STATUS_SENDREQUEST_COMPLETE = 0x00400000;

// --- state ------------------------------------------------------------------------

static CRITICAL_SECTION g_ringLock;
// g_stateLock serialises start/stop against the loader's relaunch stop.
static CRITICAL_SECTION g_stateLock;
static BOOL g_lockReady = FALSE;

// g_armed: hooks installed in the running image. g_capturing: hooks record.
// Both only change under g_ringLock, hooks peek without it first.
static volatile BOOL g_armed = FALSE;
static volatile BOOL g_capturing = FALSE;

// Fixed at plugin load, never allocated at runtime. Drained every frame, so
// 1 MB holds seconds of BAP traffic even if the file stalls. Plugins load into
// system memory, so keep this small.
#define RING_SIZE (1 * 1024 * 1024)
static BYTE g_ring[RING_SIZE];
static const DWORD g_ringSize = RING_SIZE;
static DWORD g_ringRead = 0;
static DWORD g_ringWrite = 0;
static DWORD g_ringUsed = 0;

static volatile DWORD g_sent = 0;
static volatile DWORD g_received = 0;
// Capture files closed since the title loaded, shown on the overlay.
static DWORD g_capturesSaved = 0;
static volatile DWORD g_dropped = 0;
static volatile DWORD g_faults = 0;

static ULONGLONG g_baseMicros = 0;
static ULONGLONG g_baseTimebase = 0;
static ULONGLONG g_timebaseHz = 0;

// Worker thread only.
static HANDLE g_file = INVALID_HANDLE_VALUE;
static CHAR g_fileName[64];
static DWORD g_fileBytes = 0;
static DWORD g_droppedReported = 0;
static BOOL g_outOfSpaceNotified = FALSE;

// --- small helpers (safe on game threads: no CRT, no allocation) ------------------

static VOID Put16(BYTE* p, DWORD v)
{
	p[0] = (BYTE)v;
	p[1] = (BYTE)(v >> 8);
}

static VOID Put32(BYTE* p, DWORD v)
{
	p[0] = (BYTE)v;
	p[1] = (BYTE)(v >> 8);
	p[2] = (BYTE)(v >> 16);
	p[3] = (BYTE)(v >> 24);
}

static VOID Put64(BYTE* p, ULONGLONG v)
{
	Put32(p, (DWORD)v);
	Put32(p + 4, (DWORD)(v >> 32));
}

// First and last byte mapped. Heap blocks are contiguous, the SEH guard in
// Record catches anything else.
static BOOL Readable(DWORD addr, DWORD size)
{
	if (size == 0)
		return TRUE;
	DWORD last = addr + size - 1;
	if (addr < 0x10000 || last < addr)
		return FALSE;
	return MmIsAddressValid((PVOID)addr) && MmIsAddressValid((PVOID)last);
}

static DWORD Read32(DWORD addr)
{
	return *(volatile DWORD*)addr;
}

static WORD Read16(DWORD addr)
{
	return *(volatile WORD*)addr;
}

// strlen that stops at max and at the first unmapped page.
static DWORD BoundedLength(DWORD addr, DWORD max)
{
	if (!addr || !MmIsAddressValid((PVOID)addr))
		return 0;
	DWORD n = 0;
	while (n < max) {
		DWORD p = addr + n;
		if ((p & 0xFFF) == 0 && !MmIsAddressValid((PVOID)p))
			break;
		if (*(volatile CHAR*)p == 0)
			break;
		n++;
	}
	return n;
}

// Tiny text builder for records made on game threads.
struct TextBuf
{
	CHAR text[768];
	DWORD len;
};

static VOID Append(TextBuf* t, const CHAR* s, DWORD n)
{
	for (DWORD i = 0; i < n && t->len < sizeof(t->text); i++)
		t->text[t->len++] = s[i];
}

static VOID AppendStr(TextBuf* t, const CHAR* s)
{
	while (*s && t->len < sizeof(t->text))
		t->text[t->len++] = *s++;
}

static VOID AppendGame(TextBuf* t, DWORD addr, DWORD max)
{
	DWORD n = BoundedLength(addr, max);
	if (n)
		Append(t, (const CHAR*)addr, n);
}

static VOID AppendDec(TextBuf* t, DWORD v)
{
	CHAR digits[10];
	DWORD n = 0;
	do {
		digits[n++] = (CHAR)('0' + v % 10);
		v /= 10;
	} while (v && n < sizeof(digits));
	while (n)
		Append(t, &digits[--n], 1);
}

static VOID AppendHex(TextBuf* t, DWORD v)
{
	static const CHAR hex[] = "0123456789ABCDEF";
	for (int shift = 28; shift >= 0; shift -= 4)
		Append(t, &hex[(v >> shift) & 0xF], 1);
}

static ULONGLONG NowMicros()
{
	ULONGLONG hz = g_timebaseHz;
	if (!hz)
		return g_baseMicros;
	ULONGLONG ticks = __mftb() - g_baseTimebase;
	return g_baseMicros + (ticks / hz) * 1000000 + ((ticks % hz) * 1000000) / hz;
}

// --- ring (caller holds g_ringLock) ---------------------------------------------------

static VOID RingPut(const VOID* data, DWORD size)
{
	const BYTE* src = (const BYTE*)data;
	DWORD first = g_ringSize - g_ringWrite;
	if (first > size)
		first = size;
	memcpy(g_ring + g_ringWrite, src, first);
	if (size > first)
		memcpy(g_ring, src + first, size - first);
	g_ringWrite = (g_ringWrite + size) % g_ringSize;
	g_ringUsed += size;
}

static DWORD RingTake(BYTE* out, DWORD max)
{
	DWORD n = g_ringUsed < max ? g_ringUsed : max;
	if (!n)
		return 0;
	DWORD first = g_ringSize - g_ringRead;
	if (first > n)
		first = n;
	memcpy(out, g_ring + g_ringRead, first);
	if (n > first)
		memcpy(out + first, g_ring, n - first);
	g_ringRead = (g_ringRead + n) % g_ringSize;
	g_ringUsed -= n;
	return n;
}

// --- records ------------------------------------------------------------------------

struct RecordInfo
{
	BYTE direction;
	BYTE channel;
	WORD flags;
	DWORD connection;
	DWORD ip;   // first octet in the high byte, 0 unknown
	WORD port;
	WORD type;
};

// Copies one record into the ring. force: used by the worker for the stop
// record after hooks were switched off.
static VOID Record(RecordInfo info, DWORD header, DWORD headerLen, DWORD body, DWORD bodyLen, BOOL force)
{
	if ((!g_capturing && !force) || !g_lockReady)
		return;

	// pcap orig_len: what the frame really had, before any truncation.
	DWORD fullLen = RECORD_HEADER_SIZE + headerLen + bodyLen;
	if (fullLen < bodyLen || fullLen < headerLen)
		fullLen = 0xFFFFFFFF;
	if (headerLen > MAX_HEADER) {
		headerLen = MAX_HEADER;
		info.flags |= FLAG_TRUNCATED;
	}
	if (bodyLen > SNAPLEN - RECORD_HEADER_SIZE - headerLen) {
		bodyLen = SNAPLEN - RECORD_HEADER_SIZE - headerLen;
		info.flags |= FLAG_TRUNCATED;
	}
	if (!Readable(header, headerLen) || !Readable(body, bodyLen)) {
		headerLen = bodyLen = 0;
		info.flags |= FLAG_UNREADABLE;
	}

	ULONGLONG us = NowMicros();
	DWORD storedLen = RECORD_HEADER_SIZE + headerLen + bodyLen;

	BYTE pre[PCAP_RECORD_SIZE + RECORD_HEADER_SIZE];
	Put32(pre + 0, (DWORD)(us / 1000000));
	Put32(pre + 4, (DWORD)(us % 1000000));
	Put32(pre + 8, storedLen);
	Put32(pre + 12, fullLen);

	BYTE* h = pre + PCAP_RECORD_SIZE;
	h[0] = 'S'; h[1] = 'R'; h[2] = 'C'; h[3] = 'P';
	Put16(h + 4, RECORD_VERSION);
	Put16(h + 6, RECORD_HEADER_SIZE);
	Put64(h + 8, us);
	h[16] = info.direction;
	h[17] = info.channel;
	Put16(h + 18, info.flags);
	Put32(h + 20, info.connection);
	h[24] = (BYTE)(info.ip >> 24);
	h[25] = (BYTE)(info.ip >> 16);
	h[26] = (BYTE)(info.ip >> 8);
	h[27] = (BYTE)info.ip;
	Put16(h + 28, info.port);
	Put16(h + 30, info.type);
	Put32(h + 32, headerLen);
	Put32(h + 36, bodyLen);

	DWORD total = sizeof(pre) + headerLen + bodyLen;

	EnterCriticalSection(&g_ringLock);
	if ((g_capturing || force) && g_ring) {
		if (g_ringSize - g_ringUsed < total) {
			g_dropped++;
		}
		else {
			DWORD write = g_ringWrite, used = g_ringUsed;
			BOOL ok = TRUE;
			__try {
				RingPut(pre, sizeof(pre));
				if (headerLen)
					RingPut((const VOID*)header, headerLen);
				if (bodyLen)
					RingPut((const VOID*)body, bodyLen);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				ok = FALSE;
			}
			if (!ok) {
				// A game buffer vanished under us. Forget the partial record.
				g_ringWrite = write;
				g_ringUsed = used;
				g_faults++;
			}
			else if (info.channel != CH_META) {
				if (info.direction == DIR_SEND)
					g_sent++;
				else if (info.direction == DIR_RECEIVE)
					g_received++;
			}
		}
	}
	LeaveCriticalSection(&g_ringLock);
}

static VOID RecordText(BYTE channel, WORD type, const CHAR* text, BOOL force)
{
	RecordInfo info = { DIR_LOCAL, channel, 0, 0, 0, 0, type };
	Record(info, 0, 0, (DWORD)text, (DWORD)strlen(text), force);
}

static VOID FillConnection(RecordInfo* info, DWORD conn)
{
	info->connection = Read32(conn + CONN_INDEX);
	info->ip = Read32(conn + CONN_REMOTE_IP);
	info->port = Read16(conn + CONN_REMOTE_PORT);
}

// --- BAP hooks -----------------------------------------------------------------------

// bap_frame_encode sub_82D035D8(chan = conn + 0x1BF0, encrypt, pending, out
// header buffer, out capacity (128), int* out length). Single caller 0x82CC51E4.
// The body is encrypted in place after this, so record on the way in.
typedef DWORD (*BapFrameEncode_t)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
static BapFrameEncode_t o_bapFrameEncode;

static DWORD BapFrameEncodeHook(DWORD chan, DWORD encrypt, DWORD pending, DWORD out, DWORD outCap, DWORD outLen)
{
	if (g_capturing && pending && chan > CONN_CHANNEL) __try {
		DWORD msg = Read32(pending + PENDING_MESSAGE);
		if (msg) {
			WORD wire = Read16(msg + MSG_WIRE);
			DWORD headerLen = Read32(msg + MSG_HEADER_LEN);
			// Same decision as the encoder: 25 and 30 go as secure hello (kind 2),
			// the rest GCM when asked. It refuses (and the caller retries) when the
			// envelope and header don't fit, so skip those to avoid duplicates.
			BOOL hello = wire == 25 || wire == 30;
			BOOL gcm = (encrypt & 0xFF) && !hello;
			DWORD prefix = gcm ? 22 : 6;
			if (headerLen <= BAP_HEADER_MAX && (int)(headerLen + prefix) <= (int)outCap) {
				RecordInfo info = { DIR_SEND, CH_BAP, 0, 0, 0, 0, wire };
				info.flags = FLAG_KIND_KNOWN | (hello ? 2 : gcm ? 1 : 0);
				FillConnection(&info, chan - CONN_CHANNEL);
				Record(info, msg + MSG_HEADER, headerLen, Read32(msg + MSG_BODY), Read32(msg + MSG_BODY_LEN), FALSE);
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_faults++;
	}
	return o_bapFrameEncode(chan, encrypt, pending, out, outCap, outLen);
}

// bap_recv_deliver sub_82CC56C8(conn, bool* out, int* error): rx state 7, a
// complete frame (plain or already decrypted) about to be handed on. Single
// caller 0x82CC5D90.
typedef DWORD (*BapRecvDeliver_t)(DWORD, DWORD, DWORD);
static BapRecvDeliver_t o_bapRecvDeliver;

static DWORD BapRecvDeliverHook(DWORD conn, DWORD delivered, DWORD error)
{
	if (g_capturing && conn) __try {
		DWORD headerLen = Read32(conn + CONN_RX_HEADER_LEN);
		if (headerLen <= BAP_HEADER_MAX) {
			RecordInfo info = { DIR_RECEIVE, CH_BAP, 0, 0, 0, 0, Read16(conn + CONN_RX_WIRE) };
			FillConnection(&info, conn);
			Record(info, conn + CONN_RX_HEADER, headerLen, Read32(conn + CONN_RX_BODY), Read32(conn + CONN_RX_BODY_LEN), FALSE);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_faults++;
	}
	return o_bapRecvDeliver(conn, delivered, error);
}

// bap_connection_connect sub_82CC5F90(conn). Record once it picked an address
// and moved the state on (2 connecting, 9 failed).
typedef DWORD (*BapConnect_t)(DWORD);
static BapConnect_t o_bapConnect;

static DWORD BapConnectHook(DWORD conn)
{
	DWORD before = conn ? Read32(conn + CONN_STATE) : 0;
	DWORD result = o_bapConnect(conn);
	if (g_capturing && conn) __try {
		DWORD state = Read32(conn + CONN_STATE);
		if (state != before && Read32(conn + CONN_REMOTE_IP)) {
			RecordInfo info = { DIR_LOCAL, CH_META, 0, 0, 0, 0, META_BAP_CONNECT };
			FillConnection(&info, conn);
			TextBuf t;
			t.len = 0;
			AppendGame(&t, conn + CONN_REMOTE_TEXT, 32);
			AppendStr(&t, " state ");
			AppendDec(&t, state);
			Record(info, (DWORD)t.text, t.len, 0, 0, FALSE);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_faults++;
	}
	return result;
}

// bap_channel_set_key sub_832750E0(chan, key[16], iv[12], is_server), from the
// secure hello response. Logged so captures can also be checked against a
// wire capture with the old decode.mjs (sunrise-capture.mjs --keys).
typedef DWORD (*BapSetKey_t)(DWORD, DWORD, DWORD, DWORD);
static BapSetKey_t o_bapSetKey;

static DWORD BapSetKeyHook(DWORD chan, DWORD key, DWORD iv, DWORD isServer)
{
	if (g_capturing && chan > CONN_CHANNEL && Readable(key, 16) && Readable(iv, 12)) {
		BYTE body[36];
		memcpy(body, (const VOID*)key, 16);
		memcpy(body + 16, (const VOID*)iv, 12);
		body[28] = (BYTE)(isServer & 0xFF ? 1 : 0);
		body[29] = body[30] = body[31] = 0;
		Put32(body + 32, chan);
		RecordInfo info = { DIR_LOCAL, CH_META, 0, 0, 0, 0, META_BAP_KEY };
		FillConnection(&info, chan - CONN_CHANNEL);
		Record(info, 0, 0, (DWORD)body, sizeof(body), FALSE);
	}
	return o_bapSetKey(chan, key, iv, isServer);
}

// --- XHttp hooks (bdXHTTP, bd thread) ------------------------------------------------

// bdXHTTP::sendXHTTPRequest sub_83168E08(op). After it returns TRUE the
// request handle exists, and it ties the body and response records together.
typedef DWORD (*SendXHttpRequest_t)(DWORD);
static SendXHttpRequest_t o_sendXHttpRequest;

static DWORD SendXHttpRequestHook(DWORD op)
{
	DWORD result = o_sendXHttpRequest(op);
	if (g_capturing && (result & 0xFF) && op) __try {
		TextBuf t;
		t.len = 0;
		AppendGame(&t, op + OP_VERB, 16);
		AppendStr(&t, " ");
		AppendGame(&t, Read32(op + OP_PATH), 512);
		AppendStr(&t, "\r\nHost: ");
		AppendGame(&t, Read32(op + OP_HOST), 128);
		AppendStr(&t, ":");
		AppendDec(&t, Read16(op + OP_PORT));
		AppendStr(&t, "\r\nX-Connect-Flags: 0x");
		AppendHex(&t, Read32(op + OP_CONNECT_FLAGS));
		AppendStr(&t, "\r\n");

		RecordInfo info = { DIR_SEND, CH_XHTTP, 0, Read32(op + OP_REQUEST), 0, Read16(op + OP_PORT), XHTTP_REQUEST };
		Record(info, (DWORD)t.text, t.len, op + OP_HEADERS, BoundedLength(op + OP_HEADERS, 4096), FALSE);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_faults++;
	}
	return result;
}

// XHttpWriteData wrapper sub_8336AF28(hRequest, buffer, length, overlapped).
typedef DWORD (*XHttpWriteData_t)(DWORD, DWORD, DWORD, DWORD);
static XHttpWriteData_t o_xhttpWriteData;

static DWORD XHttpWriteDataHook(DWORD request, DWORD buffer, DWORD length, DWORD overlapped)
{
	if (g_capturing && buffer && length) {
		RecordInfo info = { DIR_SEND, CH_XHTTP, 0, request, 0, 0, XHTTP_REQUEST_BODY };
		Record(info, 0, 0, buffer, length, FALSE);
	}
	return o_xhttpWriteData(request, buffer, length, overlapped);
}

// bdXHTTP::httpOpStatusCallback sub_831685A0(hRequest, op, status, info,
// info length). READ_COMPLETE carries the next response chunk: it was read
// to *(op+0x42C) + received so far, or to *(op+0x43C) when streaming
// (bdXHTTP::downloadDataChunk, 0x83168248 / 0x831682BC).
typedef VOID (*XHttpStatus_t)(DWORD, DWORD, DWORD, DWORD, DWORD);
static XHttpStatus_t o_xhttpStatus;

static VOID XHttpStatusHook(DWORD request, DWORD op, DWORD status, DWORD statusInfo, DWORD statusLen)
{
	if (g_capturing && op && Readable(op, OP_RECEIVED + 4)) __try {
		if (status == XHTTP_STATUS_READ_COMPLETE && statusLen) {
			DWORD data = 0;
			DWORD buffer = Read32(op + OP_RESPONSE_BUFFER);
			if (buffer)
				data = buffer + Read32(op + OP_RECEIVED);
			else if (Read32(op + OP_STREAM))
				data = Read32(op + OP_STREAM_BUFFER);
			if (data) {
				RecordInfo info = { DIR_RECEIVE, CH_XHTTP, 0, request, 0, Read16(op + OP_PORT), XHTTP_RESPONSE_BODY };
				Record(info, 0, 0, data, statusLen, FALSE);
			}
		}
		else if (status == XHTTP_STATUS_HEADERS_AVAILABLE || status == XHTTP_STATUS_REQUEST_ERROR ||
			status == XHTTP_STATUS_SENDREQUEST_COMPLETE || status == XHTTP_STATUS_WRITE_COMPLETE) {
			// Status word, then up to 16 bytes of the status info as passed.
			BYTE body[20];
			DWORD n = statusInfo && statusLen ? (statusLen < 16 ? statusLen : 16) : 0;
			Put32(body, status);
			if (n && Readable(statusInfo, n))
				memcpy(body + 4, (const VOID*)statusInfo, n);
			else
				n = 0;
			RecordInfo info = { DIR_LOCAL, CH_XHTTP, 0, request, 0, Read16(op + OP_PORT), XHTTP_STATUS };
			Record(info, 0, 0, (DWORD)body, 4 + n, FALSE);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_faults++;
	}
	o_xhttpStatus(request, op, status, statusInfo, statusLen);
}

// --- hook table -------------------------------------------------------------------------

// The hooks are installed only while a capture runs and removed when it stops,
// so a title load with no Xbox Live sign-in never touches the game.
enum HookGroup { GROUP_BAP, GROUP_XHTTP, GROUP_OVERLAY };

struct CaptureHookSite
{
	const char* name;
	DWORD address;
	DWORD prologue[4]; // first 16 bytes, checked before hooking
	const VOID* target; // our hook
	void** origSlot;    // set to the trampoline (call this to reach the original)
	HookGroup group;
	BOOL required;      // capture will not start without it
	BYTE saved[16];     // original prologue bytes, restored on removal
	BOOL installed;
};

// Indices 7 and 8 are the overlay hooks (CaptureOverlay.cpp), gated by
// bCaptureOverlay; the rest are the BAP/XHttp capture hooks.
#define HOOK_SWAP 7
#define HOOK_WATERMARK 8
#define HOOK_SWAP_ISR 9
static CaptureHookSite g_hooks[10] = {
	{ "bap_frame_encode", 0x82D035D8, { 0x7D8802A6, 0x4850A3A1, 0x9421FE40, 0x81650014 }, (const VOID*)BapFrameEncodeHook, (void**)&o_bapFrameEncode, GROUP_BAP, TRUE },
	{ "bap_recv_deliver", 0x82CC56C8, { 0x7D8802A6, 0x485482B1, 0x9421FF60, 0x3B800000 }, (const VOID*)BapRecvDeliverHook, (void**)&o_bapRecvDeliver, GROUP_BAP, TRUE },
	{ "bap_connection_connect", 0x82CC5F90, { 0x7D8802A6, 0x485479F9, 0x9421FF90, 0x8163000C }, (const VOID*)BapConnectHook, (void**)&o_bapConnect, GROUP_BAP, FALSE },
	{ "bap_channel_set_key", 0x832750E0, { 0x7D8802A6, 0x4BF988A5, 0x9421FF80, 0x7C7F1B78 }, (const VOID*)BapSetKeyHook, (void**)&o_bapSetKey, GROUP_BAP, FALSE },
	{ "bdXHTTP::httpOpStatusCallback", 0x831685A0, { 0x7D8802A6, 0x480A53E9, 0xDBE1FFD8, 0x9421FF80 }, (const VOID*)XHttpStatusHook, (void**)&o_xhttpStatus, GROUP_XHTTP, FALSE },
	{ "bdXHTTP::sendXHTTPRequest", 0x83168E08, { 0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FFA0 }, (const VOID*)SendXHttpRequestHook, (void**)&o_sendXHttpRequest, GROUP_XHTTP, FALSE },
	{ "XHttpWriteData wrapper", 0x8336AF28, { 0x7CC73378, 0x7CA62B78, 0x7C852378, 0x7C641B78 }, (const VOID*)XHttpWriteDataHook, (void**)&o_xhttpWriteData, GROUP_XHTTP, FALSE },
	{ "D3DDevice_Swap", 0x831AAF18, { 0x7D8802A6, 0x48062A49, 0x9421FE20, 0x7C7F1B78 }, (const VOID*)SwapHook, (void**)&o_swap, GROUP_OVERLAY, FALSE },
	{ "watermark format", 0x82474AA8, { 0x7D8802A6, 0x48D98EC5, 0x9421FC00, 0x3D608371 }, (const VOID*)WatermarkHook, (void**)&o_watermark, GROUP_OVERLAY, FALSE },
	{ "D3D swap interrupt", 0x831AA0E8, { 0x7D8802A6, 0x4806388D, 0x9421FF50, 0x7C7E1B78 }, (const VOID*)SwapInterruptHook, (void**)&o_swapInterrupt, GROUP_OVERLAY, FALSE },
};

// One fixed, cache-line-aligned trampoline per hook. It is rebuilt on each
// Start into the same slot, so the shared Detour buffer is never used and
// nothing grows across start/stop cycles.
__declspec(align(128)) static BYTE g_tramp[10][128];

// PowerPC self-modifying code: a plain store leaves the new bytes in the data
// cache while the instruction fetch reads stale memory. Push the written lines
// to memory (dcbst + sync) so the first fetch of a just-patched site or a
// freshly built trampoline sees them; isync clears our own pipeline.
static VOID FlushCode(const void* addr, DWORD size)
{
	DWORD start = (DWORD)addr & ~127;
	DWORD end = ((DWORD)addr + size + 127) & ~127;
	for (DWORD p = start; p < end; p += 128)
		__dcbst(0, (const void*)p);
	__sync();
	__emit(0x4C00012C); // isync
}

// Builds a fresh trampoline and writes the jump. The console's image calls
// __savegprlr at a different address than the IDA copy, so any bl is accepted
// as the second prologue word; Detour relocates it into the trampoline.
static BOOL BuildAndInstall(CaptureHookSite* h, DWORD index)
{
	// Hooks stay in (as passthrough) between captures until the title exits.
	if (h->installed)
		return TRUE;

	const DWORD* code = (const DWORD*)h->address;
	for (DWORD i = 0; i < 4; i++) {
		BOOL relinked = i == 1 && (code[i] & 0xFC000003) == 0x48000001;
		if (code[i] != h->prologue[i] && !relinked) {
			Sunrise_Dbg("[capture] %s at %08X: prologue %08X %08X %08X %08X not the expected one, not hooking",
				h->name, h->address, code[0], code[1], code[2], code[3]);
			return FALSE;
		}
	}
	if (code[1] != h->prologue[1])
		Sunrise_Dbg("[capture] %s at %08X: second word %08X (expected %08X), hooking anyway",
			h->name, h->address, code[1], h->prologue[1]);

	Sunrise_Dbg("[capture] installing %s at %08X -> %08X", h->name, h->address, (DWORD)h->target);

	memcpy(h->saved, (const void*)h->address, 16);

	BYTE* tramp = g_tramp[index];
	BYTE* d = tramp;
	for (DWORD k = 0; k < 4; k++)
		d += Detour::CopyInstruction((UINT32*)d, (const UINT32*)(h->address + k * 4));
	d += Detour::WriteFarBranch(d, (const void*)(h->address + 16), false, true);
	FlushCode(tramp, (DWORD)(d - tramp));
	*h->origSlot = tramp;

	// Write the jump into our hook, then push it to memory before it runs.
	Detour::WriteFarBranch((void*)h->address, h->target, false, false);
	FlushCode((const void*)h->address, 16);

	h->installed = TRUE;
	Sunrise_Dbg("[capture] installed %s, trampoline %08X", h->name, (DWORD)tramp);
	return TRUE;
}

// Restores the original bytes. Guarded, since on a title change the image may
// already be gone. The trampoline slot is left pointing where it was: any hook
// call already in flight finishes through it, and the next Start overwrites it.
static VOID RemoveHook(CaptureHookSite* h)
{
	if (!h->installed)
		return;
	__try {
		memcpy((void*)h->address, h->saved, 16);
		FlushCode((const void*)h->address, 16);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		Sunrise_Dbg("[capture] %s: restore faulted (image gone?), left as is", h->name);
	}
	h->installed = FALSE;
}

// Installs the enabled groups. Rolls back and returns FALSE when a required
// hook can't go in.
static BOOL InstallHooks()
{
	DWORD installed = 0;
	BOOL ok = TRUE;
	for (DWORD i = 0; i < ARRAYSIZE(g_hooks); i++) {
		BOOL groupOn = g_hooks[i].group == GROUP_BAP ? bCaptureHooksBap :
			g_hooks[i].group == GROUP_XHTTP ? bCaptureHooksXHttp : bCaptureOverlay;
		if (!groupOn) {
			Sunrise_Dbg("[capture] %s: disabled by ini, skipped", g_hooks[i].name);
			if (g_hooks[i].required)
				ok = FALSE;
			continue;
		}
		if (BuildAndInstall(&g_hooks[i], i))
			installed++;
		else if (g_hooks[i].required)
			ok = FALSE;
	}
	if (!ok) {
		Sunrise_Dbg("[capture] a required BAP hook is missing, not starting");
		for (DWORD i = 0; i < ARRAYSIZE(g_hooks); i++)
			RemoveHook(&g_hooks[i]);
		return FALSE;
	}
	Sunrise_Dbg("[capture] %u/%u hooks installed", installed, ARRAYSIZE(g_hooks));
	return TRUE;
}

static VOID RemoveHooks()
{
	for (DWORD i = 0; i < ARRAYSIZE(g_hooks); i++)
		RemoveHook(&g_hooks[i]);
}

// --- capture file -----------------------------------------------------

static BOOL WriteAll(const VOID* data, DWORD size)
{
	DWORD written = 0;
	if (!WriteFile(g_file, data, size, &written, NULL) || written != size) {
		Sunrise_Dbg("[capture] WriteFile failed (%u), %u of %u bytes", GetLastError(), written, size);
		return FALSE;
	}
	g_fileBytes += size;
	return TRUE;
}

// Moves up to `budget` bytes from the ring to the file. Records are whole in
// the ring, so any split between chunks still lands in order.
static BOOL Drain(DWORD budget)
{
	static BYTE chunk[64 * 1024];
	DWORD moved = 0;
	while (moved < budget) {
		EnterCriticalSection(&g_ringLock);
		DWORD n = RingTake(chunk, sizeof(chunk));
		LeaveCriticalSection(&g_ringLock);
		if (!n)
			break;
		if (!WriteAll(chunk, n))
			return FALSE;
		moved += n;
	}
	return TRUE;
}

static HANDLE OpenCaptureFile()
{
	SYSTEMTIME st;
	GetLocalTime(&st);

	CHAR path[MAX_PATH];
	HANDLE file = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 2 && file == INVALID_HANDLE_VALUE; attempt++) {
		if (attempt == 1) {
			// SR3: is mounted when the title loads, from XAM's context. Mount it
			// again from here in case this thread doesn't see that link.
			MountPath(MOUNT_POINT, GetMountPath());
		}
		CreateDirectory(MOUNT_POINT "\\packets", NULL);
		for (int n = 1; n <= 9 && file == INVALID_HANDLE_VALUE; n++) {
			if (n == 1)
				sprintf_s(g_fileName, sizeof(g_fileName), "%04u-%02u-%02u_%02u-%02u-%02u.pcap",
					st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
			else
				sprintf_s(g_fileName, sizeof(g_fileName), "%04u-%02u-%02u_%02u-%02u-%02u_%d.pcap",
					st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, n);
			sprintf_s(path, sizeof(path), MOUNT_POINT "\\packets\\%s", g_fileName);
			file = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS)
				break;
		}
		if (file == INVALID_HANDLE_VALUE)
			Sunrise_Dbg("[capture] can't create %s (%u)", path, GetLastError());
	}
	return file;
}

static VOID StartCapture()
{
	Sunrise_Trace("[capture] StartCapture enter");
	EnterCriticalSection(&g_stateLock);
	if (g_capturing || g_file != INVALID_HANDLE_VALUE) {
		LeaveCriticalSection(&g_stateLock);
		return;
	}

	// Hook the game only now. The hooks are passthrough until g_capturing is set.
	if (!InstallHooks()) {
		LeaveCriticalSection(&g_stateLock);
		XNotify(L"Capture: hooks unavailable");
		return;
	}

	g_file = OpenCaptureFile();
	if (g_file == INVALID_HANDLE_VALUE) {
		LeaveCriticalSection(&g_stateLock);
		XNotify(L"Capture: can't create the packets file");
		return;
	}
	g_fileBytes = 0;

	BYTE global[24];
	Put32(global + 0, PCAP_MAGIC);
	Put16(global + 4, 2);
	Put16(global + 6, 4);
	Put32(global + 8, 0);
	Put32(global + 12, 0);
	Put32(global + 16, SNAPLEN);
	Put32(global + 20, LINKTYPE_USER0);
	if (!WriteAll(global, sizeof(global))) {
		CloseHandle(g_file);
		g_file = INVALID_HANDLE_VALUE;
		LeaveCriticalSection(&g_stateLock);
		XNotify(L"Capture: can't write the packets file");
		return;
	}

	// Wall clock once, then the timebase for everything after it.
	FILETIME now;
	KeQuerySystemTime(&now);
	ULONGLONG ft = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
	LARGE_INTEGER hz;
	QueryPerformanceFrequency(&hz);

	EnterCriticalSection(&g_ringLock);
	g_ringRead = g_ringWrite = g_ringUsed = 0;
	g_sent = g_received = g_dropped = g_faults = 0;
	g_droppedReported = 0;
	g_outOfSpaceNotified = FALSE;
	g_baseMicros = (ft - 116444736000000000ULL) / 10;
	g_baseTimebase = __mftb();
	g_timebaseHz = hz.QuadPart;
	g_capturing = TRUE;
	LeaveCriticalSection(&g_ringLock);

	PLDR_DATA_TABLE_ENTRY xex = (PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle;
	CHAR text[160];
	sprintf_s(text, sizeof(text), "title=%08X xex=%08X plugin=Sunrise3 %s file=%s timebase=%llu",
		XamGetCurrentTitleId(), xex ? xex->TimeDateStamp : 0, SunriseVers, g_fileName, g_timebaseHz);
	RecordText(CH_META, META_START, text, FALSE);

	Sunrise_Dbg("[capture] started: packets\\%s", g_fileName);
	static WCHAR note[96];
	swprintf_s(note, ARRAYSIZE(note), L"Capture started: packets\\%S", g_fileName);
	XNotify(note);

	OverlaySetStatus(0, 0, g_capturesSaved);
	OverlayOnStart(g_hooks[HOOK_SWAP].installed, g_hooks[HOOK_WATERMARK].installed,
		g_hooks[HOOK_SWAP_ISR].installed);

	LeaveCriticalSection(&g_stateLock);
}

static VOID StopCapture(const char* why)
{
	Sunrise_Trace("[capture] StopCapture(%s) enter, file %p capturing %d", why, g_file, g_capturing);
	EnterCriticalSection(&g_stateLock);
	if (!g_capturing && g_file == INVALID_HANDLE_VALUE) {
		LeaveCriticalSection(&g_stateLock);
		return;
	}

	// Stop recording first so any in-flight hook call falls straight through.
	EnterCriticalSection(&g_ringLock);
	g_capturing = FALSE;
	LeaveCriticalSection(&g_ringLock);

	// The hooks stay in as passthrough until the title exits: nothing is
	// restored while a network thread may be running through them.
	OverlayOnStop();

	CHAR text[128];
	sprintf_s(text, sizeof(text), "sent=%u received=%u dropped=%u faults=%u reason=%s",
		g_sent, g_received, g_dropped, g_faults, why);
	RecordText(CH_META, META_STOP, text, TRUE);

	Drain(0xFFFFFFFF);
	if (g_file != INVALID_HANDLE_VALUE) {
		FlushFileBuffers(g_file);
		CloseHandle(g_file);
		g_file = INVALID_HANDLE_VALUE;
		g_capturesSaved++;
	}

	Sunrise_Dbg("[capture] stopped (%s): packets\\%s, %u sent, %u received, %u dropped, %u faults, %u bytes",
		why, g_fileName, g_sent, g_received, g_dropped, g_faults, g_fileBytes);
	// One toast per capture end, whatever ended it (combo, title change, error).
	static WCHAR note[128];
	swprintf_s(note, ARRAYSIZE(note), L"Capture saved: %S (%u sent, %u received, %u dropped)",
		g_fileName, g_sent, g_received, g_dropped);
	XNotify(note);

	LeaveCriticalSection(&g_stateLock);
}
// --- pad ----------------------------------------------------------------------------------

static const WORD COMBO_HOLD = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_Y;

static WORD g_previousButtons[XUSER_MAX_COUNT];
static DWORD g_lastFlush = 0, g_lastLog = 0, g_lastStatusToast = 0;

// The overlay hooks go in once the title has been polling pads for a while,
// rather than at title load: well clear of boot, on the game's own thread, and
// by then it is certainly rendering. Drawing the combo hint needs the Swap hook
// in before the first capture, so it cannot wait for StartCapture.
static const DWORD OVERLAY_ARM_FRAMES = 300;
static DWORD g_overlayFrames = 0;
static BOOL g_overlayArmed = FALSE;

static VOID ArmOverlay()
{
	g_overlayArmed = TRUE;
	BuildAndInstall(&g_hooks[HOOK_SWAP], HOOK_SWAP);
	BuildAndInstall(&g_hooks[HOOK_WATERMARK], HOOK_WATERMARK);
	BuildAndInstall(&g_hooks[HOOK_SWAP_ISR], HOOK_SWAP_ISR);
	OverlayArm(g_hooks[HOOK_SWAP].installed, g_hooks[HOOK_WATERMARK].installed,
		g_hooks[HOOK_SWAP_ISR].installed);
}

// Everything runs from the game's own per-frame input call: no plugin threads.
// Drains the ring to the file, at most 256 KB per call so a frame never waits long.
static VOID CaptureTick()
{
	if (g_file == INVALID_HANDLE_VALUE)
		return;

	if (!g_capturing) {
		StopCapture("hooks off");
		return;
	}

	// The ring filled up and frames were lost. Once per capture.
	if (g_dropped && !g_outOfSpaceNotified) {
		g_outOfSpaceNotified = TRUE;
		XNotify(L"out of packet space tell codie");
	}

	if (g_dropped != g_droppedReported) {
		CHAR text[64];
		sprintf_s(text, sizeof(text), "dropped=%u (ring full)", g_dropped);
		g_droppedReported = g_dropped;
		RecordText(CH_META, META_DROPPED, text, FALSE);
	}

	if (!Drain(256 * 1024)) {
		StopCapture("write failed");
		return;
	}

	OverlaySetStatus(g_sent, g_received, g_capturesSaved);

	DWORD now = GetTickCount();
	// USB sticks only show new bytes to a PC after a flush.
	if (now - g_lastFlush >= 5000) {
		FlushFileBuffers(g_file);
		g_lastFlush = now;
	}
	// When the overlay is off or has given up, show the counts as a toast.
	if ((!bCaptureOverlay || OverlayFailed()) && now - g_lastStatusToast >= 6000) {
		static WCHAR status[96];
		swprintf_s(status, ARRAYSIZE(status), L"capturing packets (%u sent, %u received, %u saved)",
			g_sent, g_received, g_capturesSaved);
		XNotify(status);
		g_lastStatusToast = now;
	}
	if (now - g_lastLog >= 10000) {
		Sunrise_Dbg("[capture] %u sent, %u received, %u dropped, %u KB written",
			g_sent, g_received, g_dropped, g_fileBytes / 1024);
		g_lastLog = now;
	}
}

// The game polls every pad through its XamInputGetState import each frame.
// The combo is watched here, and the capture file is written from here too.
static DWORD XamInputGetStateHook(DWORD user, DWORD flags, PXINPUT_STATE state)
{
	// Every call for the first 300, then every 300th, to find a freeze.
	static DWORD calls = 0;
	calls++;
	BOOL trace = calls <= 300 || calls % 300 == 0;
	if (trace)
		Sunrise_Trace("[capture] input #%u in: user %u flags %08X state %p", calls, user, flags, state);
	DWORD result = XamInputGetState(user, flags, state);
	if (trace)
		Sunrise_Trace("[capture] input #%u out: result %u buttons %04X", calls, result,
			result == ERROR_SUCCESS && state ? state->Gamepad.wButtons : 0);
	if (!g_armed || user >= XUSER_MAX_COUNT)
		return result;

	if (result == ERROR_SUCCESS && state) {
		WORD buttons = state->Gamepad.wButtons;
		BOOL held = (buttons & COMBO_HOLD) == COMBO_HOLD;
		BOOL edge = (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) && !(g_previousButtons[user] & XINPUT_GAMEPAD_LEFT_SHOULDER);
		g_previousButtons[user] = buttons;
		if (held && edge) {
			Sunrise_Trace("[capture] combo on pad %u, capture %s", user, g_file == INVALID_HANDLE_VALUE ? "starting" : "stopping");
			if (g_file == INVALID_HANDLE_VALUE) {
				StartCapture();
				g_lastFlush = g_lastLog = g_lastStatusToast = GetTickCount();
			}
			else {
				StopCapture("pad combo");
			}
		}
	}

	// Once per frame: the game polls pad 0 first.
	if (user == 0) {
		if (bCaptureOverlay && !g_overlayArmed && ++g_overlayFrames >= OVERLAY_ARM_FRAMES)
			ArmOverlay();
		CaptureTick();
	}
	if (trace)
		Sunrise_Trace("[capture] input #%u done", calls);
	return result;
}

// --- API --------------------------------------------------------------------------------------

VOID SetupDestinyCapture()
{
	if (!bPacketCapture) {
		Sunrise_Dbg("[capture] PacketCapture is off in the ini");
		return;
	}
	if (!g_lockReady) {
		InitializeCriticalSection(&g_ringLock);
		InitializeCriticalSection(&g_stateLock);
		g_lockReady = TRUE;
	}

	// Nothing runs and nothing is hooked in the game's code until the combo:
	// only the input import is redirected so the combo can be seen.
	Sunrise_Trace("[capture] setup: locks ready");
	ZeroMemory(g_previousButtons, sizeof(g_previousButtons));
	g_armed = TRUE;
	DWORD patched = PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 401, (DWORD)XamInputGetStateHook);
	Sunrise_Dbg("[capture] XamInputGetState import %s (%u)", patched == S_OK ? "redirected" : "not found", patched);

	Sunrise_Dbg("[capture] ready: hold L3 + R3 + Y and tap LB to start/stop");
}

BOOL DestinyCaptureNewExecutable()
{
	if (!g_lockReady)
		return FALSE;
	BOOL wasArmed = g_armed;
	// Stop the overlay first: the swap interrupt is still hooked and its cached
	// front buffers belong to the image that is about to go.
	OverlayShutdown();
	// Close any running capture. The hooks go away with the outgoing image, so
	// they are only marked as gone.
	StopCapture("new executable");
	for (DWORD i = 0; i < ARRAYSIZE(g_hooks); i++)
		g_hooks[i].installed = FALSE;
	g_armed = FALSE;
	g_overlayArmed = FALSE;
	g_overlayFrames = 0;
	return wasArmed;
}

VOID DestinyCaptureShutdown()
{
	if (!g_lockReady)
		return;
	OverlayShutdown();
	StopCapture("plugin unload");
	// Unlike a new executable, the title keeps running here, so the prologues
	// have to go back: a branch left pointing into the unloaded plugin kills the
	// game on the next call. A thread inside a hook at this moment is a risk we
	// cannot avoid, and it is the smaller one.
	RemoveHooks();
	g_armed = FALSE;
	g_overlayArmed = FALSE;
	g_overlayFrames = 0;
}
