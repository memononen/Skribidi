// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_FONT_COLLECTION_INTERNAL_H
#define SKB_FONT_COLLECTION_INTERNAL_H

#include <stdint.h>

// harfbuzz forward declarations
typedef struct hb_font_t hb_font_t;

typedef struct skb_font_collection_t {
	uint32_t id;			// ID of the font collection.
	skb_font_t** fonts;		// Array of fonts in the collection.
	int32_t fonts_count;	// Number of font in the collection.
	int32_t fonts_cap;		// Capacity of the fonts array.
} skb_font_collection_t;


// Font flags
/** 1 if the font is color font. */
#define SKB_FONT_IS_COLOR 0x01

typedef struct skb_font_t {
	hb_font_t* hb_font;		// Associate harfbuzz font.
	char* name;				// Name of the font (file name)
	uint64_t name_hash;		// Hash of the name, used as unique identifier.
	int32_t upem;			// units per em square.
	float upem_scale;		// 1 / upem.
	skb_font_metrics_t metrics;	// Font metrics (ascender, etc).
	uint8_t* scripts;		// Supported scripts
	int32_t scripts_count;	// Number of supported scripts
	uint8_t font_family;	// font family identifier.
	uint8_t flags;			// Font flags (use SKB_FONT_* macros).
	uint8_t style;			// Normal, italic, oblique (skb_font_style_t)
	float stretch;			// From 0.5 (ultra condensed) -> 1.0 (normal) -> 2.0 (ultra wide).
	uint16_t weight;		// weight of the font (400 = regular).
	uint8_t idx;			// Font index withing collection.
} skb_font_t;

#endif // SKB_FONT_COLLECTION_INTERNAL_H