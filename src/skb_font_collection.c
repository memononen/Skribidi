// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "skb_font_collection.h"
#include "skb_font_collection_internal.h"

#include <assert.h>
#include <float.h>
#include <string.h>

#include "SheenBidi/SBScript.h"
#include "hb.h"
#include "hb-ot.h"

//
// Fonts
//

typedef struct skb__sb_tag_array_t {
	uint8_t* tags;
	int32_t tags_count;
	int32_t tags_cap;
} skb__sb_tag_array_t;


static void skb__add_unique(skb__sb_tag_array_t* script_tags, uint8_t sb_script)
{
	for (int32_t i = 0; i < script_tags->tags_count; i++) {
		if (script_tags->tags[i] == sb_script)
			continue;
	}
	SKB_ARRAY_RESERVE(script_tags->tags, script_tags->tags_count+1);
	script_tags->tags[script_tags->tags_count++] = sb_script;
}

static void skb__add_unique_script_from_ot_tag(skb__sb_tag_array_t* script_tags, uint32_t ot_script_tag)
{
	// TODO: we could make a lookup table and binary search based on ot_script_tag, instead of going through the hoops each time.

	// Brute force over all SBScripts
	static const uint8_t sb_last_script_index = 0xab; // This is highest SBScript value.
	for (uint8_t sb_script = 0; sb_script < sb_last_script_index; sb_script++) {
		// SBScript -> ISO-15924
		uint32_t unicode_tag = SBScriptGetUnicodeTag(sb_script);
		// ISO-15924 -> hb_script_t
		hb_script_t hb_script = hb_script_from_iso15924_tag(unicode_tag);
		// hb_script_t -> all possible Opentype scripts
		hb_tag_t ot_script_tags[2];
		unsigned int ot_script_tags_count = 2;
		hb_ot_tags_from_script_and_language(hb_script, NULL, &ot_script_tags_count, ot_script_tags, NULL, NULL);
		for (unsigned int i = 0; i < ot_script_tags_count; i++) {
			if (ot_script_tags[i] == ot_script_tag) {
				// Found match, store the matching SBScript.
				skb__add_unique(script_tags, sb_script);
				break;
			}
		}
	}
}

static void skb__append_tags_from_table(hb_face_t* face, hb_tag_t table_tag, skb__sb_tag_array_t* scripts)
{
	hb_tag_t tags[32];
	uint32_t offset = 0;
	uint32_t tags_count = 32;
	while (tags_count == 32) {
		tags_count = 32;
		tags_count = hb_ot_layout_table_get_script_tags(face, table_tag, offset, &tags_count, tags);

		for (uint32_t i = 0; i < tags_count; i++)
			skb__add_unique_script_from_ot_tag(scripts, tags[i]);

		offset += tags_count;
	}
}

static void skb__append_tags_from_unicodes(hb_face_t* face, skb__sb_tag_array_t* scripts)
{
	hb_set_t* unicodes = hb_set_create();
	hb_face_collect_unicodes(face, unicodes);

	hb_unicode_funcs_t* unicode_funcs = hb_unicode_funcs_get_default();

	// To save us testing the script of each individual glyph, we just sample the first and last glyph in the range.
	hb_codepoint_t first = HB_SET_VALUE_INVALID;
	hb_codepoint_t last = HB_SET_VALUE_INVALID;
	while (hb_set_next_range (unicodes, &first, &last)) {

		int32_t unicode_count = 0;
		hb_script_t unicode_scripts[2];
		unicode_scripts[unicode_count++] = hb_unicode_script(unicode_funcs, first);
		if (first != last) {
			unicode_scripts[unicode_count++] = hb_unicode_script(unicode_funcs, last);
			if ((uint32_t)unicode_scripts[unicode_count-1] == (uint32_t)unicode_scripts[unicode_count-2])
				unicode_count--;
		}

		for (int32_t j = 0; j < unicode_count; j++) {
			hb_tag_t ot_scripts[4];
			uint32_t ot_scripts_count = 4;
			hb_ot_tags_from_script_and_language (unicode_scripts[j], HB_LANGUAGE_INVALID, &ot_scripts_count, ot_scripts, NULL, NULL);

			for (uint32_t i = 0; i < ot_scripts_count; i++)
				skb__add_unique_script_from_ot_tag(scripts, ot_scripts[i]);
		}
	}

	hb_set_destroy(unicodes);
}


static skb_font_t* skb__font_create(const char* path, uint8_t font_family)
{
	hb_blob_t* blob = NULL;
	hb_face_t* face = NULL;
	skb_font_t* font = NULL;

	skb__sb_tag_array_t scripts = {0};

	skb_debug_log("Loading font: %s\n", path);

	// Use Harfbuzz to load the font data, it uses mmap when possible.
	blob = hb_blob_create_from_file(path);
	if (blob == hb_blob_get_empty()) goto error;

	face = hb_face_create(blob, 0);
	hb_blob_destroy(blob);
	if (!face) goto error;

	// Get how many points per EM, used to scale font size.
	unsigned int upem = hb_face_get_upem(face);

	bool has_paint = hb_ot_color_has_paint(face);
	bool has_layers = hb_ot_color_has_layers(face);

	// Try to get script tags from tables.
	skb__append_tags_from_table(face, HB_OT_TAG_GSUB, &scripts);
	skb__append_tags_from_table(face, HB_OT_TAG_GPOS, &scripts);

	// If the tables did not define the scripts, fallback to checking the supported glyph ranges.
	if (scripts.tags_count == 0)
		skb__append_tags_from_unicodes(face, &scripts);

	hb_font_t* hb_font = hb_font_create(face);
	hb_face_destroy(face);

	const float italic = hb_style_get_value(hb_font, HB_STYLE_TAG_ITALIC);
	const float slant = hb_style_get_value(hb_font, HB_STYLE_TAG_SLANT_RATIO);
	const float weight = hb_style_get_value(hb_font, HB_STYLE_TAG_WEIGHT);
	const float width = hb_style_get_value(hb_font, HB_STYLE_TAG_WIDTH);

	if (!hb_font) goto error;

	font = (skb_font_t*)skb_malloc(sizeof(skb_font_t));
	memset(font, 0, sizeof(skb_font_t));

	font->upem = (int)upem;
	font->upem_scale = 1.f / (float)upem;

	if (italic > 0.1f)
		font->style = SKB_FONT_STYLE_ITALIC;
	else if (slant > 0.01f)
		font->style = SKB_FONT_STYLE_OBLIQUE;
	else
		font->style = SKB_FONT_STYLE_NORMAL;

	font->weight = (uint16_t)weight;

	font->stretch = width / 100.f;

	// Save HB font
	font->hb_font = hb_font;

	// Store name
	size_t path_len = strlen(path);
	font->name = skb_malloc(path_len+1);
	memcpy(font->name, path, path_len+1); // copy null term.
	font->name_hash = skb_hash64_append_str(skb_hash64_empty(), font->name);

	// Store supported scripts
	font->scripts = scripts.tags;
	font->scripts_count = scripts.tags_count;

	font->font_family = font_family;

	// Leaving this debug log here, as it has been often needed.
//	for (uint32_t i = 0; i < font->scripts_count; i++)
//		skb_debug_log(" - script: %c%c%c%c\n", HB_UNTAG(SBScriptGetOpenTypeTag(font->scripts[i])));

	// Store metrics
	hb_font_extents_t extents;
	if (hb_font_get_h_extents(font->hb_font, &extents)) {
		font->metrics.ascender = -(float)extents.ascender * font->upem_scale;
		font->metrics.descender = -(float)extents.descender * font->upem_scale;
		font->metrics.line_gap = (float)extents.line_gap * font->upem_scale;
	}

	hb_position_t x_height;
	if (hb_ot_metrics_get_position (font->hb_font, HB_OT_METRICS_TAG_X_HEIGHT, &x_height)) {
		font->metrics.x_height = -(float)x_height * font->upem_scale;
	}

	// Caret metrics
	hb_position_t caret_offset;
	hb_position_t caret_rise;
	hb_position_t caret_run;

	if (hb_ot_metrics_get_position (font->hb_font, HB_OT_METRICS_TAG_HORIZONTAL_CARET_OFFSET, &caret_offset)
		&& hb_ot_metrics_get_position (font->hb_font, HB_OT_METRICS_TAG_HORIZONTAL_CARET_RISE, &caret_rise)
		&& hb_ot_metrics_get_position (font->hb_font, HB_OT_METRICS_TAG_HORIZONTAL_CARET_RUN, &caret_run)) {

		font->caret_metrics.offset = (float)caret_offset * font->upem_scale;
		font->caret_metrics.slope = (float)caret_run / (float)caret_rise;

	} else {
		font->caret_metrics.offset = 0.f;
		font->caret_metrics.slope = 0.f;
	}

	return font;

error:
	hb_face_destroy(face);
	skb_free(scripts.tags);

	return NULL;
}

static void skb__font_destroy(skb_font_t* font)
{
	if (!font) return;
	skb_free(font->name);
	skb_free(font->scripts);
	hb_font_destroy(font->hb_font);
	skb_free(font);
}

skb_font_collection_t* skb_font_collection_create(void)
{
	static uint32_t id = 0;

	skb_font_collection_t* result = skb_malloc(sizeof(skb_font_collection_t));
	memset(result, 0, sizeof(skb_font_collection_t));

	result->id = ++id;

	return result;
}

void skb_font_collection_destroy(skb_font_collection_t* font_collection)
{
	if (!font_collection) return;
	for (int32_t i = 0; i < font_collection->fonts_count; i++) {
		if (font_collection->fonts[i])
			skb__font_destroy(font_collection->fonts[i]);
	}
	skb_free(font_collection);
}

skb_font_t* skb_font_collection_add_font(skb_font_collection_t* font_collection, const char* file_name, uint8_t font_family)
{
	SKB_ARRAY_RESERVE(font_collection->fonts, font_collection->fonts_count+1);

	skb_font_t* font = skb__font_create(file_name, font_family);
	if (font) {
		assert(font_collection->fonts_count <= 255);
		font->idx = (uint8_t)font_collection->fonts_count;
		font_collection->fonts[font_collection->fonts_count++] = font;
	}
	return font;
}

static float g_stretch_to_value[] = {
	1.0f, // SKB_FONT_STRETCH_NORMAL
	0.5f, // SKB_FONT_STRETCH_ULTRA_CONDENSED
	0.625f, // SKB_FONT_STRETCH_EXTRA_CONDENSED
	0.75f, // SKB_FONT_STRETCH_CONDENSED
	0.875f, // SKB_FONT_STRETCH_SEMI_CONDENSED
	1.125f, // SKB_FONT_STRETCH_SEMI_EXPANDED
	1.25f, // SKB_FONT_STRETCH_EXPANDED
	1.5f, // SKB_FONT_STRETCH_EXTRA_EXPANDED
	2.0f, // SKB_FONT_STRETCH_ULTRA_EXPANDED
};

static bool skb__supports_script(const skb_font_t* font, uint8_t script)
{
	for (int32_t script_idx = 0; script_idx < font->scripts_count; script_idx++) {
		if (font->scripts[script_idx] == script)
			return true;
	}
	return false;
}

int32_t skb_font_collection_match_fonts(
	const skb_font_collection_t* font_collection,
	const uint8_t requested_script, uint8_t requested_font_family,
	skb_font_style_t requested_style, skb_font_stretch_t requested_stretch, uint16_t requested_weight,
	const skb_font_t** results, int32_t results_cap)
{
	// Based on https://drafts.csswg.org/css-fonts-3/#font-style-matching

	int32_t candidates_count = 0;
	int32_t current_candidates_count = 0;
	bool multiple_stretch = false;
	bool multiple_styles = false;
	bool multiple_weights = false;

	// Match script and font family.
	for (int32_t font_idx = 0; font_idx < font_collection->fonts_count; font_idx++) {
		const skb_font_t* font = font_collection->fonts[font_idx];
		if (font->font_family == requested_font_family
			&& (requested_font_family == SKB_FONT_FAMILY_EMOJI || skb__supports_script(font, requested_script))) { // Ignore script for emoji fonts, as emojis are the same on each writing system.
			if (candidates_count < results_cap) {
				if (candidates_count > 0) {
					const skb_font_t* prev_font = results[candidates_count - 1];
					multiple_stretch |= !skb_equalsf(prev_font->stretch, font->stretch, 0.01f);
					multiple_styles |= prev_font->style != font->style;
					multiple_weights |= prev_font->weight != font->weight;
				}
				results[candidates_count++] = font;
			}
		}
	}

	if (!candidates_count)
		return 0;

	// Match stretch.
	if (multiple_stretch) {
		float requested_stretch_value = g_stretch_to_value[skb_clampi((int32_t)requested_stretch, 0, SKB_COUNTOF(g_stretch_to_value)-1)];

		bool exact_stretch_match = false;
		float nearest_narrow_error = FLT_MAX;
		float nearest_narrow = requested_stretch_value;
		float nearest_wide_error = FLT_MAX;
		float nearest_wide = requested_stretch_value;

		for (int32_t i = 0; i < candidates_count; i++) {
			const skb_font_t* font = results[i];
			if (skb_equalsf(requested_stretch_value, font->stretch, 0.01f)) {
				exact_stretch_match = true;
				break;
			}
			const float error = skb_absf(font->stretch - requested_stretch_value);
			if (font->stretch <= 0.f) {
				if (error < nearest_narrow_error) {
					nearest_narrow_error = error;
					nearest_narrow = font->stretch;
				}
			} else {
				if (error < nearest_wide_error) {
					nearest_wide_error = error;
					nearest_wide = font->stretch;
				}
			}
		}

		float selected_stretch = -1.f;
		if (exact_stretch_match) {
			selected_stretch = requested_stretch_value;
		} else {
			if (requested_stretch_value <= 1.f) {
				if (nearest_narrow_error < FLT_MAX)
					selected_stretch = nearest_narrow;
				else if (nearest_wide_error < FLT_MAX)
					selected_stretch = nearest_wide;
			} else {
				if (nearest_wide_error < FLT_MAX)
					selected_stretch = nearest_wide;
				else if (nearest_narrow_error < FLT_MAX)
					selected_stretch = nearest_narrow;
			}
		}

		// Prune out everything but the selected stretch.
		current_candidates_count = candidates_count;
		candidates_count = 0;
		for (int32_t i = 0; i < current_candidates_count; i++) {
			if (!skb_equalsf(selected_stretch, results[i]->stretch, 0.01f))
				continue;
			results[candidates_count++] = results[i];
		}

		if (candidates_count <= 1)
			return candidates_count;
	}

	// Style
	if (multiple_styles) {
		int32_t normal_count = 0;
		int32_t italic_count = 0;
		int32_t oblique_count = 0;
		for (int32_t i = 0; i < candidates_count; i++) {
			uint8_t style = results[i]->style;
			if (style == SKB_FONT_STYLE_NORMAL)
				normal_count++;
			if (style == SKB_FONT_STYLE_ITALIC)
				italic_count++;
			if (style == SKB_FONT_STYLE_OBLIQUE)
				oblique_count++;
		}

		uint8_t selected_style = SKB_FONT_STYLE_NORMAL;
		if (requested_style == SKB_FONT_STYLE_ITALIC) {
			if (italic_count > 0)
				selected_style = SKB_FONT_STYLE_ITALIC;
			else if (oblique_count > 0)
				selected_style = SKB_FONT_STYLE_OBLIQUE;
			else if (normal_count > 0)
				selected_style = SKB_FONT_STYLE_NORMAL;
		} else if (requested_style == SKB_FONT_STYLE_OBLIQUE) {
			if (oblique_count > 0)
				selected_style = SKB_FONT_STYLE_OBLIQUE;
			else if (italic_count > 0)
				selected_style = SKB_FONT_STYLE_ITALIC;
			else if (normal_count > 0)
				selected_style = SKB_FONT_STYLE_NORMAL;
		} else {
			if (normal_count > 0)
				selected_style = SKB_FONT_STYLE_NORMAL;
			else if (oblique_count > 0)
				selected_style = SKB_FONT_STYLE_OBLIQUE;
			else if (italic_count > 0)
				selected_style = SKB_FONT_STYLE_ITALIC;
		}

		// Prune out everything but the selected style.
		current_candidates_count = candidates_count;
		candidates_count = 0;
		for (int32_t i = 0; i < current_candidates_count; i++) {
			if (results[i]->style != selected_style)
				continue;
			results[candidates_count++] = results[i];
		}

		if (candidates_count <= 1)
			return candidates_count;
	}

	// Font weight
	if (multiple_weights) {
		bool exact_weight_match = false;
		bool has_400 = false;
		bool has_500 = false;
		int32_t nearest_lighter_error = INT32_MAX;
		uint16_t nearest_lighter = requested_weight;
		int32_t nearest_darker_error = INT32_MAX;
		uint16_t nearest_darker = requested_weight;

		for (int32_t i = 0; i < candidates_count; i++) {
			const skb_font_t* font = results[i];
			if (requested_weight == font->weight) {
				exact_weight_match = true;
				break;
			}
			const int32_t error = skb_absi((int32_t)font->weight - (int32_t)requested_weight);
			if (font->weight <= 450) {
				if (error < nearest_lighter_error) {
					nearest_lighter_error = error;
					nearest_lighter = font->weight;
				}
			} else {
				if (error < nearest_darker_error) {
					nearest_darker_error = error;
					nearest_darker = font->weight;
				}
			}
			has_400 |= font->weight == 400;
			has_500 |= font->weight == 500;
		}

		uint16_t selected_weight = 0;
		if (exact_weight_match) {
			selected_weight = requested_weight;
		} else {
			if (requested_weight >= 400 && requested_weight < 450 && has_500) {
				selected_weight = 500;
			} else if (requested_weight >= 450 && requested_weight <= 450 && has_400) {
				selected_weight = 400;
			} else {
				// Nearest
				if (requested_weight <= 450) {
					if (nearest_lighter_error < INT32_MAX)
						selected_weight = nearest_lighter;
					else if (nearest_darker_error < INT32_MAX)
						selected_weight = nearest_darker;
				} else {
					if (nearest_darker_error < INT32_MAX)
						selected_weight = nearest_darker;
					else if (nearest_lighter_error < INT32_MAX)
						selected_weight = nearest_lighter;
				}
			}
		}

		// Prune out everything but the selected weight.
		current_candidates_count = candidates_count;
		candidates_count = 0;
		for (int32_t i = 0; i < current_candidates_count; i++) {
			if (results[i]->weight != selected_weight)
				continue;
			results[candidates_count++] = results[i];
		}
	}

	return candidates_count;
}

const skb_font_t* skb_font_collection_get_default_font(const skb_font_collection_t* font_collection, uint8_t font_family)
{
	const skb_font_t* results[64];
	int32_t results_count = skb_font_collection_match_fonts(
		font_collection, SBScriptLATN, font_family, SKB_FONT_STYLE_NORMAL, SKB_FONT_STRETCH_NORMAL,
		400, results, SKB_COUNTOF( results ) );
	return results_count > 0 ? results[0] : NULL;
}

skb_font_t* skb_font_collection_get_font(const skb_font_collection_t* font_collection, uint8_t font_idx)
{
	assert(font_collection);
	assert((int32_t)font_idx < font_collection->fonts_count);
	return font_collection->fonts[font_idx];
}

uint32_t skb_font_collection_get_id(const skb_font_collection_t* font_collection)
{
	assert(font_collection);
	return font_collection->id;
}

skb_rect2_t skb_font_get_glyph_bounds(const skb_font_t* font, uint32_t glyph_id, float font_size)
{
	if (!font || glyph_id == 0) return (skb_rect2_t) { 0 };

	hb_glyph_extents_t extents;
	if (hb_font_get_glyph_extents(font->hb_font, glyph_id, &extents)) {
		const float scale = font_size * font->upem_scale;
		const float x = (float)extents.x_bearing * scale;
		const float y = -(float)extents.y_bearing * scale;
		const float width = (float)extents.width * scale;
		const float height = -(float)extents.height * scale;

		return (skb_rect2_t) {
			.x = x,
			.y = y,
			.width = width,
			.height = height,
		};
	}
	return (skb_rect2_t) { 0 };
}


static float skb__get_baseline_normalized(const skb_font_t* font, hb_ot_layout_baseline_tag_t baseline_tag, bool is_rtl, hb_script_t script)
{
	hb_position_t coord;
	hb_ot_layout_get_baseline_with_fallback2(font->hb_font, baseline_tag, is_rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR, script, NULL, &coord);
	return -(float)coord * font->upem_scale;
}

skb_font_metrics_t skb_font_get_metrics(const skb_font_t* font)
{
	assert(font);
	return font->metrics;
}

skb_caret_metrics_t skb_font_get_caret_metrics(const skb_font_t* font)
{
	assert(font);
	return font->caret_metrics;
}

hb_font_t* skb_font_get_hb_font(const skb_font_t* font)
{
	assert(font);
	return font->hb_font;
}

float skb_font_get_baseline(const skb_font_t* font, skb_baseline_t baseline, bool is_rtl, uint8_t script, float font_size)
{
	hb_ot_layout_baseline_tag_t baseline_tag = {0};

	uint32_t unicode_tag = SBScriptGetUnicodeTag(script);
	hb_script_t hb_script = hb_script_from_iso15924_tag(unicode_tag);

	const float alphabetic_value = skb__get_baseline_normalized(font, HB_OT_LAYOUT_BASELINE_TAG_ROMAN, is_rtl, hb_script);
	float baseline_value = 0.f;

	switch (baseline) {
	case SKB_BASELINE_ALPHABETIC:
		baseline_value = alphabetic_value;
		break;
	case SKB_BASELINE_IDEOGRAPHIC:
		baseline_value = skb__get_baseline_normalized(font, HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_BOTTOM_OR_LEFT, is_rtl, hb_script);
		break;
	case SKB_BASELINE_CENTRAL:
		baseline_value = skb__get_baseline_normalized(font, HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_CENTRAL, is_rtl, hb_script);
		break;
	case SKB_BASELINE_HANGING:
		baseline_value = skb__get_baseline_normalized(font, HB_OT_LAYOUT_BASELINE_TAG_HANGING, is_rtl, hb_script);
		break;
	case SKB_BASELINE_MATHEMATICAL:
		baseline_value = skb__get_baseline_normalized(font, HB_OT_LAYOUT_BASELINE_TAG_MATH, is_rtl, hb_script);
		break;
	case SKB_BASELINE_MIDDLE:
		baseline_value = font->metrics.x_height * 0.5f;
		break;
	case SKB_BASELINE_TEXT_BOTTOM:
		baseline_value = font->metrics.descender;
		break;
	case SKB_BASELINE_TEXT_TOP:
		baseline_value = font->metrics.ascender;
		break;
	default:
		baseline_value = alphabetic_value;
		break;
	}

	return (baseline_value - alphabetic_value) * font_size;
}
