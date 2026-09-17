#include "stdafx.h"
#include "PacketCapture.h"
#include "Utilities.h"

#define CAP_RING_SIZE		(2 * 1024 * 1024)
#define CAP_FLUSH_CHUNK		8192
#define PATH_NETCAP			MOUNT_POINT "\\destiny_netcap.dat"

typedef int (NTAPI *NetDll_XnpEthernetInterceptSetCallbacks_t)(
	XNCALLER_TYPE xnc,
	LP_INTERCEPT_XMIT_FUNC pfnEnetInterceptXmitCallback,
	LP_INTERCEPT_RECV_FUNC pfnEnetInterceptRecvCallback,
	PVOID pvCallbackUserData,
	DWORD dwFlags
);

static NetDll_XnpEthernetInterceptSetCallbacks_t g_setCbs = NULL;
static HANDLE g_capFile = INVALID_HANDLE_VALUE;
static DWORD g_capStartTick = 0;
static volatile BOOL g_capturing = FALSE;
static volatile BOOL g_flushRunning = FALSE;
static CRITICAL_SECTION g_cs;
static BOOL g_csInit = FALSE;

static BYTE g_ring[CAP_RING_SIZE];
static DWORD g_ringWrite = 0;
static DWORD g_ringRead = 0;
static DWORD g_ringUsed = 0;
static DWORD g_dropped = 0;

static VOID EnsureCs()
{
	if (!g_csInit) {
		InitializeCriticalSection(&g_cs);
		g_csInit = TRUE;
	}
}

static BOOL RingWrite(const VOID* data, DWORD size)
{
	if (size == 0)
		return TRUE;
	if (g_ringUsed + size > CAP_RING_SIZE)
		return FALSE;

	const BYTE* src = (const BYTE*)data;
	DWORD first = CAP_RING_SIZE - g_ringWrite;
	if (first > size)
		first = size;

	memcpy(g_ring + g_ringWrite, src, first);
	if (size > first)
		memcpy(g_ring, src + first, size - first);

	g_ringWrite = (g_ringWrite + size) % CAP_RING_SIZE;
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

	DWORD first = CAP_RING_SIZE - g_ringRead;
	if (first > n)
		first = n;

	memcpy(out, g_ring + g_ringRead, first);
	if (n > first)
		memcpy(out + first, g_ring, n - first);

	g_ringRead = (g_ringRead + n) % CAP_RING_SIZE;
	g_ringUsed -= n;
	return n;
}

static VOID WriteCapRecord(const BYTE* frame, DWORD cb)
{
	// NetCap record: DWORD length (includes 8-byte header), DWORD ms timestamp, then frame
	DWORD hdr[2];
	hdr[0] = cb + 8;
	hdr[1] = GetTickCount() - g_capStartTick;

	EnterCriticalSection(&g_cs);
	if (g_ringUsed + sizeof(hdr) + cb > CAP_RING_SIZE) {
		g_dropped++;
	}
	else {
		RingWrite(hdr, sizeof(hdr));
		RingWrite(frame, cb);
	}
	LeaveCriticalSection(&g_cs);
}

static int CapXmit(PVOID, const BYTE* data, DWORD cb)
{
	if (g_capturing && data && cb)
		WriteCapRecord(data, cb);
	return 0;
}

static int CapRecv(PVOID, const BYTE* data, DWORD cb)
{
	if (g_capturing && data && cb)
		WriteCapRecord(data, cb);
	return 0;
}

static DWORD WINAPI FlushThread(LPVOID)
{
	BYTE chunk[CAP_FLUSH_CHUNK];

	while (g_flushRunning) {
		EnterCriticalSection(&g_cs);
		DWORD n = RingRead(chunk, sizeof(chunk));
		LeaveCriticalSection(&g_cs);

		if (n && g_capFile != INVALID_HANDLE_VALUE) {
			DWORD written = 0;
			WriteFile(g_capFile, chunk, n, &written, NULL);
		}
		else {
			Sleep(50);
		}
	}

	return 0;
}

static VOID DrainRingToFile()
{
	BYTE chunk[CAP_FLUSH_CHUNK];
	for (;;) {
		EnterCriticalSection(&g_cs);
		DWORD n = RingRead(chunk, sizeof(chunk));
		LeaveCriticalSection(&g_cs);
		if (!n)
			break;
		if (g_capFile != INVALID_HANDLE_VALUE) {
			DWORD written = 0;
			WriteFile(g_capFile, chunk, n, &written, NULL);
		}
	}
}

BOOL IsPacketCaptureActive()
{
	return g_capturing;
}

VOID StopPacketCapture()
{
	if (!g_capturing && g_capFile == INVALID_HANDLE_VALUE)
		return;

	Sunrise_Dbg("Stopping packet capture (dropped %u records)", g_dropped);
	g_capturing = FALSE;

	if (g_setCbs)
		g_setCbs(XNCALLER_SYSAPP, NULL, NULL, NULL, 0);

	g_flushRunning = FALSE;
	Sleep(150); // let flush thread leave its loop
	DrainRingToFile();

	if (g_capFile != INVALID_HANDLE_VALUE) {
		CloseHandle(g_capFile);
		g_capFile = INVALID_HANDLE_VALUE;
	}

	XNotify(L"Destiny capture stopped");
}

VOID StartPacketCapture()
{
	EnsureCs();

	if (g_capturing)
		StopPacketCapture();

	if (!g_setCbs) {
		g_setCbs = (NetDll_XnpEthernetInterceptSetCallbacks_t)ResolveFunction(MODULE_XAM, 109);
		if (!g_setCbs) {
			Sunrise_Dbg("Failed to resolve NetDll_XnpEthernetInterceptSetCallbacks");
			return;
		}
	}

	g_capFile = CreateFile(
		PATH_NETCAP,
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	if (g_capFile == INVALID_HANDLE_VALUE) {
		Sunrise_Dbg("Failed to create %s", PATH_NETCAP);
		return;
	}

	EnterCriticalSection(&g_cs);
	g_ringWrite = 0;
	g_ringRead = 0;
	g_ringUsed = 0;
	g_dropped = 0;
	LeaveCriticalSection(&g_cs);

	g_capStartTick = GetTickCount();
	g_flushRunning = TRUE;
	g_capturing = TRUE;

	ThreadMe(FlushThread);

	int status = g_setCbs(XNCALLER_SYSAPP, CapXmit, CapRecv, NULL, 0);
	if (status != 0) {
		Sunrise_Dbg("XnpEthernetInterceptSetCallbacks failed: %d", status);
		StopPacketCapture();
		return;
	}

	Sunrise_Dbg("Packet capture started -> %s", PATH_NETCAP);
}
