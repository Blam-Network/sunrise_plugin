#pragma once

#ifndef CAPTUREFONT_H
#define CAPTUREFONT_H
#include "stdafx.h"

// Bungie's fallback font "fixedsys-9" for the capture overlay
// (CaptureFont.cpp). ASCII only. All storage is static; no allocation.

#define CAPTURE_FONT_CHARACTERS 128
#define CAPTURE_GLYPH_MAX_PIXELS 256

struct CaptureGlyph
{
	int advance;         // character_width
	int left;            // initial_offset, added before drawing and to the advance
	int originY;         // bitmap_origin_y, rows above the baseline
	int width, height;   // bitmap size
	const BYTE* alpha;   // width * height, 0..15, NULL when the font has no glyph
};

// Parses and unpacks the font once. FALSE if the data doesn't validate.
BOOL CaptureFontInitialize();

// NULL when the character has no glyph.
const CaptureGlyph* CaptureFontGlyph(DWORD character);

// Baseline below the top of a line, and the distance between lines.
int CaptureFontAscent();
int CaptureFontLineHeight();

#endif
