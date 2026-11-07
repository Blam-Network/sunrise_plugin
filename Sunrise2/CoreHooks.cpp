#include "stdafx.h"
#include "Sunrise3.h"
#include "Utilities.h"
#include "Detour.h"
#include "DestinyCapture.h"

XTITLE_SERVER_INFO activeServer;
// Set when RegisterActiveServer succeeds. XHTTP does its own DNS; DW/LSP
// needs this IP via XamEnumerate — retry there if XNetStartup registration
// never ran or failed.
bool lsp_server_registered = false;

int NetDll_socketHook(XNCALLER_TYPE n, int af, int type, int protocol)
{
	int s = NetDll_socket(n, af, type, protocol);
	if (n == 1 && protocol == 6) {
		BOOL b = TRUE;
		NetDll_setsockopt(n, s, SOL_SOCKET, 0x5801, (char*)&b, sizeof(BOOL));
	}

	return s;
}

// The title tearing its network down is the last point where everything the
// capture needs is still alive: the image is mapped, threads are running and
// the file handle is good. Closing the capture here rather than at
// XexLoadExecutable keeps the flush and the hook removal off the teardown path.
int NetDll_XNetCleanupHook(XNCALLER_TYPE xnc)
{
	Sunrise_Dbg("XNetCleanup hook, closing any running capture");
	DestinyCaptureShutdown();
	return NetDll_XNetCleanup(xnc);
}

int NetDll_XNetStartupHook(XNCALLER_TYPE xnc, XNetStartupParams* xnsp)
{
	// For devkits or modded boxes with devkit software.
	xnsp->cfgFlags |= XNET_STARTUP_BYPASS_SECURITY;
	int result = NetDll_XNetStartup(xnc, xnsp);
	Sunrise_Dbg("XNetStartup hook result=%d",
		result);
	if (result == 0 && !lsp_server_registered) {
		RegisterBungieServer();
	}
	return result;
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

// Destiny TU0 finds installed DLC with XamContentAggregateCreateEnumerator
// (xam ordinal 0x279, marketplace content type), resolved at runtime into a
// cached pointer (sub_82CD8E90), so no import hook sees it. Pointing the
// cache at this hook before the title runs routes the DLC scan through here.

// Title id with no marketplace content on disk: the pre-alpha one.
const DWORD NoDlcTitleId = 0x41560907;

DWORD XamContentAggregateCreateEnumeratorHook(XUID xuid, XCONTENTDEVICEID device_id, DWORD content_type, DWORD title_id, PHANDLE enumerator)
{
	// The spoofed TU0 must not load marketplace content made for later title
	// updates. Enumerate a title that has none so the scan finds nothing.
	if (content_type == XCONTENTTYPE_MARKETPLACE) {
		Sunrise_Dbg("Version spoofing - hiding DLC from content scan (title %08X)", title_id);
		title_id = NoDlcTitleId;
	}

	return XamContentAggregateCreateEnumerator(xuid, device_id, content_type, title_id, enumerator);
}

VOID SetupContentAggregateHook(DWORD cached_pointer)
{
	*(DWORD*)cached_pointer = (DWORD)XamContentAggregateCreateEnumeratorHook;
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
	lsp_server_registered = (address.S_un.S_addr != 0);
}

void RegisterActiveServerDomain(char* domain, const char description[XTITLE_SERVER_MAX_SERVER_INFO_LEN]) {
	WSAEVENT event = NULL;
	static struct in_addr addr;
	XNDNS* dns = NULL;
	DWORD wait = 0;

	if (!domain || !domain[0]) {
		Sunrise_Dbg("RegisterActiveServerDomain: null/empty domain");
		goto error;
	}

	Sunrise_Dbg("RegisterActiveServerDomain: looking up %s", domain);

	// Fast path: dotted IP (skip XNet DNS).
	{
		DWORD ip = NetDll_inet_addr(domain);
		if (ip != INADDR_NONE && ip != 0) {
			addr.S_un.S_addr = ip;
			RegisterActiveServer(addr, description);
			Sunrise_Dbg("RegisterActiveServerDomain: %s is IP %u.%u.%u.%u",
				domain,
				addr.S_un.S_un_b.s_b1, addr.S_un.S_un_b.s_b2,
				addr.S_un.S_un_b.s_b3, addr.S_un.S_un_b.s_b4);
			return;
		}
	}

	event = WSACreateEvent();
	if (!event) {
		Sunrise_Dbg("RegisterActiveServerDomain: WSACreateEvent failed");
		goto error;
	}

	if (XNetDnsLookup(domain, event, &dns) != 0 || !dns) {
		Sunrise_Dbg("RegisterActiveServerDomain: XNetDnsLookup failed for %s", domain);
		goto error;
	}

	// Never block forever — console XNet DNS can stall on custom domains.
	wait = WaitForSingleObject((HANDLE)event, 10000);
	if (wait == WAIT_TIMEOUT) {
		Sunrise_Dbg("RegisterActiveServerDomain: DNS timeout (10s) for %s", domain);
		goto error;
	}
	if (wait != WAIT_OBJECT_0) {
		Sunrise_Dbg("RegisterActiveServerDomain: WaitForSingleObject failed (%08X)", wait);
		goto error;
	}

	if (dns->iStatus != 0) {
		Sunrise_Dbg("RegisterActiveServerDomain: DNS status %d for %s", dns->iStatus, domain);
		goto error;
	}
	if (dns->cina <= 0) {
		Sunrise_Dbg("RegisterActiveServerDomain: no addresses for %s", domain);
		goto error;
	}

	memcpy(&addr, dns->aina, sizeof(addr));

	WSACloseEvent(event);
	event = NULL;
	XNetDnsRelease(dns);
	dns = NULL;

	RegisterActiveServer(addr, description);
	Sunrise_Dbg("RegisterActiveServerDomain: %s -> %u.%u.%u.%u",
		domain,
		addr.S_un.S_un_b.s_b1, addr.S_un.S_un_b.s_b2,
		addr.S_un.S_un_b.s_b3, addr.S_un.S_un_b.s_b4);
	return;

error:
	if (event)
		WSACloseEvent(event);
	if (dns)
		XNetDnsRelease(dns);
	Sunrise_Dbg("RegisterActiveServerDomain: FAILED for %s — LSP/XHTTP will break",
		domain ? domain : "(null)");
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
		if (!lsp_server_registered) {
			Sunrise_Dbg("XamEnumerate LSP - Blamnet not registered yet, resolving");
			RegisterBungieServer();
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

// Destiny launch mode: holding LB while Destiny starts runs it against Blam
// Network (spoofed). Otherwise the plugin leaves it alone for release servers.

BOOL bDestinyBlamRequested = FALSE;

// One read of the pads when a title starts. SYSTEMAPP input is the dashboard's
// own stream and comes back with no buttons while a title is launching, so the
// plain gamepad query goes first - the same flags the title itself polls with.
BOOL IsLeftShoulderHeld()
{
	const DWORD flagSets[3] = {
		XINPUT_FLAG_GAMEPAD,
		XINPUT_FLAG_ANYUSER | XINPUT_FLAG_GAMEPAD,
		XINPUT_FLAG_SYSTEMAPP | XINPUT_FLAG_GAMEPAD,
	};
	XINPUT_STATE state;
	for (DWORD set = 0; set < ARRAYSIZE(flagSets); set++) {
		for (DWORD user = 0; user < XUSER_MAX_COUNT; user++) {
			ZeroMemory(&state, sizeof(state));
			DWORD result = XamInputGetState(user, flagSets[set], &state);
			if (result != ERROR_SUCCESS)
				continue;
			Sunrise_Trace("launch pad %d flags %08X buttons %04X", user, flagSets[set], state.Gamepad.wButtons);
			if (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) {
				Sunrise_Dbg("Launch pad %d buttons %04X (flags %08X)", user, state.Gamepad.wButtons, flagSets[set]);
				return TRUE;
			}
		}
	}
	return FALSE;
}

NTSTATUS XexLoadExecutableHook(PCHAR Name, PHANDLE Handle, DWORD TypeFlags, DWORD Version) {
	Sunrise_Trace("XexLoadExecutable %s flags %08X version %08X", Name, TypeFlags, Version);

	bDestinyBlamRequested = IsLeftShoulderHeld();
	Sunrise_Trace("LB check done: %d", bDestinyBlamRequested);
	if (bDestinyBlamRequested) {
		Sunrise_Dbg("LB held at launch");
		// A new process: set Destiny up again even if it was also the last title.
		LastTitleId = 0;
	}

	// Stop a running capture and restore its hooks while the old image is mapped.
	if (DestinyCaptureNewExecutable())
		LastTitleId = 0;
	Sunrise_Trace("capture new-executable check done, calling XexLoadExecutable");

	HANDLE Module = 0;
	NTSTATUS Result = XexLoadExecutable(Name, &Module, TypeFlags, Version);
	Sunrise_Trace("XexLoadExecutable result %08X module %08X", Result, Module);
	if (Handle != 0) *Handle = Module;
	if (NT_SUCCESS(Result)) SetupHaloPatches();
	Sunrise_Trace("SetupHaloPatches returned, SetupLoadHooks(%08X)", Module);
	SetupLoadHooks((PLDR_DATA_TABLE_ENTRY)Module);
	Sunrise_Trace("XexLoadExecutableHook done");
	return Result;
}

NTSTATUS XexLoadImageHook(CONST PCHAR Name, DWORD TypeFlags, DWORD Version, PHANDLE Handle) {
	Sunrise_Trace("XexLoadImage %s flags %08X version %08X", Name, TypeFlags, Version);

	HANDLE Module = 0;
	NTSTATUS Result = XexLoadImage(Name, TypeFlags, Version, &Module);
	Sunrise_Trace("XexLoadImage result %08X module %08X", Result, Module);
	if (Handle != 0) *Handle = Module;
	if (NT_SUCCESS(Result)) SetupHaloPatches();
	SetupLoadHooks((PLDR_DATA_TABLE_ENTRY)Module);
	Sunrise_Trace("XexLoadImageHook done");
	return Result;
}

VOID SetupLSPHooks()
{
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 3, (DWORD)NetDll_socketHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 51, (DWORD)NetDll_XNetStartupHook);
	PatchModuleImport((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle, MODULE_XAM, 52, (DWORD)NetDll_XNetCleanupHook);
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

DWORD __stdcall XUserReadStats_hook(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD* pcbResults, DWORD* pResults, void*)
{
	if (pcbResults)
		*pcbResults = 4;
	if (pResults)
		*pResults = 0;
	return 0;
}

VOID SetupXUserReadStatsHook(DWORD Address)
{
	Sunrise_Dbg("Ignoring true skill");
	PatchInJump((DWORD*)Address, (DWORD)&XUserReadStats_hook, false);
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
