#include "stdafx.h"
#include "Sunrise3.h"
#include "Utilities.h"
#include "Detour.h"

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

#ifndef HINTERNET
typedef PVOID HINTERNET;
#endif

#define XHTTP_FLAG_SECURE 0x00800000

typedef HINTERNET (NTAPI *NetDll_XHttpConnect_t)(
	XNCALLER_TYPE xnc,
	HINTERNET hSession,
	const CHAR* serverName,
	WORD port,
	DWORD flags
);

static NetDll_XHttpConnect_t g_XHttpConnect = NULL;

HINTERNET NetDll_XHttpConnectHook(
	XNCALLER_TYPE xnc,
	HINTERNET hSession,
	const CHAR* serverName,
	WORD port,
	DWORD flags
) {
	WORD redirectPort = port;
	DWORD redirectFlags = flags & ~XHTTP_FLAG_SECURE;

	if (redirectPort == 443)
		redirectPort = 80;

	Sunrise_Dbg("XHttpConnect(%s:%u flags=%08X) -> %s:%u flags=%08X",
		serverName ? serverName : "(null)", port, flags,
		BlamnetDomain, redirectPort, redirectFlags);

	return g_XHttpConnect(xnc, hSession, BlamnetDomain, redirectPort, redirectFlags);
}

VOID SetupXHttpHooks()
{
	if (!g_XHttpConnect) {
		g_XHttpConnect = (NetDll_XHttpConnect_t)ResolveFunction(MODULE_XAM, 205);
		if (!g_XHttpConnect) {
			Sunrise_Dbg("Failed to resolve NetDll_XHttpConnect");
			return;
		}
	}

	PatchModuleImport(
		(PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle,
		MODULE_XAM,
		205,
		(DWORD)NetDll_XHttpConnectHook
	);
	Sunrise_Dbg("XHttpConnect hook installed -> %s", BlamnetDomain);
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