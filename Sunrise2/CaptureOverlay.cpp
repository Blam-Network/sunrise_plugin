// On-screen capture overlay for Destiny 2.3.1 (default_ttk_231, 0x579810CC).
//
// Draws "capturing packets (n sent, n received)" top left and the real
// pre-alpha watermark, in the pre-alpha layout: the watermark lines at
// (0.2, 0.8), the id hashes at (0.8, 0.8), white at alpha 0xB0 with a shadow,
// in Bungie's fixedsys-9 fallback font (CaptureFont.cpp).
//
// No threads and no allocation. Everything runs synchronously in the
// D3DDevice_Swap hook (sub_831AAF18) on the render thread:
// - Swap increments device+0x4248 for every swap it queues; the swap interrupt
//   increments device+0x424C when the GPU reaches that swap, after the resolve
//   into the front buffer. The hook records the buffer it was handed and its
//   swap number, then draws into the PREVIOUS recorded buffer once device+0x424C
//   shows the GPU has passed it (resolve done, currently on screen).
// - The watermark text is the game's own: while capturing we set byte_83709C6E
//   so sub_82474AA8 formats it each frame (its draw calls are gone), and a
//   Detour on it copies the two strings out of its stack frame. If none arrives,
//   the overlay formats the same lines itself.
//
// Every value read from the game is validated (readable pointers, a 2D 32 bpp
// texture of sane size, a CPU mapping whose physical address matches the base).
// Any doubt skips the frame; repeated failure turns the overlay off for the
// session with one log line, and the counters fall back to XNotify toasts.

#include "stdafx.h"
#include "CaptureOverlay.h"
#include "CaptureFont.h"
#include "Utilities.h"
#include <xgraphics.h>
#include <ppcintrinsics.h>

// --- game addresses (2.3.1, IDA :13341) ---------------------------------------------

static const DWORD DEV_SWAPS_QUEUED = 0x4248;  // ++ at the top of D3DDevice_Swap
static const DWORD DEV_SWAPS_REACHED = 0x424C; // ++ in the swap interrupt sub_831AA0E8
static const DWORD TEXTURE_FETCH = 0x1C;       // fetch constant in a D3DBaseTexture

// Watermark gate, only read by sub_82474AA8 (no writer in the image).
static volatile BYTE* const WATERMARK_GATE = (volatile BYTE*)0x83709C6E;

// sub_82474AA8 frame (stwu r1,-0x400): "PRE-ALPHA BUILD (%c) (logged %d)
// (NAT:%s)\r\n%s @ ..." at sp+0x1A0, "%016llx\r\n%016llx" at sp+0x120.
static const DWORD MARK_FROM_TOP = 0x400 - 0x1A0;
static const DWORD HASH_BELOW_MARK = 0x1A0 - 0x120;
static const DWORD MARK_SIZE = 520;
static const DWORD HASH_SIZE = 128;

// Current slice-set index. sub_823D5278 hands out &off_83448D50; the live
// object hangs off +4 and the packed slice-set field sits at +0x110C, which
// sub_823CEA98 turns into an index with (value >> 3) & 0x3F, or -1 when the
// field itself is -1. Same value sub_82522878 reports as "map slice set index".
static const DWORD SLICE_SET_HOLDER = 0x83448D54;
static const DWORD SLICE_SET_FIELD = 0x110C;

static const DWORD MARK_FALLBACK_MS = 3000;
static const DWORD FAIL_STREAK = 16; // consecutive bad frames before giving up

// --- state ------------------------------------------------------------------------------

typedef DWORD (*D3DSwap_t)(DWORD, DWORD, DWORD);
typedef VOID (*SwapIsr_t)(DWORD);
typedef VOID (*WatermarkFormat_t)();
void* o_swap;
void* o_watermark;
void* o_swapInterrupt;

// g_lock guards the watermark text and the plan cache (the Swap hook runs on
// the render thread, the watermark hook on a job worker, start/stop on the
// input thread). It is a lock, not a thread.
static CRITICAL_SECTION g_lock;
static BOOL g_ready = FALSE;

static volatile BOOL g_on = FALSE;      // drawing; stays on after a capture ends
static volatile BOOL g_active = FALSE;  // a capture is running
static volatile BOOL g_failed = FALSE;  // gave up for this session
static BOOL g_swapHooked = FALSE;
static BOOL g_markHooked = FALSE;
static BOOL g_isrHooked = FALSE;
static DWORD g_failStreak = 0;
static DWORD g_drawCount = 0;

static volatile DWORD g_sent = 0;
static volatile DWORD g_received = 0;
static volatile DWORD g_saved = 0;
static DWORD g_onTick = 0;

// Buffers handed to Swap that are not drawn into yet: the GPU has to reach a
// swap before that buffer is resolved and safe to touch, and it runs several
// swaps behind. Remembering only the newest dropped every buffer the GPU had
// not caught up to, and those frames displayed with no overlay at all.
static const DWORD PENDING_MAX = 16; // the depth the counters already allow

struct Pending
{
	DWORD fetch[6];
	DWORD seq;
};

static Pending g_pending[PENDING_MAX];
static DWORD g_pendingHead = 0;
static DWORD g_pendingCount = 0;

static CHAR g_markText[MARK_SIZE];
static CHAR g_hashText[HASH_SIZE];
static DWORD g_markTick = 0;

// Watermark hook scratch (its own thread only).
static CHAR g_scanMark[MARK_SIZE];
static CHAR g_scanHash[HASH_SIZE];
static DWORD g_scanDelta = 0;

// --- helpers ------------------------------------------------------------------------------

static VOID Fail(const char* reason)
{
	if (!g_failed) {
		g_failed = TRUE;
		g_on = FALSE;
		Sunrise_Dbg("[capture] overlay off for this session: %s", reason);
		if (g_ready) {
			EnterCriticalSection(&g_lock);
			if (g_markHooked)
				*WATERMARK_GATE = 0;
			LeaveCriticalSection(&g_lock);
		}
	}
}

static BOOL Readable(DWORD addr, DWORD size)
{
	if (!addr || addr + size < addr)
		return FALSE;
	return MmIsAddressValid((PVOID)addr) && MmIsAddressValid((PVOID)(addr + size - 1));
}

// -1 until the world controller has a slice set, so never before orbit.
static int CurrentSliceSet()
{
	if (!Readable(SLICE_SET_HOLDER, 4))
		return -1;
	DWORD object = *(volatile DWORD*)SLICE_SET_HOLDER;
	if (!Readable(object + SLICE_SET_FIELD, 4))
		return -1;
	DWORD packed = *(volatile DWORD*)(object + SLICE_SET_FIELD);
	return packed == 0xFFFFFFFF ? -1 : (int)((packed >> 3) & 0x3F);
}

// Physical memory is visible at 0xA0000000 (64 KB pages), 0xC0000000 (16 MB
// pages) and 0xE0000000 (4 KB pages, offset by 0x1000). Only the view the
// allocation was made in is mapped, so try each and let the kernel confirm.
static BYTE* MapPhysical(DWORD phys, DWORD bytes)
{
	const DWORD candidates[3] = {
		0xA0000000 | phys,
		0xC0000000 | phys,
		phys >= 0x1000 ? 0xE0000000 + (phys - 0x1000) : 0,
	};
	for (DWORD i = 0; i < 3; i++) {
		DWORD va = candidates[i];
		DWORD last = va + bytes - 1;
		if (!va || last < va)
			continue;
		if (!MmIsAddressValid((PVOID)va) || !MmIsAddressValid((PVOID)last)) {
			Sunrise_Trace("[capture] overlay: %08X..%08X not mapped", va, last);
			continue;
		}
		DWORD first = (DWORD)MmGetPhysicalAddress((PVOID)va);
		if (first != phys || (DWORD)MmGetPhysicalAddress((PVOID)last) != phys + bytes - 1) {
			Sunrise_Trace("[capture] overlay: %08X is physical %08X, wanted %08X", va, first, phys);
			continue;
		}
		return (BYTE*)va;
	}
	return NULL;
}

// --- front buffer plan --------------------------------------------------------------------

struct Plan
{
	DWORD fetch[6];
	BOOL used;
	BYTE* pixels;      // CPU address of texel (0, 0)
	DWORD width, height;
	DWORD pitch;       // texels per row
	BOOL tiled;
	DWORD phys;        // masked physical base, how the swap interrupt finds it
};

static Plan g_plans[4];
static DWORD g_nextPlan = 0;

// Returns a validated plan for this fetch constant, or NULL (with a fail vote).
static Plan* PlanFor(const DWORD fetch[6])
{
	for (DWORD i = 0; i < ARRAYSIZE(g_plans); i++)
		if (g_plans[i].used && memcmp(g_plans[i].fetch, fetch, sizeof(g_plans[i].fetch)) == 0)
			return g_plans[i].pixels ? &g_plans[i] : NULL;

	Plan p;
	ZeroMemory(&p, sizeof(p));
	memcpy(p.fetch, fetch, sizeof(p.fetch));
	p.used = TRUE;

	GPUTEXTURE_FETCH_CONSTANT f;
	memcpy(&f, fetch, sizeof(f));
	Plan* slot = &g_plans[g_nextPlan++ % ARRAYSIZE(g_plans)];

	// Logged once per new front buffer, whatever the verdict below.
	Sunrise_Trace("[capture] overlay: fetch %08X %08X %08X %08X %08X %08X type %u dim %u fmt %u size %ux%u pitch %u tiled %u base %08X",
		fetch[0], fetch[1], fetch[2], fetch[3], fetch[4], fetch[5], f.Type, f.Dimension, f.DataFormat,
		f.Size.TwoD.Width + 1, f.Size.TwoD.Height + 1, f.Pitch << 5, f.Tiled, f.BaseAddress << 12);

	if (f.Type != GPUCONSTANTTYPE_TEXTURE || f.Dimension != GPUDIMENSION_2D) {
		Sunrise_Trace("[capture] overlay: rejected, not a 2D texture");
		*slot = p; // remembered as unusable (pixels NULL)
		return NULL;
	}
	switch (f.DataFormat) {
		case GPUTEXTUREFORMAT_8_8_8_8:
		case GPUTEXTUREFORMAT_8_8_8_8_AS_16_16_16_16:
		case GPUTEXTUREFORMAT_2_10_10_10:
		case GPUTEXTUREFORMAT_2_10_10_10_AS_16_16_16_16:
			break; // white is all ones and black all zeroes in both
		default:
			Sunrise_Trace("[capture] overlay: rejected, format %u not handled", f.DataFormat);
			*slot = p;
			return NULL;
	}
	p.width = f.Size.TwoD.Width + 1;
	p.height = f.Size.TwoD.Height + 1;
	p.pitch = f.Pitch << 5;
	p.tiled = f.Tiled;
	if (p.width < 320 || p.height < 240 || p.width > 2048 || p.height > 2048 || p.pitch < p.width || p.pitch > 8192) {
		Sunrise_Trace("[capture] overlay: rejected, size %ux%u pitch %u", p.width, p.height, p.pitch);
		*slot = p;
		return NULL;
	}

	DWORD rows = p.tiled ? (p.height + 31) & ~31 : p.height;
	DWORD rowTexels = p.tiled ? (p.pitch + 31) & ~31 : p.pitch;
	// The fetch constant's base is a GPU address, which is the physical address
	// with the CPU's physical-window bits still on it (0xB574E000 for physical
	// 0x1574E000). Strip them: MapPhysical puts a window back on.
	DWORD phys = (f.BaseAddress << 12) & 0x1FFFFFFF;
	p.phys = phys;
	p.pixels = phys ? MapPhysical(phys, rows * rowTexels * 4) : NULL;
	*slot = p;
	if (!p.pixels) {
		Sunrise_Trace("[capture] overlay: rejected, physical %08X (%u bytes) has no CPU mapping", phys, rows * rowTexels * 4);
		return NULL;
	}

	Sunrise_Dbg("[capture] overlay: front buffer %ux%u fmt %u %s pitch %u at %08X (phys %08X)",
		p.width, p.height, f.DataFormat, p.tiled ? "tiled" : "linear", p.pitch, (DWORD)p.pixels, phys);
	return slot;
}

static DWORD TexelOffset(const Plan* p, DWORD x, DWORD y)
{
	if (p->tiled)
		return XGAddress2DTiledOffset(x, y, p->pitch, 4) * 4;
	return (y * p->pitch + x) * 4;
}

// --- text ---------------------------------------------------------------------------------

static const BYTE STAMP_SHADOW = 0x10;
static const BYTE STAMP_ALPHA = 0x0F;

static BYTE g_stamp[2048 * 72];

// Lays out `len` characters the way c_draw_string does and returns the size.
static DWORD BuildStamp(const CHAR* text, DWORD len, DWORD scale, DWORD maxWidth, DWORD* outW, DWORD* outH)
{
	int s = (int)scale;
	int shadow = s > 1 ? s / 2 : 1;
	int ascent = CaptureFontAscent();
	int h = (CaptureFontLineHeight() + 1) * s + shadow;
	int cursor = 0;
	for (DWORD i = 0; i < len; i++) {
		const CaptureGlyph* g = CaptureFontGlyph((BYTE)text[i]);
		if (!g)
			g = CaptureFontGlyph('?');
		int step = g ? (g->advance + g->left) * s : 8 * s;
		if (cursor + step + shadow > (int)maxWidth)
			break;
		cursor += step;
	}
	int w = cursor + shadow;
	if (w <= shadow || w * h > (int)sizeof(g_stamp))
		return 0;
	memset(g_stamp, 0, w * h);

	for (int pass = 0; pass < 2; pass++) {
		int offset = pass == 0 ? shadow : 0;
		cursor = 0;
		for (DWORD i = 0; i < len && cursor < w - shadow; i++) {
			const CaptureGlyph* g = CaptureFontGlyph((BYTE)text[i]);
			if (!g)
				g = CaptureFontGlyph('?');
			if (!g) {
				cursor += 8 * s;
				continue;
			}
			int left = cursor + g->left * s + offset;
			int top = (ascent - g->originY) * s + offset;
			for (int gy = 0; gy < g->height; gy++) {
				for (int gx = 0; gx < g->width; gx++) {
					BYTE a = g->alpha[gy * g->width + gx];
					if (!a)
						continue;
					for (int sy = 0; sy < s; sy++) {
						int y = top + gy * s + sy;
						if (y < 0 || y >= h)
							continue;
						for (int sx = 0; sx < s; sx++) {
							int x = left + gx * s + sx;
							if (x < 0 || x >= w)
								continue;
							BYTE* at = &g_stamp[y * w + x];
							if (pass == 0)
								*at |= STAMP_SHADOW;
							else if (a > (*at & STAMP_ALPHA))
								*at = (BYTE)((*at & ~STAMP_ALPHA) | a);
						}
					}
				}
			}
			cursor += (g->advance + g->left) * s;
		}
	}
	*outW = w;
	*outH = h;
	return len;
}

// Opaque: a covered pixel is white, a shadow-only pixel is black. Nothing
// underneath shows through, so the pixel never has to be read back.
static DWORD StampPixel(BYTE stamp)
{
	return (stamp & STAMP_ALPHA) >= 8 ? 0xFFFFFFFF : 0x00000000;
}

// The pixels for one frame's text, as byte offsets into a front buffer with
// bit 0 carrying the colour (set = white, clear = black). Every front buffer
// shares format, pitch and tiling, so one list fits all of them and only the
// base address differs. Built on the render thread, applied by the swap
// interrupt, which cannot take a lock: two lists, and the live index is
// published only once a list is complete.
static const DWORD PAINT_MAX = 24576;

struct PaintList
{
	DWORD count;
	DWORD pixel[PAINT_MAX];
};

static PaintList g_paint[2];
static volatile DWORD g_paintLive = 0; // index of the finished list, or 2 for none
static PaintList* g_building = NULL;

static VOID ApplyStamp(Plan* p, DWORD x0, DWORD y0, DWORD w, DWORD h)
{
	DWORD line = 0xFFFFFFFF;
	for (DWORD y = 0; y < h && y0 + y < p->height; y++) {
		for (DWORD x = 0; x < w && x0 + x < p->width; x++) {
			BYTE stamp = g_stamp[y * w + x];
			if (!stamp)
				continue;
			DWORD off = TexelOffset(p, x0 + x, y0 + y);
			if (g_building) {
				if (g_building->count < PAINT_MAX)
					g_building->pixel[g_building->count++] = off | ((stamp & STAMP_ALPHA) >= 8 ? 1 : 0);
				continue;
			}
			BYTE* at = p->pixels + off;
			if ((off & ~127) != line) {
				if (line != 0xFFFFFFFF)
					__dcbf(0, p->pixels + line);
				line = off & ~127;
				__dcbf(0, p->pixels + line);
				__sync();
			}
			*(volatile DWORD*)at = StampPixel(stamp);
		}
	}
	if (g_building)
		return;
	if (line != 0xFFFFFFFF)
		__dcbf(0, p->pixels + line);
	__sync();
}

static VOID DrawLines(Plan* p, DWORD x, DWORD y, const CHAR* text, DWORD scale, DWORD pitch)
{
	while (*text && y < p->height) {
		DWORD len = 0;
		while (text[len] && text[len] != '\r' && text[len] != '\n')
			len++;
		DWORD w, h;
		if (len && x < p->width && BuildStamp(text, len, scale, p->width - x, &w, &h))
			ApplyStamp(p, x, y, w, h);
		text += len;
		if (*text == '\r')
			text++;
		if (*text == '\n')
			text++;
		y += pitch;
	}
}

// Our own watermark when the game's never shows up (no user signed in, or the
// stage that calls sub_82474AA8 isn't running). Same shape; the fields only
// the game knows are "?".
static VOID FallbackWatermark(CHAR* mark, DWORD markSize, CHAR* hash, DWORD hashSize)
{
	CHAR name[32] = "?";
	XUID xuid = 0;
	for (DWORD user = 0; user < XUSER_MAX_COUNT; user++) {
		if (XUserGetSigninState(user) != eXUserSigninState_NotSignedIn) {
			XUserGetName(user, name, sizeof(name));
			XUserGetXUID(user, &xuid);
			break;
		}
	}
	DWORD nat = XOnlineGetNatType();
	const char* natName = nat == XONLINE_NAT_OPEN ? "Open" : nat == XONLINE_NAT_MODERATE ? "Moderate" : nat == XONLINE_NAT_STRICT ? "Strict" : "?";
	SYSTEMTIME st;
	GetLocalTime(&st);
	sprintf_s(mark, markSize, "PRE-ALPHA BUILD (?) (logged ?) (NAT:%s)\r\n%s @ %02u/%02u/%04u %02u:%02u:%02u",
		natName, name, st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond);
	ULONGLONG random = (ULONGLONG)GetTickCount() * 0x0016A64CB1582E5FULL + 0x6A70D893FE726A92ULL;
	sprintf_s(hash, hashSize, "%016llx\r\n%016llx", random, xuid ^ random ^ 0xAEA9434DAEA9434DULL);
}

// Draws the three text blocks into the plan's buffer. FALSE on a fault.
static BOOL DrawInto(Plan* p)
{
	CHAR mark[MARK_SIZE], hash[HASH_SIZE], status[256];
	static BOOL fallbackLogged = FALSE;

	EnterCriticalSection(&g_lock);
	DWORD markTick = g_markTick;
	memcpy(mark, g_markText, sizeof(mark));
	memcpy(hash, g_hashText, sizeof(hash));
	LeaveCriticalSection(&g_lock);

	DWORD now = GetTickCount();
	// The watermark belongs to a running capture; between captures only the
	// build line and the combo hint are drawn.
	if (!g_active) {
		mark[0] = hash[0] = 0;
	}
	else if (!markTick || now - markTick > MARK_FALLBACK_MS) {
		if (now - g_onTick > MARK_FALLBACK_MS) {
			if (!fallbackLogged) {
				Sunrise_Dbg("[capture] overlay: no watermark text from sub_82474AA8, formatting our own");
				fallbackLogged = TRUE;
			}
			FallbackWatermark(mark, sizeof(mark), hash, sizeof(hash));
		}
		else {
			mark[0] = hash[0] = 0;
		}
	}
	if (!g_active) {
		sprintf_s(status, sizeof(status),
			"packet capture inactive\r\nhold L3 + R3 + Y and tap LB to begin capture");
	}
	else {
		// The time since this capture started: milliseconds raw, to line up
		// with the pcap, then the same span broken out for reading.
		DWORD elapsed = now - g_onTick;
		DWORD seconds = elapsed / 1000;
		sprintf_s(status, sizeof(status),
			"capturing packets (%u sent, %u received, %u captures saved)\r\n"
			"%u ms (%uh %um %us) | current slice %d\r\n"
			"hold L3 + R3 + Y and tap LB to end capture",
			g_sent, g_received, g_saved,
			elapsed, seconds / 3600, (seconds / 60) % 60, seconds % 60,
			CurrentSliceSet());
	}

	DWORD scale = p->height >= 1000 ? 2 : 1;
	DWORD pitch = CaptureFontLineHeight() * scale;
	__try {
		DrawLines(p, p->width * 5 / 100, p->height * 5 / 100, status, scale, pitch);
		DrawLines(p, p->width * 2 / 10, p->height * 8 / 10, mark, scale, pitch);
		DrawLines(p, p->width * 8 / 10, p->height * 8 / 10, hash, scale, pitch);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return FALSE;
	}
	if (g_drawCount++ == 0)
		Sunrise_Dbg("[capture] overlay: first frame drawn");
	return TRUE;
}

// Lays this frame's text out into the spare paint list and publishes it. Same
// geometry for every front buffer, so the swap interrupt can apply it to
// whichever one goes live. Nothing is written to the buffer here.
static BOOL BuildPaint(Plan* p)
{
	DWORD spare = g_paintLive == 0 ? 1 : 0;
	g_building = &g_paint[spare];
	g_building->count = 0;
	BOOL ok = DrawInto(p);
	g_building = NULL;
	if (!ok)
		return FALSE;
	g_paintLive = spare;
	if (g_paint[spare].count == PAINT_MAX)
		Sunrise_Trace("[capture] overlay: paint list full at %u pixels, text clipped", PAINT_MAX);
	return TRUE;
}

// The front buffer the swap interrupt was handed, by physical base. Plans are
// only written by the render thread and only published complete, so reading
// them without the lock is safe enough for an interrupt.
static const Plan* PlanForPhys(DWORD phys)
{
	for (DWORD i = 0; i < ARRAYSIZE(g_plans); i++)
		if (g_plans[i].used && g_plans[i].pixels && g_plans[i].phys == phys)
			return &g_plans[i];
	return NULL;
}

// --- Swap hook ------------------------------------------------------------------------------

// D3DDevice_Swap sub_831AAF18(device, front buffer texture, scaler params).
// Passthrough unless a capture is drawing.
DWORD SwapHook(DWORD device, DWORD frontBuffer, DWORD params)
{
	DWORD result = ((D3DSwap_t)o_swap)(device, frontBuffer, params);

	if (!g_on || g_failed)
		return result;

	if (!Readable(device + DEV_SWAPS_QUEUED, 4) || !Readable(device + DEV_SWAPS_REACHED, 4) ||
		!Readable(frontBuffer + TEXTURE_FETCH, 24)) {
		if (++g_failStreak >= FAIL_STREAK)
			Fail("device or front buffer pointer not readable");
		return result;
	}
	DWORD queued = *(volatile DWORD*)(device + DEV_SWAPS_QUEUED);
	DWORD reached = *(volatile DWORD*)(device + DEV_SWAPS_REACHED);
	if (queued - reached > 16) {
		if (++g_failStreak >= FAIL_STREAK)
			Fail("D3D swap counters at device+0x4248/+0x424C look wrong");
		return result;
	}

	// With the swap interrupt hooked the text goes in there, the moment a buffer
	// is handed to the scanout, so nothing is written to the buffer here: this
	// only lays the pixels out for it. The geometry of the buffer just handed to
	// Swap stands in for all of them, which is fair - they only differ by base.
	BOOL drewOrIdle = TRUE;
	if (g_isrHooked) {
		DWORD fetch[6];
		memcpy(fetch, (const void*)(frontBuffer + TEXTURE_FETCH), sizeof(fetch));
		Plan* p = PlanFor(fetch);
		if (!p || !BuildPaint(p))
			drewOrIdle = FALSE;
		g_pendingHead = g_pendingCount = 0;
	}
	else {
		// Only the swap the GPU has just reached is on screen. Anything older
		// has already been replaced and is back in the game's pool, so drawing
		// into it paints nothing and can land in a buffer the GPU is rendering
		// into: drop those without touching them.
		while (g_pendingCount) {
			Pending* e = &g_pending[g_pendingHead];
			if ((int)(reached - e->seq) < 0)
				break;
			if (e->seq == reached) {
				Plan* p = PlanFor(e->fetch);
				if (!p || !DrawInto(p))
					drewOrIdle = FALSE; // unusable or undrawable front buffer
			}
			g_pendingHead = (g_pendingHead + 1) % PENDING_MAX;
			g_pendingCount--;
		}
	}
	if (drewOrIdle)
		g_failStreak = 0;
	else if (++g_failStreak >= FAIL_STREAK)
		Fail("front buffer not drawable (format, size or mapping)");

	if (!g_isrHooked) {
		// Record this swap. The oldest goes only if the GPU is further behind
		// than its own counters are supposed to allow.
		if (g_pendingCount == PENDING_MAX) {
			g_pendingHead = (g_pendingHead + 1) % PENDING_MAX;
			g_pendingCount--;
		}
		Pending* slot = &g_pending[(g_pendingHead + g_pendingCount) % PENDING_MAX];
		memcpy(slot->fetch, (const void*)(frontBuffer + TEXTURE_FETCH), sizeof(slot->fetch));
		slot->seq = queued;
		g_pendingCount++;
	}
	return result;
}

// --- swap interrupt hook --------------------------------------------------------------------

// sub_831AA0E8(packed) is the GPU swap interrupt: it bumps device+0x424C and,
// when the swap is not deferred, programs the buffer's physical base into the
// display register at 0x7FC86110. The low bits of its argument are flags, the
// top ones that same base, so this is the one moment where the buffer about to
// be scanned out is known and its resolve is already done. Painting here is
// what stops the text flickering: written before the buffer is on screen and
// never touched again until the game renders into it.
//
// Interrupt context, so: no lock, no allocation, no logging, no SEH, and only
// the stores from a list the render thread already laid out.
VOID SwapInterruptHook(DWORD packed)
{
	((SwapIsr_t)o_swapInterrupt)(packed);

	if (!g_on || g_failed)
		return;
	DWORD phys = (packed & 0xFFFFF000) & 0x1FFFFFFF;
	if (!phys)
		return; // a deferred swap programs nothing
	DWORD live = g_paintLive;
	if (live > 1)
		return;
	const PaintList* list = &g_paint[live];
	const Plan* p = PlanForPhys(phys);
	if (!p)
		return;

	DWORD line = 0xFFFFFFFF;
	for (DWORD i = 0; i < list->count; i++) {
		DWORD pixel = list->pixel[i];
		DWORD off = pixel & ~3;
		if ((off & ~127) != line) {
			if (line != 0xFFFFFFFF)
				__dcbf(0, p->pixels + line);
			line = off & ~127;
			__dcbf(0, p->pixels + line);
			__sync();
		}
		*(volatile DWORD*)(p->pixels + off) = (pixel & 1) ? 0xFFFFFFFF : 0x00000000;
	}
	if (line != 0xFFFFFFFF)
		__dcbf(0, p->pixels + line);
	__sync();
}

// --- watermark hook -------------------------------------------------------------------------

// A macro, not a call: the scrape below must make no call before the copy.
#define IS_LOWER_HEX(c) (((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'f'))

// sub_82474AA8 formats the watermark into its own stack frame and returns (the
// draw calls are gone). The frame sat just below ours, so the text is still
// there on return. Everything up to the copy is plain loops, no calls.
VOID WatermarkHook()
{
	((WatermarkFormat_t)o_watermark)();
	if (!g_on || g_failed)
		return;

	volatile DWORD anchor = 0;
	DWORD top = (DWORD)&anchor & ~3;
	const CHAR* mark = NULL;
	for (DWORD i = 0; i <= 0x200 / 4 && !mark; i++) {
		DWORD delta = i == 0 ? g_scanDelta : (i - 1) * 4;
		if (i > 0 && delta == g_scanDelta)
			continue;
		const CHAR* q = (const CHAR*)(top - MARK_FROM_TOP - delta);
		if (q[0] == 'P' && q[1] == 'R' && q[2] == 'E' && q[3] == '-' && q[4] == 'A' && q[5] == 'L' &&
			q[6] == 'P' && q[7] == 'H' && q[8] == 'A' && q[9] == ' ' && q[10] == 'B') {
			mark = q;
			g_scanDelta = delta;
		}
	}
	if (!mark)
		return;

	DWORD n = 0;
	for (; n < MARK_SIZE - 1 && mark[n]; n++) {
		CHAR c = mark[n];
		if (!(c == '\r' || c == '\n' || (c >= 0x20 && c < 0x7F)))
			return;
		g_scanMark[n] = c;
	}
	if (n == MARK_SIZE - 1)
		return;
	g_scanMark[n] = 0;

	const CHAR* hash = mark - HASH_BELOW_MARK;
	for (n = 0; n < 34; n++) {
		CHAR c = hash[n];
		BOOL ok = (n == 16) ? c == '\r' : (n == 17) ? c == '\n' : IS_LOWER_HEX(c);
		if (!ok)
			break;
		g_scanHash[n] = c;
	}
	g_scanHash[n == 34 && hash[34] == 0 ? 34 : 0] = 0;

	// Calls are fine from here on.
	DWORD tick = GetTickCount();
	EnterCriticalSection(&g_lock);
	memcpy(g_markText, g_scanMark, sizeof(g_markText));
	memcpy(g_hashText, g_scanHash, sizeof(g_hashText));
	g_markTick = tick ? tick : 1;
	LeaveCriticalSection(&g_lock);
}

// --- API ------------------------------------------------------------------------------------

static VOID Begin(BOOL swapHooked, BOOL markHooked, BOOL isrHooked, BOOL capturing)
{
	g_isrHooked = isrHooked;
	if (!g_ready) {
		InitializeCriticalSection(&g_lock);
		g_ready = TRUE;
	}
	g_swapHooked = swapHooked;
	g_markHooked = markHooked;

	if (!swapHooked) {
		Sunrise_Dbg("[capture] overlay: Swap hook not installed, no on-screen overlay");
		return;
	}
	if (g_failed)
		return;
	if (!CaptureFontInitialize()) {
		Fail("fallback font data did not validate");
		return;
	}

	EnterCriticalSection(&g_lock);
	ZeroMemory(g_plans, sizeof(g_plans));
	g_nextPlan = 0;
	g_pendingHead = g_pendingCount = 0;
	g_markTick = 0;
	g_failStreak = 0;
	g_onTick = GetTickCount();
	g_paintLive = 2; // nothing for the interrupt to paint until a frame is laid out
	g_paint[0].count = g_paint[1].count = 0;
	if (markHooked)
		*WATERMARK_GATE = capturing ? 1 : 0;
	g_active = capturing;
	g_on = TRUE;
	LeaveCriticalSection(&g_lock);
	Sunrise_Dbg("[capture] overlay %s%s", capturing ? "on" : "armed",
		markHooked ? "" : " (self-formatted watermark)");
}

// Drawing starts as soon as the hooks are in, so the combo hint is on screen
// before the first capture.
VOID OverlayArm(BOOL swapHooked, BOOL markHooked, BOOL isrHooked)
{
	Begin(swapHooked, markHooked, isrHooked, FALSE);
}

VOID OverlayOnStart(BOOL swapHooked, BOOL markHooked, BOOL isrHooked)
{
	Begin(swapHooked, markHooked, isrHooked, TRUE);
}

// Drawing carries on between captures, showing the combo hint, so the Swap
// hook keeps running as long as it is installed.
VOID OverlayOnStop()
{
	g_active = FALSE;
	if (!g_ready)
		return;
	EnterCriticalSection(&g_lock);
	if (g_markHooked)
		*WATERMARK_GATE = 0;
	g_markTick = 0;
	LeaveCriticalSection(&g_lock);
}

// Order matters: drawing is switched off and the switch made visible before
// anything the interrupt reads is cleared. An interrupt already inside the
// paint loop cannot be called back, which is the one race left here.
VOID OverlayShutdown()
{
	g_on = FALSE;
	g_active = FALSE;
	__sync();
	g_paintLive = 2;
	g_paint[0].count = g_paint[1].count = 0;
	ZeroMemory(g_plans, sizeof(g_plans));
	g_nextPlan = 0;
	g_pendingHead = g_pendingCount = 0;
	g_isrHooked = FALSE;
	g_swapHooked = FALSE;
	g_markHooked = FALSE;
}

VOID OverlaySetStatus(DWORD sent, DWORD received, DWORD saved)
{
	g_sent = sent;
	g_received = received;
	g_saved = saved;
}

BOOL OverlayFailed()
{
	return g_failed || !g_swapHooked;
}
