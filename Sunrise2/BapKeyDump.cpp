#include "stdafx.h"
#include "BapKeyDump.h"
#include "Detour.h"
#include "PacketCapture.h"
#include "Utilities.h"

#define PATH_BAP_KEYS MOUNT_POINT "\\destiny_bap_keys.txt"

// Destiny retail ttk_231 / tiger_release_final — Jul 27 2016 (LDR TimeDateStamp).
#define TTK231_TS         0x579810CC
#define TTK231_GCM_INIT   0x833C8EC8
#define TTK231_GCM_ADD_IV 0x833C8530

// Same pattern as PacketCapture: hooks only enqueue; a plugin thread owns WriteFile.
// Direct WriteFile from title crypto threads + rare Flush left the USB file empty
// (header only) while console still printed keys.
#define KEYS_RING_SIZE    (256 * 1024)
#define KEYS_FLUSH_CHUNK  4096

static Detour g_gcmInitDetour;
static Detour g_gcmAddIvDetour;
static HANDLE g_keysFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_keysCs;
static BOOL g_keysCsInit = FALSE;
static BOOL g_active = FALSE;
static volatile BOOL g_flushRunning = FALSE;
static DWORD g_dumpStartTick = 0;
static DWORD g_linesQueued = 0;
static DWORD g_linesFlushed = 0;
static DWORD g_dropLines = 0;

static BYTE g_ring[KEYS_RING_SIZE];
static DWORD g_ringWrite = 0;
static DWORD g_ringRead = 0;
static DWORD g_ringUsed = 0;

static VOID EnsureKeysCs()
{
	if (!g_keysCsInit) {
		InitializeCriticalSection(&g_keysCs);
		g_keysCsInit = TRUE;
	}
}

static DWORD CapMs()
{
	DWORD cap = GetPacketCaptureElapsedMs();
	if (cap != 0 || IsPacketCaptureActive())
		return cap;
	return GetTickCount() - g_dumpStartTick;
}

static BOOL RingWrite(const VOID* data, DWORD size)
{
	if (size == 0)
		return TRUE;
	if (g_ringUsed + size > KEYS_RING_SIZE)
		return FALSE;

	const BYTE* src = (const BYTE*)data;
	DWORD first = KEYS_RING_SIZE - g_ringWrite;
	if (first > size)
		first = size;

	memcpy(g_ring + g_ringWrite, src, first);
	if (size > first)
		memcpy(g_ring, src + first, size - first);

	g_ringWrite = (g_ringWrite + size) % KEYS_RING_SIZE;
	g_ringUsed += size;
	return TRUE;
}

static DWORD RingRead(BYTE* out, DWORD max)
{
	if (g_ringUsed == 0 || max == 0)
		return 0;

	DWORD n = g_ringUsed;
	if (n > max)
		n = max;

	DWORD first = KEYS_RING_SIZE - g_ringRead;
	if (first > n)
		first = n;

	memcpy(out, g_ring + g_ringRead, first);
	if (n > first)
		memcpy(out + first, g_ring, n - first);

	g_ringRead = (g_ringRead + n) % KEYS_RING_SIZE;
	g_ringUsed -= n;
	return n;
}

static VOID HexAppend(CHAR* out, DWORD outCap, DWORD* used, const BYTE* data, DWORD len)
{
	static const CHAR* hex = "0123456789abcdef";
	for (DWORD i = 0; i < len && *used + 2 < outCap; i++) {
		out[(*used)++] = hex[data[i] >> 4];
		out[(*used)++] = hex[data[i] & 0xF];
	}
	if (*used < outCap)
		out[*used] = 0;
}

static VOID QueueKeyLine(const CHAR* kind, DWORD gcmPtr, const BYTE* bytes, DWORD len, BOOL logConsole)
{
	if (!g_active || !bytes || !len)
		return;

	CHAR line[512];
	CHAR hex[128];
	DWORD hexUsed = 0;
	if (len > 48)
		len = 48;
	HexAppend(hex, sizeof(hex), &hexUsed, bytes, len);

	int n = sprintf_s(
		line,
		sizeof(line),
		"{\"t\":%u,\"kind\":\"%s\",\"gcm\":\"0x%08X\",\"%s\":\"%s\",\"len\":%u}\r\n",
		CapMs(),
		kind,
		gcmPtr,
		(strcmp(kind, "gcm_add_iv") == 0) ? "iv" : "key",
		hex,
		len
	);
	if (n <= 0)
		return;

	EnterCriticalSection(&g_keysCs);
	if (!RingWrite(line, (DWORD)n))
		g_dropLines++;
	else
		g_linesQueued++;
	LeaveCriticalSection(&g_keysCs);

	if (logConsole) {
		Sunrise_Dbg("BAP %s gcm=0x%08X %s=%s (queued %u flushed %u drop %u)",
			kind, gcmPtr,
			(strcmp(kind, "gcm_add_iv") == 0) ? "iv" : "key", hex,
			g_linesQueued, g_linesFlushed, g_dropLines);
	}
}

static DWORD WINAPI KeysFlushThread(LPVOID)
{
	BYTE chunk[KEYS_FLUSH_CHUNK];
	DWORD lastFlushTick = GetTickCount();
	BOOL dirty = FALSE;

	while (g_flushRunning) {
		EnterCriticalSection(&g_keysCs);
		DWORD n = RingRead(chunk, sizeof(chunk));
		LeaveCriticalSection(&g_keysCs);

		if (n && g_keysFile != INVALID_HANDLE_VALUE) {
			DWORD written = 0;
			if (WriteFile(g_keysFile, chunk, n, &written, NULL) && written == n) {
				dirty = TRUE;
				for (DWORD i = 0; i < n; i++) {
					if (chunk[i] == '\n')
						g_linesFlushed++;
				}
			} else {
				Sunrise_Dbg("BAP key dump: WriteFile failed err=%u n=%u", GetLastError(), n);
			}
		} else {
			Sleep(50);
		}

		// USB mass-storage only shows new bytes to the PC after a flush.
		// Throttle so IV spam doesn't stall the stick every ~100B write.
		if (dirty && (GetTickCount() - lastFlushTick) >= 250) {
			FlushFileBuffers(g_keysFile);
			lastFlushTick = GetTickCount();
			dirty = FALSE;
		}
	}

	return 0;
}

static VOID DrainKeysRingToFile()
{
	BYTE chunk[KEYS_FLUSH_CHUNK];
	for (;;) {
		EnterCriticalSection(&g_keysCs);
		DWORD n = RingRead(chunk, sizeof(chunk));
		LeaveCriticalSection(&g_keysCs);
		if (!n)
			break;
		if (g_keysFile != INVALID_HANDLE_VALUE) {
			DWORD written = 0;
			WriteFile(g_keysFile, chunk, n, &written, NULL);
			FlushFileBuffers(g_keysFile);
			for (DWORD i = 0; i < n; i++) {
				if (chunk[i] == '\n')
					g_linesFlushed++;
			}
		}
	}
}

// libtomcrypt: int gcm_init(gcm_state *gcm, int cipher, const unsigned char *key, int keylen);
static int GcmInitHook(DWORD gcm, int cipher, const BYTE* key, int keylen)
{
	DWORD dumpLen = 16;
	if (keylen > 0 && keylen <= 32)
		dumpLen = (DWORD)keylen;
	if (key)
		QueueKeyLine("gcm_init", gcm, key, dumpLen, TRUE);

	return g_gcmInitDetour.GetOriginal<decltype(&GcmInitHook)>()(gcm, cipher, key, keylen);
}

// libtomcrypt: int gcm_add_iv(gcm_state *gcm, const unsigned char *IV, unsigned long IVlen);
static int GcmAddIvHook(DWORD gcm, const BYTE* iv, unsigned int ivlen)
{
	if (iv && ivlen > 0)
		QueueKeyLine("gcm_add_iv", gcm, iv, ivlen, FALSE);

	return g_gcmAddIvDetour.GetOriginal<decltype(&GcmAddIvHook)>()(gcm, iv, ivlen);
}

static DWORD ReadInsn(DWORD addr)
{
	return *(volatile DWORD*)addr;
}

BOOL IsBapKeyDumpActive()
{
	return g_active;
}

VOID StopBapKeyDump()
{
	if (!g_active && g_keysFile == INVALID_HANDLE_VALUE)
		return;

	Sunrise_Dbg("Stopping BAP key dump (queued %u flushed %u drop %u)",
		g_linesQueued, g_linesFlushed, g_dropLines);
	g_active = FALSE;

	g_gcmInitDetour.Remove();
	g_gcmAddIvDetour.Remove();

	g_flushRunning = FALSE;
	Sleep(150);
	DrainKeysRingToFile();

	if (g_keysFile != INVALID_HANDLE_VALUE) {
		FlushFileBuffers(g_keysFile);
		CloseHandle(g_keysFile);
		g_keysFile = INVALID_HANDLE_VALUE;
	}
}

VOID StartBapKeyDump()
{
	EnsureKeysCs();

	if (g_active)
		StopBapKeyDump();

	PLDR_DATA_TABLE_ENTRY mod = (PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle;
	if (!mod || !mod->ImageBase || !mod->SizeOfFullImage) {
		Sunrise_Dbg("BAP key dump: no title module");
		return;
	}

	DWORD gcmInit = 0;
	DWORD gcmAddIv = 0;

	if (mod->TimeDateStamp == TTK231_TS) {
		gcmInit = TTK231_GCM_INIT;
		gcmAddIv = TTK231_GCM_ADD_IV;
		Sunrise_Dbg("BAP key dump: ttk_231 (Jul 2016) addrs");
	} else {
		Sunrise_Dbg("BAP key dump: unsupported ts=0x%08X (need ttk_231 0x579810CC)",
			mod->TimeDateStamp);
		return;
	}

	if (ReadInsn(gcmInit) != 0x7D8802A6 || ReadInsn(gcmAddIv) != 0x7D8802A6) {
		Sunrise_Dbg("BAP key dump: bad prologue (init=0x%08X add_iv=0x%08X)",
			gcmInit, gcmAddIv);
		return;
	}

	g_keysFile = CreateFile(
		PATH_BAP_KEYS,
		GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
		NULL
	);
	if (g_keysFile == INVALID_HANDLE_VALUE) {
		Sunrise_Dbg("BAP key dump: failed to create %s err=%u", PATH_BAP_KEYS, GetLastError());
		return;
	}

	EnterCriticalSection(&g_keysCs);
	g_ringWrite = 0;
	g_ringRead = 0;
	g_ringUsed = 0;
	g_linesQueued = 0;
	g_linesFlushed = 0;
	g_dropLines = 0;
	LeaveCriticalSection(&g_keysCs);

	g_dumpStartTick = GetTickCount();
	{
		CHAR hdr[256];
		int n = sprintf_s(
			hdr,
			sizeof(hdr),
			"# destiny_bap_keys v2 title_ts=0x%08X base=0x%08X size=0x%08X\r\n"
			"# ring+flush; if you still see correlate/v1 header you deployed the wrong xex\r\n",
			mod->TimeDateStamp,
			(DWORD)mod->ImageBase,
			mod->SizeOfFullImage
		);
		DWORD written = 0;
		if (n > 0) {
			WriteFile(g_keysFile, hdr, (DWORD)n, &written, NULL);
			FlushFileBuffers(g_keysFile);
		}
	}

	g_flushRunning = TRUE;
	g_active = TRUE;
	ThreadMe(KeysFlushThread);

	g_gcmInitDetour = Detour((void*)gcmInit, (void*)&GcmInitHook);
	g_gcmAddIvDetour = Detour((void*)gcmAddIv, (void*)&GcmAddIvHook);
	if (!g_gcmInitDetour.Install() || !g_gcmAddIvDetour.Install()) {
		Sunrise_Dbg("BAP key dump: Detour.Install failed");
		StopBapKeyDump();
		return;
	}

	Sunrise_Dbg("BAP key dump active -> %s (gcm_init=0x%08X gcm_add_iv=0x%08X)",
		PATH_BAP_KEYS, gcmInit, gcmAddIv);
	XNotify(L"BAP key dump enabled");
}
