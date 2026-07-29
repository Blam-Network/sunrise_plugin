//-----------------------------------------------------------------------------
//  Sunrise 3.0
//
//  Dev:
//			Byrom
// 
//  Credits:
//			craftycodie - Halo hooks and address
//-----------------------------------------------------------------------------

#include "stdafx.h"
#include "CoreHooks.h"
#include "XHttpHooks.h"
#include "Utilities.h"
#include <cstdarg>
#include "Detour.h"
#include <ppcintrinsics.h>
#include "HaloHooks.h"
#include "PacketCapture.h"

const char* SunriseVers = "3.1.2";

const char blamnet_description[XTITLE_SERVER_MAX_SERVER_INFO_LEN] = "required,mass_storage,other,ttl,usr,shr,web,dbg,upl,prs,std,wb2,bap,dwl";

BOOL bIsDevkit; // Set on plugin load. Skips doing xnotify on devkits
DWORD LastTitleId;

const DWORD Halo3InternalBeta = 0x4D53883A;
const DWORD Halo3ExternalBeta = 0x4D53880C;
const DWORD Halo3 = 0x4D5307E6;
const DWORD Halo3ODST = 0x4D530877;
const DWORD HaloReach = 0x4D53085B;
const DWORD HaloReachBeta = 0x4D53885C;
const DWORD DestinyPreRelease = 0x41560907;
const DWORD Destiny = 0x415608F8;

BOOL IsHalo(DWORD titleId) {
	switch (titleId) {
		case Halo3InternalBeta:
		case Halo3ExternalBeta:
		case Halo3:
		case Halo3ODST:
		case HaloReach:
		case HaloReachBeta:
			return true;
		default:
			return false;
	}
}

BOOL IsDestiny(DWORD titleId) {
	switch (titleId) {
		case Destiny:
		case DestinyPreRelease:
			return true;
		default:
			return false;
	}
}

BOOL bAllowRetailPlayers = TRUE;
BOOL bDisableXNotify = FALSE;
BOOL bIgnoreTrueskill = FALSE;
BOOL bLogEventsToStdout = TRUE;
BOOL bClearCacheOnLaunch = TRUE;
BOOL bEnableDevkitSockpatch = FALSE;
char* BlamnetDomain = "xbl.lsp.blam.network";

DWORD Halo3_Retail_XUserReadStats_Addr = 0x825B6358;
DWORD Halo3_Epsilon_XUserReadStats_Addr = 0x826E77E8;



VOID SpoofTitleVersion(PLDR_DATA_TABLE_ENTRY moduleTable) {
	if (!moduleTable) {
		moduleTable = *XexExecutableModuleHandle;
	}

	PLDR_DATA_TABLE_ENTRY PLDR_HaloXex = (PLDR_DATA_TABLE_ENTRY)moduleTable;
	XEX_EXECUTION_ID* pExecutionId = (XEX_EXECUTION_ID*)RtlImageXexHeaderField(PLDR_HaloXex->XexHeaderBase, XEX_HEADER_EXECUTION_ID);
	XEX_SECTION_INFO* sectionInfo = (XEX_SECTION_INFO*)RtlImageXexHeaderField(PLDR_HaloXex->XexHeaderBase, XEX_HEADER_SECTION_TABLE);

	DWORD TitleID = pExecutionId->TitleID;

	Sunrise_Dbg("SpoofTitleVersion called with title ID %08X", TitleID)

	SetTitleId(TitleID);

	if (TitleID == Halo3ExternalBeta || TitleID == Halo3 || TitleID == Halo3InternalBeta) {
		switch (PLDR_HaloXex->TimeDateStamp) {
			case 0x4649437B: // h3 beta tu1
			{
				SetTitleId(Halo3ExternalBeta);
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, Halo3ExternalBeta, Halo3, 0x1F6);
				break;
			}
			case 0x4637C172: { // h3 beta
				SetTitleId(Halo3ExternalBeta);
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, Halo3, Halo3, 0x1F6);
				RenameSPA(sectionInfo, Halo3ExternalBeta, Halo3, 0x1F6);
				break;
			}
			case 0x45EF61A8: { // mar 7 cache release
				SetTitleId(Halo3ExternalBeta);
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, Halo3, Halo3, 0x1F6);
				RenameSPA(sectionInfo, Halo3ExternalBeta, Halo3, 0x1F6);
				break;
			}
			case 0x45F1026C: { // mar 9 cache release
				SetTitleId(Halo3ExternalBeta);
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, Halo3, Halo3, 0x1F6);
				RenameSPA(sectionInfo, Halo3ExternalBeta, Halo3, 0x1F6);
				break;
			}
			case 0x455E2AC3: { // pimps
				SetTitleId(Halo3ExternalBeta);
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, Halo3, Halo3, 0x1F6);
				RenameSPA(sectionInfo, Halo3ExternalBeta, Halo3, 0x1F6);
				break;
			}
			case 0x46BC1368: { // 11729.07.08.10.0021.main
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, Halo3, Halo3, 0x1F6);
				break;
			}
			case 0x46B2D153: { // 11637.07.08.02.2348.release
				SetTitleId(Halo3InternalBeta);
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, Halo3, Halo3, 0x1F6);
				RenameSPA(sectionInfo, Halo3InternalBeta, Halo3, 0x1F6);
				break;
			}
			case 0x46CA8883: { // 11856
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, TitleID, Halo3, 0x630);
				break;
			}
			case 0x451313B9: { // first playtest
				pExecutionId->TitleID = Halo3;
				pExecutionId->Version = 0x32A;
				RenameSPA(sectionInfo, TitleID, Halo3, 0x630);
				break;
			}
		}
	}
	else if (TitleID == HaloReach) {
		switch (PLDR_HaloXex->TimeDateStamp) {
			case 0x4E559FF8: // reach tu1
			case 0x4C4AAE66: // reach tu0
				break;
			case 0x4B7F307A: {
				SetTitleId(HaloReachBeta);
				pExecutionId->TitleID = HaloReach;
				pExecutionId->Version = 257;
				RenameSPA(sectionInfo, TitleID, HaloReach, 0x1F6);
			}
			case 0x4BABF021: {
				SetTitleId(HaloReachBeta);
				pExecutionId->TitleID = HaloReach;
				pExecutionId->Version = 257;
				RenameSPA(sectionInfo, TitleID, HaloReach, 0x1F6);
			}
			case 0x4BBC0DF7: {
				SetTitleId(HaloReachBeta);
				pExecutionId->TitleID = HaloReach;
				pExecutionId->Version = 257;
				RenameSPA(sectionInfo, TitleID, HaloReach, 0x1F6);
			}
			case 0x4BBF8F02: {
				SetTitleId(HaloReachBeta);
				pExecutionId->TitleID = HaloReach;
				pExecutionId->Version = 257;
				RenameSPA(sectionInfo, TitleID, HaloReach, 0x1F6);
			}
			default: {
				pExecutionId->TitleID = HaloReach;
				pExecutionId->Version = 257;
				RenameSPA(sectionInfo, TitleID, HaloReach, 0x1F6);
				break;
			}
		}
	}
	else if (TitleID == HaloReachBeta) {
		Sunrise_Dbg("Halo: Reach Beta detected! Spoofing...");

		pExecutionId->TitleID = HaloReach;
		pExecutionId->Version = 257;
		RenameSPA(sectionInfo, TitleID, HaloReach, 0x1F6); 
	}
	else if (TitleID == DestinyPreRelease) {
		Sunrise_Dbg("Destiny Pre Release detected! Spoofing...");

		SetTitleId(DestinyPreRelease);
		pExecutionId->TitleID = Destiny;
		pExecutionId->Version = 5890; 
		RenameSPA(sectionInfo, TitleID, Destiny, 0x160);
	}
}

VOID AllowRetailPlayers_HALO3_RETAIL()
{
	Sunrise_Dbg("Allowing Retail players");
	// allow MM to start with offline peers
	*((DWORD*)(0x822BA37C)) = 0x60000000;
	// disable host migration before map/game variants are downloaded.
	*((DWORD*)(0x824004BC)) = 0x60000000;
	*((DWORD*)(0x824004C0)) = 0x60000000;
	*((DWORD*)(0x824004C4)) = 0x60000000;
	*((DWORD*)(0x824004C8)) = 0x60000000;
	*((DWORD*)(0x824004CC)) = 0x60000000;
	*((DWORD*)(0x824004D0)) = 0x60000000;
	*((DWORD*)(0x824004D4)) = 0x60000000;
	*((DWORD*)(0x824004D8)) = 0x60000000;

	// Allow MM to start with peers who don't have exp loaded
	*((DWORD*)(0x823F8B04)) = 0x60000000;
	*((DWORD*)(0x822BB77C)) = 0x60000000;
	*((DWORD*)(0x822BB788)) = 0x60000000;
	*((DWORD*)(0x822BB794)) = 0x60000000;
	*((DWORD*)(0x822BB7A0)) = 0x60000000;
}

VOID AllowRetailPlayers_HALOREACH_RETAIL()
{
	Sunrise_Dbg("Allowing Retail players");
	// allow MM to start with offline players
	*((DWORD*)(0x82290744)) = 0x60000000;
	*((DWORD*)(0x82290750)) = 0x60000000;

	// disable host migration before map/game variants are downloaded.
	*((DWORD*)(0x82287B20)) = 0x60000000;
	*((DWORD*)(0x82287B24)) = 0x60000000;
	*((DWORD*)(0x82287B28)) = 0x60000000;
	*((DWORD*)(0x82287B2C)) = 0x60000000;
	*((DWORD*)(0x82287B30)) = 0x60000000;
	*((DWORD*)(0x82287B38)) = 0x60000000;
	*((DWORD*)(0x82287B40)) = 0x60000000;
	*((DWORD*)(0x82287B60)) = 0x60000000;
}

VOID SetupHaloPatches() {
	PLDR_DATA_TABLE_ENTRY PLDR_Xex = (PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle;
	if (!PLDR_Xex) {
		Sunrise_Dbg("PLDR_HaloXex was null, weird");
		return;
	}
	XEX_EXECUTION_ID* pExecutionId = (XEX_EXECUTION_ID*)RtlImageXexHeaderField(PLDR_Xex->XexHeaderBase, XEX_HEADER_EXECUTION_ID);

	DWORD TitleID = pExecutionId->TitleID;
	if (TitleID != LastTitleId)
	{
		if (MountPath(MOUNT_POINT, GetMountPath()) != 0)
		{
			Sunrise_Dbg("Failed to set mount point!");
			return;
		}
		Readini();
		ApplyPrivHook();

		// Stop Destiny capture / key dump when leaving Destiny retail.
		if (TitleID != Destiny) {
			if (IsPacketCaptureActive())
			StopPacketCapture();
		}

		LastTitleId = TitleID; // Set the last title id  to the current title id so we don't loop rechecking

		XEX_SECTION_INFO* sectionInfo = (XEX_SECTION_INFO*)RtlImageXexHeaderField(PLDR_Xex->XexHeaderBase, XEX_HEADER_SECTION_TABLE);

		Sunrise_Dbg("Loaded title %08X v %d", pExecutionId->TitleID, pExecutionId->Version);

		if (IsHalo(TitleID) || IsDestiny(TitleID)) {
			SetupLSPHooks();
			SpoofTitleVersion(PLDR_Xex);
		}

		if (TitleID == Halo3 || TitleID == Halo3ExternalBeta || TitleID == Halo3InternalBeta) // Check for both regular and alpha/beta title ids
		{
			switch (PLDR_Xex->TimeDateStamp) // Detects the exact xex by timestamp. Prevents patching static addresses in the wrong xex.
			{
			case 0x48C1FB10: // Halo 3 Retail TU2
			{
				Sunrise_Dbg("Halo 3 Retail detected! Initialising hooks...");

				if (bAllowRetailPlayers)
					AllowRetailPlayers_HALO3_RETAIL();

				if (bIgnoreTrueskill)
					SetupXUserReadStatsHook(Halo3_Retail_XUserReadStats_Addr);

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x825982F8);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x46CA8883: // Halo 3 Epsilon Aug 20th 2007
			{
				Sunrise_Dbg("Halo 3 Epsilon (Aug 20th) detected! Initialising hooks...");

				if (bIgnoreTrueskill)
					SetupXUserReadStatsHook(Halo3_Epsilon_XUserReadStats_Addr);

				if (bLogEventsToStdout)
					SetupHalo3EventsHook(0x82235F08);

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x826C9540);

				// Log events to file
				*((DWORD*)(0x82236154)) = 0x60000000;

				// Allow Epsilon to load Release maps.
				*((char*)(0x82232737)) = 1; // skip build string validation
				*((DWORD*)(0x822401F4)) = 0x48000018; // skip RSA validation
				*((char*)(0x8223DB7F)) = 1; // another skip RSA validation skip

				// Use TU0 hopper files.
				*((WORD*)(0x824900EA)) = 11855; // use release title storage
				
				XNotify(L"Halo Sunrise Initialised!");
				break;
			}
			case 0x46B2D153: // halo3 cache release xenon 11637.07.08.02.2348.release  Aug  2 2007 23:50:55
			{
				Sunrise_Dbg("11637.07.08.02.2348.release detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x826C0218);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x46BC1368: // 11729.07.08.10.0021.main
			{
				Sunrise_Dbg("11729.07.08.10.0021.main detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x82575C00);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4637C172: // Halo 3 Beta May 1st 2007
			{
				Sunrise_Dbg("Halo 3 Beta (May 1st) detected! Initialising hooks...");
				SetupSpoofHooks();
				
				if(bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x824CFA18);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4649437B: // Halo 3 Beta May 15th 2007
			{
				Sunrise_Dbg("Halo 3 Beta (May 15th) detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x824CFA90);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x451313B9: // first playtest
			{
				Sunrise_Dbg("Halo 3 first playtest detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x82161700);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x455E2AC3: // Halo 3 Pimps at sea (Alpha)
			{
				Sunrise_Dbg("Halo 3 Pimps at sea (Alpha) detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x821711D0);

				// Fix a bug where high rank players can't enter matchmaking.
				*((DWORD*)(0x82457578)) = 0x3960000C;
				// Fix another stats bug.
				*((WORD*)(0x82454BAC)) = 0x4800;
				
				// Force the game to save settings even if a newer file is present.
				*((WORD*)(0x82970B30)) = 0x4800;

				// Enable debug logs.
				*((DWORD*)(0x823b23d0)) = 0x60000000;
				// Move them from cache:\\ to d:\\ 
				const char* reports_path = "d:\\reports\\";
				memcpy(((char*)(0x820B934C)), reports_path, strlen(reports_path) + 1);

				XNotify(L"Halo Sunrise Initialised!");
				break;
			}
			case 0x45F10275: // Halo 3 Delta cache_test
			{
				Sunrise_Dbg("Halo 3 Delta (cache_test, Mar 9th) detected! Initialising hooks...");
				SetupSpoofHooks();

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x45F350CB: // halo3 cache profile xenon untracked version  Mar  9 2007 22:15:40
			{
				Sunrise_Dbg("halo3 cache profile xenon untracked version  Mar  9 2007 22:15:40 detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x82101418);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x45F1026C: // 08172.07.03.08.2240.delta cache_release
			{
				Sunrise_Dbg("Halo 3 08172.07.03.08.2240.delta detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x824BED20);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x45EF61A8: // 08117.07.03.07.1702.delta cache_release
			{
				Sunrise_Dbg("Halo 3 Delta 08117.07.03.07.1702.delta detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x824DE540);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			default:
			{
				Sunrise_Dbg("Unrecognized Halo 3 xex! TimeDateStamp: 0x%X", PLDR_Xex->TimeDateStamp); // Print the timestamp so we can support this xex later if required.

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			}
		}
		else if (TitleID == Destiny)
		{
			switch (PLDR_Xex->TimeDateStamp)
			{
			case 0x579810CC: // default_ttk_231 / tiger_release_final — Jul 27 2016 (v0.0.23.2)
			{
				Sunrise_Dbg("Destiny retail ttk_231 (Jul 2016) detected! Capture + BAP key dump...");
				// StartPacketCapture();
				// StartBapKeyDump();
				XNotify(L"Destiny Sunrise Initialized!");
				break;
			}
			default:
			{
				Sunrise_Dbg("Unrecognized Destiny xex! TimeDateStamp: 0x%X — trying string-scan key dump",
					PLDR_Xex->TimeDateStamp);
			XNotify(L"Destiny Sunrise Initialized!");
				break;
			}
			}
		}
		else if (TitleID == DestinyPreRelease) // 36735.13.12.02.1953.alpha
		{
			switch (PLDR_Xex->TimeDateStamp)
			{
				case 0x529D59D0:
				{
					Sunrise_Dbg("Destiny 1 pre-alpha loaed. I hope you know what youre doing!");

					SetupSpoofHooks();
					SetupXHttpHooks(); 
					// Enable debug logs.
					*((DWORD*)(0x825E0E44)) = 0x38A00001; // li r5, 1

					// Skip password
					*((DWORD*)(0x825E1038)) = 0x48000108; // b 0x825E1140

					// Force cache0:\\ reports path available
					*((DWORD*)(0x8280B720)) = 0x60000000;
					*((DWORD*)(0x8280C040)) = 0x60000000;
					*((DWORD*)(0x8280BF58)) = 0x480000C8;

					// Bypass Proof of Ownership / DLC director dialog
					// (from 41560907 Destiny patch.toml)
					*((DWORD*)(0x83692AE8)) = 0x38600001; // unlock flag leaf -> yes
					*((DWORD*)(0x83692AEC)) = 0x4E800020; // blr
					*((DWORD*)(0x83693BA8)) = 0x38600001; // unlock expr -> yes
					*((DWORD*)(0x83693BAC)) = 0x4E800020; // blr
					*((DWORD*)(0x8282D230)) = 0x38800000; // offer-key lookup *a1=0
					*((DWORD*)(0x8282D234)) = 0x90830000; // stw r4, 0(r3)
					*((DWORD*)(0x8282D238)) = 0x4E800020; // blr
					*((DWORD*)(0x82B31990)) = 0x38600000; // PoO needed? no
					*((DWORD*)(0x82B31994)) = 0x4E800020;
					*((DWORD*)(0x82B31900)) = 0x38600000; // PoO in progress? no
					*((DWORD*)(0x82B31904)) = 0x4E800020;
					*((DWORD*)(0x82B31A38)) = 0x38600000; // start PoO? never
					*((DWORD*)(0x82B31A3C)) = 0x4E800020;
					*((DWORD*)(0x82B31B30)) = 0x38600000; // PoO tick nop
					*((DWORD*)(0x82B31B34)) = 0x4E800020;
					*((DWORD*)(0x82B2A750)) = 0x38600001; // offer owned? yes
					*((DWORD*)(0x82B2A754)) = 0x4E800020;
					*((DWORD*)(0x82B2A378)) = 0x38600000; // ownership busy? no
					*((DWORD*)(0x82B2A37C)) = 0x4E800020;
					*((DWORD*)(0x82B2A808)) = 0x38600001; // owned content? yes
					*((DWORD*)(0x82B2A80C)) = 0x4E800020;
					*((DWORD*)(0x82BA7290)) = 0x38600001; // offer+key owned? yes
					*((DWORD*)(0x82BA7294)) = 0x4E800020;
					*((DWORD*)(0x82BA7360)) = 0x38600000; // DLC marketplace never
					*((DWORD*)(0x82BA7364)) = 0x4E800020;
					*((DWORD*)(0x82BA7480)) = 0x38600000; // DLC tick nop
					*((DWORD*)(0x82BA7484)) = 0x4E800020;
					*((DWORD*)(0x837CAF50)) = 0x38600001; // DLC kickoff ok
					*((DWORD*)(0x837CAF54)) = 0x4E800020;
					*((DWORD*)(0x837CABC0)) = 0x38600001; // PoO kickoff ok
					*((DWORD*)(0x837CABC4)) = 0x4E800020;
					*((DWORD*)(0x82BA6340)) = 0x38600000; // DLC wait clear
					*((DWORD*)(0x82BA6344)) = 0x4E800020;

					XNotify(L"Destiny Sunrise Initialized!");
				}
			}
		}
		else if (TitleID == Halo3ODST)
		{
			switch (PLDR_Xex->TimeDateStamp) // Detects the exact xex by timestamp. Prevents patching static addresses in the wrong xex.
			{
			case 0x49F68EC3: // Halo 3 ODST
			{
				Sunrise_Dbg("Halo 3 ODST detected! Initialising hooks...");

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			default:
			{
				Sunrise_Dbg("Unrecognized Halo 3 ODST xex! TimeDateStamp: 0x%X", PLDR_Xex->TimeDateStamp); // Print the timestamp so we can support this xex later if required.

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			}
		}
		else if (TitleID == HaloReach || TitleID == HaloReachBeta)
		{
			switch (PLDR_Xex->TimeDateStamp)
			{
			case 0x4C4AAE66: // tu0
			{
				Sunrise_Dbg("Halo: Reach detected! Initialising hooks...");

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4C4AAE6A: // tu0 release internal
			{
				Sunrise_Dbg("omaha 11860 cache release internal Initialising hooks...");

				SpoofTitleVersion(PLDR_Xex);
				// This build has a different public key to normal reach, so we skip it's checks to allow release assets.
				SetupRSAVerificationHook(0x824D0518);

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x82A01038);

				if (bLogEventsToStdout)
					SetupHaloReachEventsHook(0x827828F0);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4C4AB277: // tu0 test
			{
				Sunrise_Dbg("omaha 11860 cache test Initialising hooks...");

				SpoofTitleVersion(PLDR_Xex);
				// This build has a different public key to normal reach, so we skip it's checks to allow release assets.
				SetupRSAVerificationHook(0x8280D8E8);

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x823C3B70);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4E559FF8: //reach tu
			{
				Sunrise_Dbg("Halo: Reach TU detected! Initialising hooks...");

				if (bAllowRetailPlayers)
					AllowRetailPlayers_HALOREACH_RETAIL();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x827E2B38);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4CC5E691: // what build is this?
			{
				Sunrise_Dbg("Halo: Reach detected! Initialising hooks...");

				// Load retail maps.
				*((DWORD*)(0x823C0244)) = 0x60000000;
				*((DWORD*)(0x823C01E4)) = 0x60000000;
				// Havok Patch
				*((DWORD*)(0x8305C000)) = 0x60000000;
				*((DWORD*)(0x8305C010)) = 0x60000000;

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4B7F307A: // private alpha
			{
				Sunrise_Dbg("Halo: Reach Alpha detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x82937038);

				*((DWORD*)(0x8229732C)) = 0x60000000; // enable log files

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4BABF021: { // private beta
				Sunrise_Dbg("Halo: Reach Beta detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x829696F8);

				*((DWORD*)(0x82294284)) = 0x60000000; // enable log files

				break;
			}
			case 0x4BBC0DF7: { // Private Beta TU
				Sunrise_Dbg("Halo: Reach Beta detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x8296A1A0);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			case 0x4BBF8F02: // Public Beta
			{
				Sunrise_Dbg("Halo: Reach Beta detected! Initialising hooks...");
				SetupSpoofHooks();

				if (bClearCacheOnLaunch)
					SetupXMountUtilityDriveExHook(0x8276BB10);

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			default:
			{
				Sunrise_Dbg("Unrecognized Halo Reach xex! TimeDateStamp: 0x%X", PLDR_Xex->TimeDateStamp); // Print the timestamp so we can support this xex later if required.

				XNotify(L"Halo Sunrise Initialized!");
				break;
			}
			}
		}
	}
}

VOID RegisterBungieServer()
{
	DWORD titleID = XamGetCurrentTitleId();

	// After Destiny PreRelease spoof, Xam title id is retail Destiny.
	if (IsHalo(titleID) || IsDestiny(titleID)) {
		RegisterActiveServerDomain(BlamnetDomain, blamnet_description);
	}
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		if (IsTrayOpen())
		{
			Sunrise_Dbg("Plugin load aborted! Disc tray is open");
			HANDLE hSunrise = hModule;
			*(WORD*)((DWORD)hSunrise + 64) = 1;
			return FALSE;
		}

		SetupLoadHooks(nullptr);
		bIsDevkit = *(DWORD*)0x8E038610 & 0x8000 ? FALSE : TRUE; // Simple devkit check
		Sunrise_Dbg("v%s loaded! Running on %s kernel", SunriseVers, bIsDevkit ? "Devkit" : "Retail");
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		StopPacketCapture();
		Sunrise_Dbg("Unloaded!");
		break;

	}
	return TRUE;
}
