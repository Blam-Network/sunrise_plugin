#include "stdafx.h"
#include "Sunrise3.h"
#include "Utilities.h"
#include "Detour.h"
#include <ppcintrinsics.h>

XTITLE_SERVER_INFO activeServer;

int NetDll_socketHook(XNCALLER_TYPE n, int af, int type, int protocol)
{
	int s = NetDll_socket(n, af, type, protocol);
	if (n == 1 && protocol == 6) {
		BOOL b = TRUE;
		NetDll_setsockopt(n, s, SOL_SOCKET, 0x5801, (char*)&b, sizeof(BOOL));
	}

	return s;
}

int NetDll_XNetStartupHook(XNCALLER_TYPE xnc, XNetStartupParams* xnsp)
{
	// For devkits or modded boxes with devkit software.
	xnsp->cfgFlags |= XNET_STARTUP_BYPASS_SECURITY;
	return NetDll_XNetStartup(xnc, xnsp);
}

int NetDll_XNetUnregisterInAddrHook(XNCALLER_TYPE xnc, IN_ADDR address) {
	if (address.S_un.S_addr == activeServer.inaServer.S_un.S_addr
	) {
		return 0;
	}

	return NetDll_XNetUnregisterInAddr(xnc, address);
}

int NetDll_XNetServerToInAddrHook(XNCALLER_TYPE n, IN_ADDR address_in, DWORD title_id, IN_ADDR* address_out) {
	if (n == 1 && address_in.S_un.S_addr == activeServer.inaServer.S_un.S_addr
		) {
		// Skip the actual bollocks, assume sockpatch is enabled and just copy the IP.
		// If the game manually checks for a secure 0. address, this will die.
		// We'll solve that if it happens.
		address_out->S_un.S_addr = address_in.S_un.S_addr;
		return 0;
	}

	return NetDll_XNetServerToInAddr(n, address_in, title_id, address_out);
}

HANDLE lsp_enum_handle;
int lsp_enumeration_index;

int XamCreateEnumeratorHandleHook(DWORD user_index, HXAMAPP app_id, DWORD open_message, DWORD close_message, DWORD extra_size, DWORD item_count, DWORD flags, PHANDLE out_handle)
{
	int result = XamCreateEnumeratorHandle(user_index, app_id, open_message, close_message, extra_size, item_count, flags, out_handle);

	if (open_message == 0x58039) {
		lsp_enum_handle = *out_handle;
		lsp_enumeration_index = 0;
	}

	return result;

}

DWORD title_id_real = 0;
VOID SetTitleId(DWORD title_id) {
	Sunrise_Dbg("SetTitleId(%08X)", title_id)
	title_id_real = title_id;
}

DWORD XamContentCreateEnumeratorHook(
	DWORD dwUserIndex,
	XCONTENTDEVICEID DeviceID,
	DWORD dwContentType,
	DWORD dwContentFlags,
	DWORD cItem,
	PDWORD pcbBuffer,
	PHANDLE phEnum
) {
	DWORD current_title_id = XamGetCurrentTitleId();
	Sunrise_Dbg("XamContentCreateEnumeratorHook for content type %d", dwContentType);
	Sunrise_Dbg("title_id_real = %08X", title_id_real);
	Sunrise_Dbg("current_title_id = %08X", current_title_id);

	// If we're title spoofing, dont try to load DLC.
	if (current_title_id != title_id_real && dwContentType == XCONTENTTYPE_MARKETPLACE) {
		Sunrise_Dbg("Title spoofing - Returning savegame instead of DLC");
		dwContentType = XCONTENTTYPE_PUBLISHER;
	}
	else if (dwContentType == XCONTENTTYPE_MARKETPLACE) {
		Sunrise_Dbg("Not spoofing so returning normal DLC.");
	}

	return XContentCreateEnumerator(
		dwUserIndex,
		DeviceID,
		dwContentType,
		dwContentFlags,
		cItem,
		pcbBuffer,
		phEnum
	);
}

DWORD XamUserReadProfileSettingsHook(
	DWORD dwTitleId,
	DWORD dwUserIndexRequester,
	DWORD dwNumFor,
	const PXUID pxuidFor,
	DWORD dwNumSettingIds,
	const PDWORD pdwSettingIds,
	PDWORD pcbResults,
	PXUSER_READ_PROFILE_SETTING_RESULT pResults, // in xonline.h
	PXOVERLAPPED pXOverlapped OPTIONAL
) {
	DWORD current_title_id = XamGetCurrentTitleId();
	//Sunrise_Dbg("XamUserReadProfileSettingsHook with title ID %08X", dwTitleId);
	//Sunrise_Dbg("title_id_real = %08X", title_id_real);
	//Sunrise_Dbg("hook read spoofed=%08X addr=%p thread=%08X", title_id_real, (void*)&title_id_real, GetCurrentThreadId());

	// If we're title spoofing, read settings from the original title id.
	if (dwTitleId == 0 && title_id_real != current_title_id) {
		Sunrise_Dbg("Spoofing profile read, using title ID %08X", title_id_real);
		dwTitleId = title_id_real;
	}

	return XamUserReadProfileSettings(dwTitleId, dwUserIndexRequester, dwNumFor, pxuidFor, dwNumSettingIds, pdwSettingIds, pcbResults, pResults, pXOverlapped);
}

DWORD XamUserWriteProfileSettingsHook(
	DWORD dwTitleId,
	DWORD dwUserIndex,
	DWORD dwNumSettings,
	const PXUSER_PROFILE_SETTING pSettings,
	PXOVERLAPPED pXOverlapped
) {
	DWORD current_title_id = XamGetCurrentTitleId();
	//Sunrise_Dbg("XamUserWriteProfileSettingsHook with title ID %08X", dwTitleId);
	//Sunrise_Dbg("title_id_real = %08X", title_id_real);
	//Sunrise_Dbg("hook read spoofed=%08X addr=%p thread=%08X", title_id_real, (void*)&title_id_real, GetCurrentThreadId());

	// If we're title spoofing, dont overwrite settings.
	if (dwTitleId == 0 && title_id_real != current_title_id) {
		Sunrise_Dbg("Spoofing profile write, using title ID %08X", title_id_real);
		dwTitleId = title_id_real;
	}

	return XamUserWriteProfileSettings(dwTitleId, dwUserIndex, dwNumSettings, pSettings, pXOverlapped);
}


struct halo_log_event
{
	int level;
	long category;
	int flags;
};


void RegisterActiveServer(in_addr address, const char description[XTITLE_SERVER_MAX_SERVER_INFO_LEN]) {
	activeServer.inaServer.S_un.S_addr = address.S_un.S_addr;
	memcpy(activeServer.szServerInfo, description, XTITLE_SERVER_MAX_SERVER_INFO_LEN);
}

bool performed_dns_lookup = false;
void RegisterActiveServerDomain(char* domain, const char description[XTITLE_SERVER_MAX_SERVER_INFO_LEN]) {
	WSAEVENT event;
	static struct in_addr addr;
	static char* addr_ptr = NULL;
	XNDNS* dns = NULL;

	addr_ptr = (char*)&addr;

	if (!domain) goto error;

	Sunrise_Dbg("Registering active server %s", domain);

	event = WSACreateEvent();
	XNetDnsLookup(domain, event, &dns);
	if (!dns) goto error;

	WaitForSingleObject((HANDLE)event, INFINITE);
	if (dns->iStatus) goto error;

	memcpy(&addr, dns->aina, sizeof(addr));

	WSACloseEvent(event);
	XNetDnsRelease(dns);

	RegisterActiveServer(addr, description);
	performed_dns_lookup = true;
	return;

error:
	XNotify(L"Failed to register Title Server!");
}

int XamEnumerateHook(
	HANDLE hEnum,
	DWORD dwFlags,
	PDWORD pvBuffer,
	DWORD cbBuffer,
	PDWORD pcItemsReturned,
	PXOVERLAPPED pOverlapped
)
{
	if (
		hEnum == lsp_enum_handle
	) {
		if (!performed_dns_lookup) {
			RegisterHaloServer();
		}

		if (cbBuffer < sizeof(XTITLE_SERVER_INFO)) {
			return ERROR_INSUFFICIENT_BUFFER;
		}

		memcpy(pvBuffer, &activeServer, sizeof(XTITLE_SERVER_INFO));

		int errorCode = lsp_enumeration_index == 0 ? 0 : ERROR_NO_MORE_FILES;

		lsp_enumeration_index = 1;

		if (pOverlapped) {
			pOverlapped->InternalLow = errorCode;
			pOverlapped->InternalHigh = 1;
			pOverlapped->InternalContext = (ULONG_PTR)GetCurrentThread();
			pOverlapped->dwExtendedError = 0;

			if (pOverlapped->hEvent) {
				ResetEvent(pOverlapped->hEvent);
			}


			if (pOverlapped->hEvent) {
				SetEvent(pOverlapped->hEvent);
			}

			return ERROR_IO_PENDING;
		}

		return errorCode;
	}

	return XamEnumerate(hEnum, dwFlags, pvBuffer, cbBuffer, pcItemsReturned, pOverlapped);
}

VOID SetupLoadHooks(PLDR_DATA_TABLE_ENTRY moduleHandle);

NTSTATUS XexLoadExecutableHook(PCHAR Name, PHANDLE Handle, DWORD TypeFlags, DWORD Version) {
	Sunrise_Print("XexLoadExecutableHook with name %s handle %d", Name, *(DWORD*)Handle);
	HANDLE Module = 0;
	NTSTATUS Result = XexLoadExecutable(Name, &Module, TypeFlags, Version);
	Sunrise_Print("XexLoadExecutable called got handle %d", Name, *(DWORD*)Handle);
	if (Handle != 0) *Handle = Module;
	if (NT_SUCCESS(Result)) SetupHaloPatches();
	SetupLoadHooks((PLDR_DATA_TABLE_ENTRY)Module);
	return Result;
}

NTSTATUS XexLoadImageHook(CONST PCHAR Name, DWORD TypeFlags, DWORD Version, PHANDLE Handle) {
	Sunrise_Print("XexLoadExecutableHook with name %s handle %d", Name, *(DWORD*)Handle);

	HANDLE Module = 0;
	NTSTATUS Result = XexLoadImage(Name, TypeFlags, Version, &Module);
	Sunrise_Print("XexLoadImage called got handle %d", Name, *(DWORD*)Handle);
	if (Handle != 0) *Handle = Module;
	if (NT_SUCCESS(Result)) SetupHaloPatches();
	SetupLoadHooks((PLDR_DATA_TABLE_ENTRY)Module);
	return Result;
}

VOID SetupLSPHooks()
{
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 3, (DWORD)NetDll_socketHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 51, (DWORD)NetDll_XNetStartupHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 58, (DWORD)NetDll_XNetServerToInAddrHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 63, (DWORD)NetDll_XNetUnregisterInAddrHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 590, (DWORD)XamCreateEnumeratorHandleHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 592, (DWORD)XamEnumerateHook);
}

VOID SetupSpoofHooks() {
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 604, (DWORD)XamContentCreateEnumeratorHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 537, (DWORD)XamUserReadProfileSettingsHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 538, (DWORD)XamUserWriteProfileSettingsHook);
}

VOID SetupLoadHooks(PLDR_DATA_TABLE_ENTRY moduleHandle)
{
	if (moduleHandle == nullptr) {
		PatchModuleImport(MODULE_XAM, MODULE_KERNEL, 0x198, (DWORD)XexLoadExecutableHook);
		PatchModuleImport(MODULE_XAM, MODULE_KERNEL, 0x199, (DWORD)XexLoadImageHook);
	}
	else {
		PatchModuleImport(moduleHandle, MODULE_KERNEL, 0x198, (DWORD)XexLoadExecutableHook);
		PatchModuleImport(moduleHandle, MODULE_KERNEL, 0x199, (DWORD)XexLoadImageHook);
	}
}

// --- Leaderboard (TrueSkill) stats reads ---------------------------------------
//
// XUserReadStats is XDK library code linked into every title (sub_826E77E8 in
// Halo 3 Epsilon). It packs its arguments into a 0x1C-byte buffer and passes
// them to xam with XMsgStartIORequest(0xFB, 0xB0021, ...). Hooking that xam
// import catches the stats read in every build with no per-build address.
//
// When the leaderboard service is down the read fails ("networking:leaderboards:
// stats query failed", ERROR_FUNCTION_FAILED / 0x80159000) and matchmaking stops.
// The hook runs the real read first. If it fails, the title gets a successful
// read with no stats views in place of the error.

static const HXAMAPP StatsReadApp = (HXAMAPP)0xFB;
static const DWORD StatsReadMessage = 0xB0021;

// The XUserReadStats arguments, in the order XUserReadStats packs them.
struct StatsReadRequest
{
	DWORD titleId;
	DWORD numXuids;
	const XUID* xuids;
	DWORD numStatsSpecs;
	const VOID* statsSpecs;
	DWORD cbResults;
	PXUSER_STATS_READ_RESULTS results;
};

// One asynchronous read. The real request runs on a worker thread with the
// inner overlapped. The title's overlapped (outer) completes when the worker
// has the result.
struct StatsReadCall
{
	XOVERLAPPED inner;
	PXOVERLAPPED outer;
	StatsReadRequest request;
	LONG refs;
	StatsReadCall* next;
};

// Reads the title has started and not yet seen complete or cancelled.
static CRITICAL_SECTION statsReadLock;
static StatsReadCall* statsReadCalls = NULL;

static VOID StatsReadLink(StatsReadCall* call)
{
	call->next = statsReadCalls;
	statsReadCalls = call;
}

// Caller holds statsReadLock. Returns FALSE when the call is no longer listed.
static BOOL StatsReadUnlink(StatsReadCall* call)
{
	for (StatsReadCall** link = &statsReadCalls; *link; link = &(*link)->next) {
		if (*link == call) {
			*link = call->next;
			return TRUE;
		}
	}
	return FALSE;
}

static VOID StatsReadRelease(StatsReadCall* call)
{
	if (InterlockedDecrement(&call->refs) == 0) {
		if (call->inner.hEvent)
			CloseHandle(call->inner.hEvent);
		free(call);
	}
}

// The "no stats" answer: a read that found no views.
static VOID StatsReadWriteEmpty(PXUSER_STATS_READ_RESULTS results)
{
	if (results) {
		results->dwNumViews = 0;
		results->pViews = NULL;
	}
}

// Complete an overlapped the way xam does for a title without a completion
// routine: result fields first, status last, then the event.
static VOID StatsReadComplete(PXOVERLAPPED overlapped, DWORD status, DWORD extendedError)
{
	overlapped->InternalHigh = 0;
	overlapped->dwExtendedError = extendedError;
	__lwsync();
	overlapped->InternalLow = status;
	if (overlapped->hEvent)
		SetEvent(overlapped->hEvent);
}

static DWORD WINAPI StatsReadWorker(LPVOID param)
{
	StatsReadCall* call = (StatsReadCall*)param;

	DWORD status;
	DWORD extendedError = 0;
	HRESULT hr = XMsgStartIORequest(StatsReadApp, StatsReadMessage, &call->inner,
		&call->request, sizeof(call->request));
	if (SUCCEEDED(hr)) {
		DWORD bytes = 0;
		status = XGetOverlappedResult(&call->inner, &bytes, TRUE);
		if (status != ERROR_SUCCESS)
			extendedError = XGetOverlappedExtendedError(&call->inner);
	}
	else {
		status = ERROR_FUNCTION_FAILED;
		extendedError = hr;
	}

	EnterCriticalSection(&statsReadLock);
	// Not listed: the title cancelled the read, and the cancel hook completed it.
	if (StatsReadUnlink(call)) {
		if (status != ERROR_SUCCESS) {
			Sunrise_Dbg("Stats read failed (status %u, extended %08X), answering with no stats",
				status, extendedError);
			StatsReadWriteEmpty(call->request.results);
			status = ERROR_SUCCESS;
			extendedError = 0;
		}
		StatsReadComplete(call->outer, status, extendedError);
	}
	LeaveCriticalSection(&statsReadLock);

	StatsReadRelease(call);
	return 0;
}

HRESULT XMsgStartIORequestHook(HXAMAPP hxamapp, DWORD dwMessage, PXOVERLAPPED pOverlapped, PVOID pUserBuffer, DWORD cbUserBuffer)
{
	if (hxamapp != StatsReadApp || dwMessage != StatsReadMessage
		|| pUserBuffer == NULL || cbUserBuffer != sizeof(StatsReadRequest)) {
		return XMsgStartIORequest(hxamapp, dwMessage, pOverlapped, pUserBuffer, cbUserBuffer);
	}

	StatsReadRequest* request = (StatsReadRequest*)pUserBuffer;

	// Synchronous read. XUserReadStats reports failure through the thread's
	// last error after a successful XMsgStartIORequest.
	if (pOverlapped == NULL) {
		HRESULT hr = XMsgStartIORequest(hxamapp, dwMessage, NULL, pUserBuffer, cbUserBuffer);
		if (SUCCEEDED(hr) && GetLastError() == ERROR_SUCCESS)
			return hr;

		Sunrise_Dbg("Stats read failed (hr %08X, error %u), answering with no stats", hr, GetLastError());
		StatsReadWriteEmpty(request->results);
		SetLastError(ERROR_SUCCESS);
		return S_OK;
	}

	// A completion routine would need an APC queued to the title's thread.
	// Leave those reads alone.
	if (pOverlapped->pCompletionRoutine != NULL)
		return XMsgStartIORequest(hxamapp, dwMessage, pOverlapped, pUserBuffer, cbUserBuffer);

	StatsReadCall* call = (StatsReadCall*)malloc(sizeof(StatsReadCall));
	if (call == NULL)
		return XMsgStartIORequest(hxamapp, dwMessage, pOverlapped, pUserBuffer, cbUserBuffer);

	memset(call, 0, sizeof(StatsReadCall));
	call->inner.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	call->outer = pOverlapped;
	call->request = *request; // The title's buffer is on its stack.
	call->refs = 1;           // Owned by the worker.

	if (call->inner.hEvent == NULL) {
		free(call);
		return XMsgStartIORequest(hxamapp, dwMessage, pOverlapped, pUserBuffer, cbUserBuffer);
	}

	// Mark the title's overlapped pending, as xam does when it accepts a request.
	pOverlapped->InternalLow = ERROR_IO_PENDING;
	pOverlapped->InternalHigh = 0;
	pOverlapped->dwExtendedError = 0;
	if (pOverlapped->hEvent)
		ResetEvent(pOverlapped->hEvent);

	EnterCriticalSection(&statsReadLock);
	StatsReadLink(call);
	LeaveCriticalSection(&statsReadLock);

	HANDLE thread = NULL;
	DWORD threadId = 0;
	ExCreateThread(&thread, 0, &threadId, (PVOID)XapiThreadStartup,
		(LPTHREAD_START_ROUTINE)StatsReadWorker, call, 0x2 | CREATE_SUSPENDED);
	if (thread == NULL) {
		EnterCriticalSection(&statsReadLock);
		StatsReadUnlink(call);
		LeaveCriticalSection(&statsReadLock);
		StatsReadRelease(call);
		return XMsgStartIORequest(hxamapp, dwMessage, pOverlapped, pUserBuffer, cbUserBuffer);
	}

	XSetThreadProcessor(thread, 4);
	ResumeThread(thread);
	CloseHandle(thread);
	return S_OK;
}

HRESULT XMsgCancelIORequestHook(PXOVERLAPPED pOverlapped, BOOL fWait)
{
	StatsReadCall* call = NULL;

	EnterCriticalSection(&statsReadLock);
	for (StatsReadCall* item = statsReadCalls; item; item = item->next) {
		if (item->outer == pOverlapped) {
			call = item;
			StatsReadUnlink(call);
			InterlockedIncrement(&call->refs);
			break;
		}
	}
	LeaveCriticalSection(&statsReadLock);

	// xam only knows about the inner overlapped of a stats read.
	if (call == NULL)
		return XMsgCancelIORequest(pOverlapped, fWait);

	HRESULT hr = XMsgCancelIORequest(&call->inner, fWait);
	StatsReadComplete(pOverlapped, ERROR_CANCELLED, 0);
	StatsReadRelease(call);
	return hr;
}

VOID SetupStatsReadHook()
{
	static BOOL lockReady = FALSE;
	if (!lockReady) {
		InitializeCriticalSection(&statsReadLock);
		lockReady = TRUE;
	}

	PLDR_DATA_TABLE_ENTRY title = (PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle;
	if (PatchModuleImport(title, MODULE_XAM, 503, (DWORD)XMsgStartIORequestHook) != S_OK)
		Sunrise_Dbg("Stats read hook: title does not import XMsgStartIORequest");
	PatchModuleImport(title, MODULE_XAM, 504, (DWORD)XMsgCancelIORequestHook);
}


// This hook ensures that the utility drive is formatted every mount.
// This reduces some issues when switching between halo caches.
Detour XMountUtilityDriveExDetour;
DWORD XMountUtilityDriveEx(DWORD dwFlags, DWORD dwBytesPerCluster, SIZE_T dwFileCacheSize)
{
	Sunrise_Dbg("XMountUtilityDriveEx Hook called, formatting Utility Drive.");
	return XMountUtilityDriveExDetour.GetOriginal<decltype(&XMountUtilityDriveEx)>()(
		0xF,
		dwBytesPerCluster,
		dwFileCacheSize
	);
}

VOID SetupXMountUtilityDriveExHook(DWORD functionAddress) {

	XMountUtilityDriveExDetour = Detour(
		reinterpret_cast<decltype(&XMountUtilityDriveEx)>(functionAddress),
		XMountUtilityDriveEx
	);
	XMountUtilityDriveExDetour.Install();
}

// Quick fix to always allow insecure sockets on devkit. Works for retail kernel too but this will do for now.
#define INSECURE_SOCK_PRIV    6
BOOL XexCheckExecutablePrivilegeHook(DWORD priv)
{
	// Allow insecure sockets for all titles
	if (priv == INSECURE_SOCK_PRIV)
		return TRUE;

	return XexCheckExecutablePrivilege(priv);
}

VOID ApplyPrivHook()
{
	if (!bEnableDevkitSockpatch)
		return;

	if (!bIsDevkit)
		return;

	// This will break if a dash update is released.
	if (*(QWORD*)0x81D0F3CC == 0x3d60800a396bff90) // Quick check to make sure it hasn't already been hooked. Some stealths already do this
	{
		Sunrise_Dbg("Applying executable priv hook!");
		if (PatchModuleImport("xam.xex", "xboxkrnl.exe", 404, (DWORD)XexCheckExecutablePrivilegeHook) != S_OK)
			Sunrise_Dbg("Failed to apply executable priv hook! (insecure sockets)");
	}
}

VOID RemovePrivHook()
{
	if (!bIsDevkit)
		return;

	Sunrise_Dbg("Removing executable priv hook!");
	// This will break if a dash update is released.
	*(QWORD*)0x81D0F3CC = 0x3d60800a396bff90;
	*(QWORD*)(0x81D0F3CC + 8) = 0x7d6903a64e800420;
}