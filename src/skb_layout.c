// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdbool.h>

#include "skb_common.h"
#include "skb_common_internal.h"
#include "skb_layout.h"
#include "skb_layout_internal.h"
#include "skb_font_collection_internal.h"
#include "skb_icon_collection.h"

#include "hb.h"
#include "SheenBidi/SheenBidi.h"
#include "graphemebreak.h"
#include "linebreak.h"
#include "wordbreak.h"
#include "budoux.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define SB_SCRIPT_COMMON SBScriptZYYY
#define SB_SCRIPT_INHERITED SBScriptZINH

// Struct used to pass temporary state during layout.
typedef struct skb__layout_build_context_t {
	uint8_t* emoji_types_buffer;
	skb_temp_alloc_t* temp_alloc;
} skb__layout_build_context_t;


//
// Content
//

skb_content_run_t skb_content_run_make_utf8(const char* text, int32_t text_count, skb_attribute_set_t attributes, intptr_t run_id)
{
	return (skb_content_run_t) {
		.type = SKB_CONTENT_RUN_UTF8,
		.run_id = run_id,
		.utf8 = {
			.text = text,
			.text_count = text_count,
		},
		.attributes = attributes,
	};
}


skb_content_run_t skb_content_run_make_utf32(const uint32_t* text, int32_t text_count, skb_attribute_set_t attributes, intptr_t run_id)
{
	return (skb_content_run_t) {
		.type = SKB_CONTENT_RUN_UTF32,
		.run_id = run_id,
		.utf32 = {
			.text = text,
			.text_count = text_count,
		},
		.attributes = attributes,
	};
}


skb_content_run_t skb_content_run_make_object(intptr_t data, float width, float height, skb_attribute_set_t attributes, intptr_t run_id)
{
	return (skb_content_run_t) {
		.type = SKB_CONTENT_RUN_OBJECT,
		.run_id = run_id,
		.object = {
			.data = data,
			.width = width,
			.height = height,
		},
		.attributes = attributes,
	};
}

skb_content_run_t skb_content_run_make_icon(skb_icon_handle_t icon_handle, float width, float height, skb_attribute_set_t attributes, intptr_t run_id)
{
	return (skb_content_run_t) {
		.type = SKB_CONTENT_RUN_ICON,
		.run_id = run_id,
		.icon = {
			.icon_handle = icon_handle,
			.width = width,
			.height = height,
		},
		.attributes = attributes,
	};
}

//
// Utils
//

uint64_t skb_layout_params_hash_append(uint64_t hash, const skb_layout_params_t* params)
{
	hash = skb_hash64_append_uint32(hash, params->font_collection ? skb_font_collection_get_id(params->font_collection) : 0);
	hash = skb_hash64_append_uint32(hash, params->icon_collection ? skb_icon_collection_get_id(params->icon_collection) : 0);
	hash = skb_hash64_append_uint32(hash, params->attribute_collection ? skb_attribute_collection_get_id(params->attribute_collection) : 0);
	hash = skb_hash64_append_float(hash, params->layout_width);
	hash = skb_hash64_append_float(hash, params->layout_height);
	hash = skb_hash64_append_uint8(hash, params->flags);
	hash = skb_attributes_hash_append(hash, params->layout_attributes);

	return hash;
}

static hb_script_t skb__sb_script_to_hb(SBScript script)
{
	const SBUInt32 script_tag = SBScriptGetUnicodeTag(script);
	return hb_script_from_iso15924_tag(script_tag);
}

uint32_t skb_script_to_iso15924_tag(uint8_t script)
{
	return SBScriptGetUnicodeTag(script);
}

typedef struct skb__script_tag_t {
	uint32_t tag;
	uint8_t script;
} skb__script_tag_t;

enum {
	SKB_MAX_SCRIPTS = 0xab+1, // This is highest SBScript value + 1
};

static int32_t skb__compare_script_tags(const void* a, const void* b)
{
	const skb__script_tag_t* sa = a;
	const skb__script_tag_t* sb = b;
	return (int32_t)sa->tag - (int32_t)sb->tag;
}

uint8_t skb_script_from_iso15924_tag(uint32_t script_tag)
{
	// Sheenbidi does not provide script reverse lookup, make one.
	// Create SBScript -> ISO-15924 table, which can be binary searched.
	static skb__script_tag_t script_tags[SKB_MAX_SCRIPTS] = {0};
	static bool initialized = false;
	if (!initialized) {
		for (int32_t sb_script = 0; sb_script < SKB_MAX_SCRIPTS; sb_script++) {
			// SBScript -> ISO-15924
			script_tags[sb_script].tag = SBScriptGetUnicodeTag((uint8_t)sb_script);
			script_tags[sb_script].script = (uint8_t)sb_script;
		}
		// Sort in tag order
		qsort(script_tags, SKB_MAX_SCRIPTS, sizeof(skb__script_tag_t), skb__compare_script_tags);
		initialized = true;
	}

	// Binary search tag.
	int32_t low = 0;
	int32_t high = SKB_MAX_SCRIPTS - 1;
	while (low != high) {
		const int32_t mid = low + (high - low + 1) / 2; // ceil
		if (script_tags[mid].tag > script_tag)
			high = mid - 1;
		else
			low = mid;
	}
	if (script_tags[low].tag == script_tag)
		return script_tags[low].script;

	return SBScriptNil;
}

static bool skb__is_japanese_script(uint8_t script)
{
	return script == SBScriptHANI || script == SBScriptHIRA || script == SBScriptKANA;
}


//
// Itemization
//

typedef struct skb__script_run_iter_t {
	const skb_text_property_t* text_props;
	int32_t pos;
	int32_t end;
} skb__script_run_iter_t;

static skb__script_run_iter_t skb__script_run_iter_make(skb_range_t range, const skb_text_property_t* text_attribs)
{
	skb__script_run_iter_t iter = {
		.text_props = text_attribs,
		.pos = range.start,
		.end = range.end,
	};
	return iter;
}

static bool skb__script_run_iter_next(skb__script_run_iter_t* iter, skb_range_t* run_range, uint8_t* run_script)
{
	if (iter->pos == iter->end)
		return false;

	run_range->start = iter->pos;

	// Find continuous script range.
	uint8_t prev_script = iter->text_props[iter->pos].script;
	while (iter->pos < iter->end) {
		iter->pos++;
		const uint8_t script = iter->pos < iter->end ? iter->text_props[iter->pos].script : 0;
		if (prev_script != script)
			break;
		prev_script = script;
	}

	*run_script = prev_script;
	run_range->end = iter->pos;

	return true;
}


typedef struct skb__text_style_run_iter_t {
	skb_range_t range;
	int32_t content_run_idx;
	int32_t content_runs_end;
	const skb__content_run_t* content_runs;
	int32_t content_runs_count;
} skb__text_style_run_iter_t;

static skb__text_style_run_iter_t skb__text_style_run_iter_make(skb_range_t range, const skb__content_run_t* content_runs, int32_t content_runs_count)
{
	return (skb__text_style_run_iter_t) {
		.range = range,
		.content_run_idx = 0,
		.content_runs_end = content_runs_count,
		.content_runs = content_runs,
		.content_runs_count = content_runs_count,
	};
}

static bool skb__text_style_run_iter_next(skb__text_style_run_iter_t* iter, skb_range_t* range, int32_t* range_content_run_idx)
{
	if (iter->content_run_idx == iter->content_runs_end)
		return false;

	while (iter->content_run_idx < iter->content_runs_end) {
		const skb__content_run_t* attributes = &iter->content_runs[iter->content_run_idx];
		if (attributes->text_range.start > iter->range.end) {
			iter->content_run_idx = iter->content_runs_end;
			return false;
		}
		const skb_range_t shaping_range = {
			.start = skb_maxi(iter->range.start, attributes->text_range.start),
			.end = skb_mini(iter->range.end, attributes->text_range.end),
		};
		iter->content_run_idx++;
		if (shaping_range.start < shaping_range.end) {
			*range = shaping_range;
			*range_content_run_idx = iter->content_run_idx - 1;
			return true;
		}
	}

	return false;
}


static int skb__bidi_run_cmp(const void* a, const void* b)
{
	const SBRun* run_a = a;
	const SBRun* run_b = b;
	return (int)run_a->offset - (int)run_b->offset;
}

static void skb__itemize(skb__layout_build_context_t* build_context, skb_layout_t* layout)
{
	if (!layout->text_count)
		return;

	SBLevel base_level = SBLevelDefaultLTR;
	const skb_text_direction_t base_direction = skb_attributes_get_text_base_direction(layout->params.layout_attributes, layout->params.attribute_collection);
	if (base_direction == SKB_DIRECTION_RTL)
		base_level = 1;
	else if (base_direction == SKB_DIRECTION_LTR)
		base_level = 0;

	SBCodepointSequence codepoint_seq = { SBStringEncodingUTF32, layout->text, layout->text_count };

	// Resolve scripts for codepoints.
	SBScriptLocatorRef script_locator = SBScriptLocatorCreate();
	SBScriptLocatorLoadCodepoints(script_locator, &codepoint_seq);
	while (SBScriptLocatorMoveNext(script_locator)) {
		const SBScriptAgent* agent = SBScriptLocatorGetAgent(script_locator);
		const int32_t run_start = (int32_t)agent->offset;
		const int32_t run_end = (int32_t)(agent->offset + agent->length);
		for (int32_t i = run_start; i < run_end; i++)
			layout->text_props[i].script = agent->script;
	}
	SBScriptLocatorRelease(script_locator);

	// Special case, the text starts with common script, look forward to find the first non-implicit script.
	if (layout->text_count && layout->text_props[0].script == SB_SCRIPT_COMMON) {
		uint8_t prev_script = SBScriptLATN; // Fallback to latin as backup.
		int32_t run_end = 0;
		while (run_end < layout->text_count) {
			if (layout->text_props[run_end].script != SB_SCRIPT_INHERITED && layout->text_props[run_end].script != SB_SCRIPT_COMMON) {
				prev_script = layout->text_props[run_end].script;
				break;
			}
			run_end++;
		}
		for (int32_t i = 0; i < run_end; i++)
			layout->text_props[i].script = prev_script;
	}
	// Inherited and common scripts get resolved to the previous script type.
	for (int32_t i = 1; i < layout->text_count; i++) {
		if (layout->text_props[i].script == SB_SCRIPT_INHERITED || layout->text_props[i].script == SB_SCRIPT_COMMON) {
			layout->text_props[i].script = layout->text_props[i - 1].script;
		}
	}

	build_context->emoji_types_buffer = SKB_TEMP_ALLOC(build_context->temp_alloc, uint8_t, layout->text_count);

	// Iterate over the text until we have processed all paragraphs.
	SBAlgorithmRef bidi_algorithm = SBAlgorithmCreate(&codepoint_seq);
	int32_t paragraph_start = 0;
	while (paragraph_start < layout->text_count) {
		const SBParagraphRef bidi_paragraph = SBAlgorithmCreateParagraph(bidi_algorithm, paragraph_start, INT32_MAX, base_level);
		const int32_t paragraph_length = (int32_t)SBParagraphGetLength(bidi_paragraph);

		// The overal text direction is taken from the first paragraph.
		if (paragraph_start == 0)
			layout->resolved_direction = (SBParagraphGetBaseLevel(bidi_paragraph) & 1) ? SKB_DIRECTION_RTL : SKB_DIRECTION_LTR;

		// Iterate over all the bidi runs.
		const SBLineRef bidi_line = SBParagraphCreateLine(bidi_paragraph, paragraph_start, paragraph_length);
		const SBRun* bidi_line_runs = SBLineGetRunsPtr(bidi_line);
		const int32_t bidi_line_runs_count = (int32_t)SBLineGetRunCount(bidi_line);

		// Sort runs back to logical order.
		qsort((SBRun*)bidi_line_runs, bidi_line_runs_count, sizeof(SBRun), skb__bidi_run_cmp);

		for (int32_t i = 0; i < bidi_line_runs_count; ++i) {
			const SBRun* bidi_run = &bidi_line_runs[i];
			const skb_range_t bidi_range = { .start = (int32_t)bidi_run->offset, .end = (int32_t)(bidi_run->offset + bidi_run->length) };
			const skb_text_direction_t bidi_direction = (bidi_run->level & 1) ? SKB_DIRECTION_RTL : SKB_DIRECTION_LTR;

			// Split bidi runs at shaping style span boundaries.
			skb__text_style_run_iter_t style_iter = skb__text_style_run_iter_make(bidi_range, layout->content_runs, layout->content_runs_count);

			skb_range_t style_range = {0};
			int32_t content_run_idx = 0;
			while (skb__text_style_run_iter_next(&style_iter, &style_range, &content_run_idx)) {
				const skb__content_run_t* content_run = &layout->content_runs[content_run_idx];

				if (content_run->type == SKB_CONTENT_RUN_OBJECT || content_run->type == SKB_CONTENT_RUN_ICON) {
					// Object or icon run.
					SKB_ARRAY_RESERVE(layout->shaping_runs, layout->shaping_runs_count + 1);
					skb__shaping_run_t* shaping_run = &layout->shaping_runs[layout->shaping_runs_count++];
					shaping_run->script = layout->text_props[style_range.start].script;
					shaping_run->text_range = style_range;
					shaping_run->direction = (uint8_t)bidi_direction;
					shaping_run->is_emoji = false;
					shaping_run->content_run_idx = content_run_idx;
					shaping_run->font_handle = 0;
					shaping_run->bidi_level = bidi_run->level;
				} else {
					// Text

					// Split the style run into runs of same script.
					skb__script_run_iter_t script_iter = skb__script_run_iter_make(style_range, layout->text_props);

					skb_range_t script_range = {0};
					uint8_t script = 0;

					while (skb__script_run_iter_next(&script_iter, &script_range, &script)) {
						// Split script range into sequences of emojis or text.
						skb_emoji_run_iterator_t emoji_iter = skb_emoji_run_iterator_make(script_range, layout->text, build_context->emoji_types_buffer);
						skb_range_t text_range = {0};
						bool has_emoji = false;
						while (skb_emoji_run_iterator_next(&emoji_iter, &text_range, &has_emoji)) {

							const uint8_t font_family = has_emoji ? SKB_FONT_FAMILY_EMOJI : skb_attributes_get_font_family(content_run->attributes, layout->params.attribute_collection);
							const skb_weight_t font_weight = skb_attributes_get_font_weight(content_run->attributes, layout->params.attribute_collection);
							const skb_style_t font_style = skb_attributes_get_font_style(content_run->attributes, layout->params.attribute_collection);
							const skb_stretch_t font_stretch = skb_attributes_get_font_stretch(content_run->attributes, layout->params.attribute_collection);
							const char* lang = skb_attributes_get_lang(content_run->attributes, layout->params.attribute_collection);
							const hb_language_t run_lang = hb_language_from_string(lang, -1);

							skb_font_handle_t fonts[32];
							int32_t fonts_count = skb_font_collection_match_fonts(
								layout->params.font_collection, hb_language_to_string(run_lang), script, font_family,
								font_weight, font_style, font_stretch,
								fonts, SKB_COUNTOF(fonts));

							if (fonts_count == 0) {
								// If not fonts found, try the font family's default font.
								fonts[0] = skb_font_collection_get_default_font(layout->params.font_collection, font_family);
								// If still not found, there's nothing we can do, so continue to next run.
								if (!fonts[0])
									continue;
								fonts_count++;
							}

							// Split run based on which font can be used.
							int32_t font_run_start = text_range.start;
							skb_font_handle_t cur_font_handle = 0;

							for (int32_t j = text_range.start; j < text_range.end; j++) {
								// Treat control characters are space for font selection, since fonts dont have glyphs for control chars.
								// Missing glyph would break runs, and cause missing glyphs. During rendering we do treat control chars as spaces too.
								const uint32_t codepoint = layout->text_props[j].flags & SKB_TEXT_PROP_CONTROL ? 32 : layout->text[j];
								skb_font_handle_t font_handle = cur_font_handle;
								if (!skb_font_collection_font_has_codepoint(layout->params.font_collection, cur_font_handle, codepoint)) {
									// Find new font
									for (int32_t k = 0; k < fonts_count; k++) {
										if (skb_font_collection_font_has_codepoint(layout->params.font_collection, fonts[k], codepoint)) {
											font_handle = fonts[k];
											break;
										}
									}
								}
								// If no font supports the codepoint, arbitrarily pick the first one, so that we get at least invalid glyphs as output.
								if (!font_handle)
									font_handle = fonts[0];

								if (font_handle && font_handle != cur_font_handle) {
									// Close the run so far.
									if (j > font_run_start) {
										SKB_ARRAY_RESERVE(layout->shaping_runs, layout->shaping_runs_count + 1);
										skb__shaping_run_t* shaping_run = &layout->shaping_runs[layout->shaping_runs_count++];
										shaping_run->script = script;
										shaping_run->text_range.start = font_run_start;
										shaping_run->text_range.end = j;
										shaping_run->direction = (uint8_t)bidi_direction;
										shaping_run->is_emoji = has_emoji;
										shaping_run->content_run_idx = content_run_idx;
										shaping_run->font_handle = cur_font_handle;
										shaping_run->bidi_level = bidi_run->level;
									}

									font_run_start = j;
									cur_font_handle = font_handle;
								}
							}

							// Close last run.
							if (cur_font_handle) {
								if (text_range.end > font_run_start) {
									SKB_ARRAY_RESERVE(layout->shaping_runs, layout->shaping_runs_count + 1);
									skb__shaping_run_t* shaping_run = &layout->shaping_runs[layout->shaping_runs_count++];
									shaping_run->script = script;
									shaping_run->text_range.start = font_run_start;
									shaping_run->text_range.end = text_range.end;
									shaping_run->direction = (uint8_t)bidi_direction;
									shaping_run->is_emoji = has_emoji;
									shaping_run->content_run_idx = content_run_idx;
									shaping_run->font_handle = cur_font_handle;
									shaping_run->bidi_level = bidi_run->level;
								}
							}
						}
					}
				}
			}
 		}

		SBLineRelease(bidi_line);
		SBParagraphRelease(bidi_paragraph);

		paragraph_start += paragraph_length;
	}

	SBAlgorithmRelease(bidi_algorithm);
}


//
// Shaping
//

enum {
	SKB_MAX_FEATURES = 32,
};

static void skb__add_font_feature(hb_feature_t* features, int32_t* features_count, hb_tag_t tag, uint32_t value)
{
	if (*features_count >= SKB_MAX_FEATURES)
		return;

	features[*features_count] = (hb_feature_t) {
		.tag = tag,
		.value = value,
		.start = HB_FEATURE_GLOBAL_START,
		.end = HB_FEATURE_GLOBAL_END,
	};
	*features_count = *features_count + 1;
}

static void skb__collect_font_features(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection, hb_feature_t* features, int32_t* features_count)
{
	const skb_attribute_t* results[SKB_MAX_FEATURES];
	const int32_t count = skb_attributes_get_by_kind(attributes, collection, SKB_ATTRIBUTE_FONT_FEATURE, results, SKB_MAX_FEATURES);

	for (int32_t i = 0; i < count; i++)
		skb__add_font_feature(features, features_count, results[i]->font_feature.tag, results[i]->font_feature.value);
}

static void skb__shape_run(
	skb__layout_build_context_t* build_context,
	skb_layout_t* layout,
	skb__shaping_run_t* shaping_run,
	const skb__content_run_t* content_run,
	hb_buffer_t* buffer,
	const skb_font_handle_t* fonts,
	int32_t fonts_count,
	int32_t font_idx)
{
	assert(fonts_count > 0);

	const char* lang = skb_attributes_get_lang(content_run->attributes, layout->params.attribute_collection);
	const float letter_spacing = skb_attributes_get_letter_spacing(content_run->attributes, layout->params.attribute_collection);
	const float font_size = content_run->font_size;

	const skb_font_t* font = skb_font_collection_get_font(layout->params.font_collection, fonts[font_idx]);
	if (!font)
		return;

	hb_buffer_add_utf32(buffer, layout->text, layout->text_count, shaping_run->text_range.start, shaping_run->text_range.end - shaping_run->text_range.start);

	const hb_language_t hb_lang = hb_language_from_string(lang, -1);

	hb_buffer_set_direction(buffer, skb_is_rtl(shaping_run->direction) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_set_script(buffer, skb__sb_script_to_hb(shaping_run->script));
	hb_buffer_set_language(buffer, hb_lang);

	hb_feature_t features[SKB_MAX_FEATURES];
	int32_t features_count = 0;

	if (skb_absf(letter_spacing) > 0.01f) {
		// Disable ligatures when letter spacing is requested.
		skb__add_font_feature(features, &features_count, SKB_TAG_STR("clig"), 0); // Contextual ligatures
		skb__add_font_feature(features, &features_count, SKB_TAG_STR("dlig"), 0); // Discretionary ligatures
		skb__add_font_feature(features, &features_count, SKB_TAG_STR("rlig"), 0); // Required ligatures
		skb__add_font_feature(features, &features_count, SKB_TAG_STR("liga"), 0); // Standard ligatures
		skb__add_font_feature(features, &features_count, SKB_TAG_STR("hlig"), 0); // Historical ligatures
	}

	// Collect font features from attributes.
 	skb__collect_font_features(content_run->attributes, layout->params.attribute_collection, features, &features_count);

	hb_buffer_flags_t flags = HB_BUFFER_FLAG_DEFAULT;
	if (shaping_run->text_range.start == 0)
		flags |= HB_BUFFER_FLAG_BOT;
	if (shaping_run->text_range.end == layout->text_count)
		flags |= HB_BUFFER_FLAG_EOT;
	hb_buffer_set_flags(buffer, flags);

	hb_shape(font->hb_font, buffer, features, features_count);

	const int32_t glyph_count = (int32_t)hb_buffer_get_length(buffer);
	const hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(buffer, NULL);
	const hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(buffer, NULL);

	// Get info about regular space character, we'll use it for control characters.
	hb_codepoint_t space_gid = 0;
	hb_font_get_glyph (font->hb_font, 0x20 /*space*/, 0, &space_gid);
	hb_position_t space_x_advance = hb_font_get_glyph_h_advance(font->hb_font, space_gid);

	const float scale = font_size * font->upem_scale;

	// Reserve space for the glyphs.
	SKB_ARRAY_RESERVE(layout->glyphs, layout->glyphs_count + glyph_count);
	SKB_ARRAY_RESERVE(layout->clusters, layout->clusters_count + glyph_count);

	shaping_run->glyph_range.start = layout->glyphs_count;
	shaping_run->cluster_range.start = layout->clusters_count;

	// Iterate clusters
	for (int32_t i = 0; i < glyph_count; ) {
		const bool is_control = (layout->text_props[glyph_info[i].cluster].flags & SKB_TEXT_PROP_CONTROL);

		// Figure out cluster of glyphs matching cluster of codepoints. This can be e.g. a ligature matching multiple graphemes, or glyph combination that matches a grapheme cluster.
		int32_t glyph_start = i;
		int32_t glyph_end = i;
		int32_t text_start = 0;
		int32_t text_end = 0;

		// Merge \r\n into one glyph.
		if ((i+1) < glyph_count && layout->text[glyph_info[i].cluster] == SKB_CHAR_CARRIAGE_RETURN && layout->text[glyph_info[i+1].cluster] == SKB_CHAR_LINE_FEED) {
			glyph_start = i+1;
			glyph_end = i+1;
			text_start = (int32_t)glyph_info[i].cluster;
			text_end = (int32_t)glyph_info[i+1].cluster + 1;
			assert(is_control);
		} else {
			// Find current cluster boundary.
			const uint32_t cluster = glyph_info[i].cluster;
			while ((glyph_end+1) < glyph_count && glyph_info[glyph_end+1].cluster == cluster)
				glyph_end++;

			// Figure out the section of text that belongs to this grapheme cluster.
			if (skb_is_rtl(shaping_run->direction)) {
				text_start = (int32_t)glyph_info[glyph_end].cluster;
				text_end = (glyph_start > 0) ? (int32_t)glyph_info[glyph_start-1].cluster : shaping_run->text_range.end;
			} else {
				text_start = (int32_t)glyph_info[glyph_start].cluster;
				text_end = (glyph_end+1 < glyph_count) ? (int32_t)glyph_info[glyph_end+1].cluster : shaping_run->text_range.end;
			}
		}
		assert(text_end >= text_start);

		skb_cluster_t* cluster = &layout->clusters[layout->clusters_count++];
		cluster->text_offset = text_start;
		cluster->text_count = (uint8_t)skb_clampi(text_end - text_start, 0, 255);
		cluster->glyphs_offset = layout->glyphs_count;
		cluster->glyphs_count = (uint8_t)skb_clampi(glyph_end + 1 - glyph_start, 0, 255);

		// Add glyphs
		for (int32_t j = glyph_start; j <= glyph_end; j++) {
			skb_glyph_t* glyph = &layout->glyphs[layout->glyphs_count++];

			if (is_control) {
				// Replace with space character to avoid showing invalid glyph.
				glyph->gid = (uint16_t)space_gid;
				glyph->offset_x = 0.f;
				glyph->offset_y = 0.f;
				glyph->advance_x = (float)space_x_advance * scale;
			} else {
				assert(glyph_info[j].codepoint <= 0xffff);
				glyph->gid = (uint16_t)glyph_info[j].codepoint;
				glyph->offset_x = (float)glyph_pos[j].x_offset * scale;
				glyph->offset_y = -(float)glyph_pos[j].y_offset * scale;
				glyph->advance_x = (float)glyph_pos[j].x_advance * scale;
			}
		}

		i = glyph_end + 1;
	}

	shaping_run->glyph_range.end = layout->glyphs_count;
	shaping_run->cluster_range.end = layout->clusters_count;

	// Reverse clusters to be in logical order
	if (skb_is_rtl(shaping_run->direction)) {
		int32_t i = shaping_run->cluster_range.start;
		int32_t j = shaping_run->cluster_range.end - 1;
		while (i < j) {
			skb_cluster_t tmp = layout->clusters[i];
			layout->clusters[i] = layout->clusters[j];
			layout->clusters[j] = tmp;
			i++;
			j--;
		}
	}

	// Set cluster idx for each glyph.
	for (int32_t i = shaping_run->cluster_range.start; i < shaping_run->cluster_range.end; i++) {
		const skb_cluster_t* cluster = &layout->clusters[i];
		for (int32_t j = 0; j < cluster->glyphs_count; j++)
			layout->glyphs[cluster->glyphs_offset + j].cluster_idx = i;
	}
}

//
// Line Layout
//

static float skb_calculate_line_height(skb_attribute_line_height_t attr_line_height, const skb_font_t* font, float font_size)
{
	const float ascender = font->metrics.ascender * font_size;
	const float descender = font->metrics.descender * font_size;

	if (attr_line_height.type == SKB_LINE_HEIGHT_NORMAL)
		return -ascender + descender;
	if (attr_line_height.type == SKB_LINE_HEIGHT_METRICS_RELATIVE)
		return (-ascender + descender) * attr_line_height.height;
	if (attr_line_height.type == SKB_LINE_HEIGHT_FONT_SIZE_RELATIVE)
		return font_size * attr_line_height.height;
	// SKB_LINE_HEIGHT_ABSOLUTE
	return attr_line_height.height;
}

static float skb__calc_run_end_whitespace(const skb_layout_t* layout, skb_range_t run_range)
{
	if (run_range.start == run_range.end)
		return 0.f;

	const bool layout_is_rtl = skb_is_rtl(layout->resolved_direction);

	float whitespace_width = 0.f;

	if (layout_is_rtl) {
		// Prune space used by whitespace or control characters.
		const skb_layout_run_t* start_run = &layout->layout_runs[run_range.start];
		for (int32_t gi = start_run->glyph_range.start; gi < start_run->glyph_range.end; gi++) {
			const skb_glyph_t* glyph = &layout->glyphs[gi];
			const skb_cluster_t* cluster= &layout->clusters[glyph->cluster_idx];
			if ((layout->text_props[cluster->text_offset].flags & SKB_TEXT_PROP_WHITESPACE) || (layout->text_props[cluster->text_offset].flags & SKB_TEXT_PROP_CONTROL))
				whitespace_width += glyph->advance_x;
			else
				break;
		}
	} else {
		// Prune space used by whitespace or control characters.
		const skb_layout_run_t* end_run = &layout->layout_runs[run_range.end - 1];
		for (int32_t gi = end_run->glyph_range.end-1; gi >= end_run->glyph_range.start; gi--) {
			const skb_glyph_t* glyph = &layout->glyphs[gi];
			const skb_cluster_t* cluster= &layout->clusters[glyph->cluster_idx];
			if (cluster->text_count > 0) {
				if ((layout->text_props[cluster->text_offset].flags & SKB_TEXT_PROP_WHITESPACE) || (layout->text_props[cluster->text_offset].flags & SKB_TEXT_PROP_CONTROL))
					whitespace_width += glyph->advance_x;
				else
					break;
			}
		}
	}

	return whitespace_width;
}

static void skb__calc_run_range_end_points(const skb_layout_t* layout, skb_range_t run_range, float* start_x, float* end_x)
{
	*start_x = 0.f;
	*end_x = 0.f;

	if (run_range.start == run_range.end)
		return;

	const skb_layout_run_t* first_run = &layout->layout_runs[run_range.start];
	const skb_glyph_t* first_glyph = &layout->glyphs[first_run->glyph_range.start];

	const skb_layout_run_t* last_run = &layout->layout_runs[run_range.end - 1];
	const skb_glyph_t* last_glyph = &layout->glyphs[last_run->glyph_range.end - 1];

	*start_x = first_glyph->offset_x;
	*end_x = last_glyph->offset_x + last_glyph->advance_x;

	const bool layout_is_rtl = skb_is_rtl(layout->resolved_direction);
	float white_space = skb__calc_run_end_whitespace(layout, run_range);
	if (layout_is_rtl)
		*start_x += white_space;
	else
		*end_x -= white_space;
}

// Prunes line end in visual order based on direction.
static void skb__prune_line_end(skb_layout_t* layout, skb_layout_line_t* line, float max_width)
{
	const bool remove_from_start = skb_is_rtl(layout->resolved_direction);

	while (line->layout_run_range.start < line->layout_run_range.end) {
		skb_layout_run_t* layout_run = remove_from_start ? &layout->layout_runs[line->layout_run_range.start] : &layout->layout_runs[line->layout_run_range.end - 1];
		if (layout_run->cluster_range.start != layout_run->cluster_range.end) {
			// Clusters are in logical order, need to reverse if run is RTL, since we're removing in visual order.
			const bool run_remove_from_start = (remove_from_start ^ skb_is_rtl(layout_run->direction));
			const skb_cluster_t* cluster = run_remove_from_start ? &layout->clusters[layout_run->cluster_range.start] : &layout->clusters[layout_run->cluster_range.end - 1];
			// Stop when line fits in max width, and there's no white space at the end of the line.
			if (line->bounds.width <= max_width) {
				if ((layout->text_props[cluster->text_offset].flags & SKB_TEXT_PROP_WHITESPACE) == 0 && (layout->text_props[cluster->text_offset].flags & SKB_TEXT_PROP_CONTROL) == 0)
					break;
			}

			// Remove cluster and all of it's glyphs.
			skb_range_t cluster_glyph_range = { . start = cluster->glyphs_offset, .end = cluster->glyphs_offset + cluster->glyphs_count };
			for (int32_t gi = cluster_glyph_range.start; gi < cluster_glyph_range.end; gi++)
				line->bounds.width -= layout->glyphs[gi].advance_x;

			// Remove glyph range
			if (cluster_glyph_range.start == layout_run->glyph_range.start)
				layout_run->glyph_range.start = cluster_glyph_range.end;
			else if (cluster_glyph_range.end == layout_run->glyph_range.end)
				layout_run->glyph_range.end = cluster_glyph_range.start;
			else
				assert(0); // Since we are removing cluster from front or back, we should be able to do the same for the glyphs.

			// Remove cluster
			if (run_remove_from_start) {
				layout_run->cluster_range.start++;
			} else {
				layout_run->cluster_range.end--;
			}
		}
		// Remove run if empty
		if (layout_run->cluster_range.start == layout_run->cluster_range.end) {
			if (remove_from_start) // Removing from end, shift down.
				memmove(&layout->layout_runs[layout_run->glyph_range.start], &layout->layout_runs[layout_run->glyph_range.start+1], layout_run->glyph_range.end - layout_run->glyph_range.start - 1);
			assert(layout->layout_runs_count == line->layout_run_range.end);
			layout->layout_runs_count--;
			line->layout_run_range.end--;
		}
	}
}

static int32_t skb__get_text_run_before(const skb_layout_t* layout, int32_t cur_layout_run_idx)
{
	for (int32_t ri = cur_layout_run_idx - 1; ri >= 0; ri--) {
		const skb_layout_run_t* run = &layout->layout_runs[ri];
		if (run->type == SKB_CONTENT_RUN_UTF8 || run->type == SKB_CONTENT_RUN_UTF32)
			return ri;
	}
	return SKB_INVALID_INDEX;
}

static int32_t skb__get_text_run_after(const skb_layout_t* layout, int32_t cur_layout_run_idx)
{
	for (int32_t ri = cur_layout_run_idx + 1; ri < layout->layout_runs_count; ri++) {
		const skb_layout_run_t* run = &layout->layout_runs[ri];
		if (run->type == SKB_CONTENT_RUN_UTF8 || run->type == SKB_CONTENT_RUN_UTF32)
			return ri;
	}
	return SKB_INVALID_INDEX;
}

static void skb__reorder_runs(skb_layout_t* layout, skb_range_t layout_run_range)
{
	int32_t max_level = 0;
	int32_t min_odd_level = 255;
	for (int32_t i = layout_run_range.start; i < layout_run_range.end; i++) {
		int32_t level = layout->layout_runs[i].bidi_level;
		max_level = skb_maxi(max_level, level);
		if (level & 1)
			min_odd_level = skb_mini(min_odd_level, level);
	}

	for (int32_t level = max_level; level >= min_odd_level; level--) {
		for (int32_t i = layout_run_range.start; i < layout_run_range.end; i++) {
			if (layout->layout_runs[i].bidi_level >= level) {
				int32_t end = i + 1;
				while (end < layout_run_range.end && layout->layout_runs[end].bidi_level >= level)
					end++;

				int32_t j = i;
				int32_t k = end - 1;
				while (j < k) {
					skb_layout_run_t tmp = layout->layout_runs[j];
					layout->layout_runs[j] = layout->layout_runs[k];
					layout->layout_runs[k] = tmp;
					j++;
					k--;
				}

				i = end;
			}
		}
	}
}

static skb_layout_line_t* skb__add_line(skb_layout_t* layout)
{
	if (layout->lines_count > 0) {
		// Return last line if it's empty.
		skb_layout_line_t* last_line = &layout->lines[layout->lines_count - 1];
		if (last_line->layout_run_range.start == last_line->layout_run_range.end)
			return last_line;
	}

	// Allocate new line.
	SKB_ARRAY_RESERVE(layout->lines, layout->lines_count+1);
	skb_layout_line_t* line = &layout->lines[layout->lines_count++];
	SKB_ZERO_STRUCT(line);
	return line;
}

static void skb__update_glyph_range(const skb_layout_t* layout, skb_layout_run_t* layout_run)
{
	if (layout_run->cluster_range.start != layout_run->cluster_range.end) {
		const skb_cluster_t* first_cluster = &layout->clusters[layout_run->cluster_range.start];
		const skb_cluster_t* last_cluster = &layout->clusters[layout_run->cluster_range.end - 1];

		if (first_cluster->glyphs_offset <= last_cluster->glyphs_offset) {
			layout_run->glyph_range.start = first_cluster->glyphs_offset;
			layout_run->glyph_range.end = last_cluster->glyphs_offset + last_cluster->glyphs_count;
		} else {
			layout_run->glyph_range.start = last_cluster->glyphs_offset;
			layout_run->glyph_range.end = first_cluster->glyphs_offset + first_cluster->glyphs_count;
		}
	}
}

// Iterator to iterate over clusters of shaping runs.
typedef struct skb__shaping_run_cluster_iter_t {
	int32_t cluster_idx;
	int32_t cluster_end_idx;
	int32_t shaping_run_idx;
} skb__shaping_run_cluster_iter_t;

skb__shaping_run_cluster_iter_t skb__shaping_run_cluster_iter_make(const skb_layout_t* layout)
{
	const skb_range_t cluster_range = layout->shaping_runs_count > 0 ? layout->shaping_runs[0].cluster_range : (skb_range_t){0};
	return (skb__shaping_run_cluster_iter_t) {
		.cluster_idx = cluster_range.start,
		.cluster_end_idx = cluster_range.end,
		.shaping_run_idx = 0,
	};
}

bool skb__shaping_run_cluster_iter_is_valid(skb__shaping_run_cluster_iter_t* it, const skb_layout_t* layout)
{
	return it->shaping_run_idx < layout->shaping_runs_count;
}

void skb__shaping_run_cluster_iter_next(skb__shaping_run_cluster_iter_t* it, const skb_layout_t* layout)
{
	if (it->shaping_run_idx >= layout->shaping_runs_count)
		return;
	it->cluster_idx++;
	if (it->cluster_idx >= it->cluster_end_idx) {
		it->shaping_run_idx++;
		const skb_range_t cluster_range = (it->shaping_run_idx < layout->shaping_runs_count) ? layout->shaping_runs[it->shaping_run_idx].cluster_range : (skb_range_t){0};
		it->cluster_idx = cluster_range.start;
		it->cluster_end_idx = cluster_range.end;
	}
}

static skb_layout_run_t* skb__line_append_shaping_run(skb_layout_t* layout, skb_layout_line_t* line, skb_layout_run_t* cur_layout_run, const skb__shaping_run_t* shaping_run, skb_range_t cluster_range)
{
	assert(!skb_range_is_empty(cluster_range));

	// Try to append to current run.
	if (cur_layout_run) {
		// Note: we're not using script here as might cause too many splits e.g. for Hira/Hani sequences, which come from same font, but different script.
		// Text direction is important due to how cluster vs glyphs are arranged.
		if (shaping_run->direction == cur_layout_run->direction && shaping_run->font_handle == cur_layout_run->font_handle && shaping_run->content_run_idx == cur_layout_run->content_run_idx) {
			// Must be adjacent to the current run
			if (cluster_range.start == cur_layout_run->cluster_range.end) {
				cur_layout_run->cluster_range.end = cluster_range.end;
				skb__update_glyph_range(layout, cur_layout_run);
				return cur_layout_run;
			}
		}
	}

	SKB_ARRAY_RESERVE(layout->layout_runs, layout->layout_runs_count + 1);
	skb_layout_run_t* layout_run = &layout->layout_runs[layout->layout_runs_count++];
	SKB_ZERO_STRUCT(layout_run);

	if (line->layout_run_range.start == line->layout_run_range.end) {
		line->layout_run_range.start = layout->layout_runs_count - 1;
		line->layout_run_range.end = layout->layout_runs_count;
	} else {
		line->layout_run_range.end = layout->layout_runs_count;
	}
	assert(line->layout_run_range.end == layout->layout_runs_count);

	const skb__content_run_t* content_run = &layout->content_runs[shaping_run->content_run_idx];

	layout_run->type = content_run->type;
	layout_run->direction = shaping_run->direction;
	layout_run->bidi_level = shaping_run->bidi_level;
	layout_run->script = shaping_run->script;
	layout_run->content_run_idx = shaping_run->content_run_idx;

	layout_run->attributes = content_run->attributes;
	layout_run->content_run_id = content_run->run_id;
	layout_run->font_size = content_run->font_size;

	layout_run->cluster_range = cluster_range;
	skb__update_glyph_range(layout, layout_run);

	if (layout_run->type == SKB_CONTENT_RUN_OBJECT || layout_run->type == SKB_CONTENT_RUN_ICON) {
		if (layout_run->type == SKB_CONTENT_RUN_OBJECT)
			layout_run->object_data = content_run->content_data;
		else if (layout_run->type == SKB_CONTENT_RUN_ICON)
			layout_run->icon_handle = (skb_icon_handle_t)content_run->content_data;
	} else {
		layout_run->font_handle = shaping_run->font_handle;
	}

	return layout_run;
}

static skb_layout_run_t* skb__line_append_shaping_run_range(skb_layout_t* layout, skb_layout_line_t* line, skb_layout_run_t* cur_layout_run, skb__shaping_run_cluster_iter_t start_it, skb__shaping_run_cluster_iter_t end_it)
{
	const int32_t shaping_runs_count = end_it.shaping_run_idx - start_it.shaping_run_idx + 1;

	if (shaping_runs_count == 1) {
		const skb__shaping_run_t* shaping_run = &layout->shaping_runs[start_it.shaping_run_idx];
		skb_range_t cluster_range = {
			.start = start_it.cluster_idx,
			.end =  end_it.cluster_idx,
		};
		return skb__line_append_shaping_run(layout, line, cur_layout_run, shaping_run, cluster_range);
	}

	// Start
	const skb__shaping_run_t* start_shaping_run = &layout->shaping_runs[start_it.shaping_run_idx];
	skb_range_t start_cluster_range = {
		.start = start_it.cluster_idx,
		.end =  start_shaping_run->cluster_range.end,
	};
	cur_layout_run = skb__line_append_shaping_run(layout, line, cur_layout_run, start_shaping_run, start_cluster_range);

	// Middle
	for (int32_t i = start_it.shaping_run_idx + 1; i < end_it.shaping_run_idx; i++) {
		const skb__shaping_run_t* shaping_run = &layout->shaping_runs[i];
		cur_layout_run = skb__line_append_shaping_run(layout, line, cur_layout_run, shaping_run, shaping_run->cluster_range);
	}

	// End
	if (end_it.shaping_run_idx < layout->shaping_runs_count) {
		const skb__shaping_run_t* end_shaping_run = &layout->shaping_runs[end_it.shaping_run_idx];
		skb_range_t end_cluster_range = {
			.start = end_shaping_run->cluster_range.start,
			.end =  end_it.cluster_idx,
		};
		if (!skb_range_is_empty(end_cluster_range))
			cur_layout_run = skb__line_append_shaping_run(layout, line, cur_layout_run, end_shaping_run, end_cluster_range);
	}

	return cur_layout_run;
}

static float skb__get_cluster_width(const skb_layout_t* layout, int32_t cluster_idx)
{
	const skb_cluster_t* cluster = &layout->clusters[cluster_idx];
	float cluster_width = 0.f;
	for (int32_t gi = 0; gi < cluster->glyphs_count; gi++)
		cluster_width += layout->glyphs[cluster->glyphs_offset + gi].advance_x;
	return cluster_width;
}

enum { SKB_MAX_COUNTER_GLYPH_COUNT = 8 };

static void skb__reverse(uint32_t* glyph_ids, int32_t count)
{
	for (int j = 0, k = count - 1; j < k; j++, k--) {
		uint32_t temp = glyph_ids[j];
		glyph_ids[j] = glyph_ids[k];
		glyph_ids[k] = temp;
	}
}

// Based on CSS counters - https://drafts.csswg.org/css-counter-styles-3/#numeric-system
static int32_t skb__construct_counter_numeric(int32_t value, const uint32_t* symbols, int32_t symbols_count, uint32_t* codepoints)
{
	value = skb_maxi(0, value);

	int32_t count = 0;
	if (value == 0) {
		codepoints[count++] = symbols[0];
	} else {
		while (value != 0 && count < SKB_MAX_COUNTER_GLYPH_COUNT - 1) {
			codepoints[count++] = symbols[value % symbols_count];
			value /= symbols_count;
		}
	}
	skb__reverse(codepoints, count);

	return count;
}

// Based on CSS counters - https://drafts.csswg.org/css-counter-styles-3/#alphabetic-system
static int32_t skb__construct_counter_alphabetic(int32_t value, const uint32_t* symbols, int32_t symbols_count, uint32_t* codepoints)
{
	value = skb_maxi(0, value);

	int32_t count = 0;
	while (value != 0 && count < SKB_MAX_COUNTER_GLYPH_COUNT - 1) {
		value = skb_maxi(0, value - 1);
		codepoints[count++] = symbols[value % symbols_count];
		value /= symbols_count;
	}
	skb__reverse(codepoints, count);

	return count;
}

static void skb__line_append_list_marker_run(skb_layout_t* layout, skb_layout_line_t* line, const skb_attribute_list_marker_t* list_marker)
{
	const bool layout_is_rtl = skb_is_rtl(layout->resolved_direction);

	// Get the font to use from the layout/paragraph attributes.
	const uint8_t font_family = skb_attributes_get_font_family(layout->params.layout_attributes, layout->params.attribute_collection);
	const float font_size = skb_attributes_get_font_size(layout->params.layout_attributes, layout->params.attribute_collection);
	const skb_font_handle_t font_handle = skb_font_collection_get_default_font(layout->params.font_collection, font_family);
	if (!font_handle)
		return;
	const skb_font_t* font = skb_font_collection_get_font(layout->params.font_collection, font_handle);
	assert(font);
	const uint8_t script = SBScriptLATN;
	const skb_baseline_t baseline_align = skb_attributes_get_baseline_align(layout->params.layout_attributes, layout->params.attribute_collection);
	const skb_baseline_set_t baseline_set = skb_font_get_baseline_set(layout->params.font_collection, font_handle, layout->resolved_direction, script, font_size);

	const float offset_y = -baseline_set.baselines[baseline_align];
	const float ref_baseline = baseline_set.alphabetic - baseline_set.baselines[baseline_align];

	int32_t marker_glyph_count = 0;
	hb_codepoint_t marker_glyph_ids[SKB_MAX_COUNTER_GLYPH_COUNT] = {0};

	if (list_marker->style == SKB_LIST_MARKER_CODEPOINT) {
		if (hb_font_get_glyph(font->hb_font, list_marker->codepoint, 0, &marker_glyph_ids[0]))
			marker_glyph_count = 1;
	} else {
		uint32_t marker_codepoints[SKB_MAX_COUNTER_GLYPH_COUNT] = {0};
		int32_t marker_codepoints_count = 0;
		if (list_marker->style == SKB_LIST_MARKER_COUNTER_DECIMAL) {
			const uint32_t pattern[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
			const int32_t pattern_count = SKB_COUNTOF(pattern);
			marker_codepoints_count = skb__construct_counter_numeric(layout->params.list_marker_counter+1, pattern, pattern_count, marker_codepoints);
		} else if (list_marker->style == SKB_LIST_MARKER_COUNTER_LOWER_LATIN) {
			const uint32_t pattern[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z' };
			const int32_t pattern_count = 2;//SKB_COUNTOF(pattern);
			marker_codepoints_count = skb__construct_counter_alphabetic(layout->params.list_marker_counter+1, pattern, pattern_count, marker_codepoints);
		} else if (list_marker->style == SKB_LIST_MARKER_COUNTER_UPPER_LATIN) {
			const uint32_t pattern[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z' };
			const int32_t pattern_count = 2;//SKB_COUNTOF(pattern);
			marker_codepoints_count = skb__construct_counter_alphabetic(layout->params.list_marker_counter+1, pattern, pattern_count, marker_codepoints);
		}

		// Suffix
		assert(marker_codepoints_count < SKB_MAX_COUNTER_GLYPH_COUNT-1);
		const uint32_t suffix = '.';
		if (layout_is_rtl) {
			for (int32_t i = marker_codepoints_count; i > 0; i--)
				marker_codepoints[i] = marker_codepoints[i-1];
			marker_codepoints[0] = suffix;
			marker_codepoints_count++;
		} else {
			marker_codepoints[marker_codepoints_count++] = suffix;
		}

		// Convert codepoints to glyph ids.
		for (int32_t i = 0; i < marker_codepoints_count; i++) {
			if (hb_font_get_glyph(font->hb_font, marker_codepoints[i], 0, &marker_glyph_ids[marker_glyph_count]))
				marker_glyph_count++;
		}
	}

	if (!marker_glyph_count)
		return;

	// Calculate advances
	const float scale = font_size * font->upem_scale;
	float total_x_advance = 0.f;

	float marker_glyph_advance[SKB_MAX_COUNTER_GLYPH_COUNT] = {0};
	skb_vec2_t marker_glyph_offset[SKB_MAX_COUNTER_GLYPH_COUNT] = {0};

	for (int32_t i = 0; i < marker_glyph_count; i++) {
		marker_glyph_advance[i] = (float)hb_font_get_glyph_h_advance(font->hb_font, marker_glyph_ids[i]) * scale;

		hb_position_t x, y;
		if (hb_font_get_glyph_h_origin (font->hb_font, marker_glyph_ids[i], &x, &y)) {
			marker_glyph_offset[i].x = x * scale;
			marker_glyph_offset[i].y = y * scale;
		} else {
			marker_glyph_offset[i] = (skb_vec2_t){0};
		}

		total_x_advance += marker_glyph_advance[i];
	}

	// Place the marker glyphs.
	SKB_ARRAY_RESERVE(layout->layout_runs, layout->layout_runs_count + 1);
	skb_layout_run_t* layout_run = &layout->layout_runs[layout->layout_runs_count++];
	SKB_ZERO_STRUCT(layout_run);
	if (line->layout_run_range.start == line->layout_run_range.end) {
		line->layout_run_range.start = layout->layout_runs_count - 1;
		line->layout_run_range.end = layout->layout_runs_count;
	} else {
		line->layout_run_range.end = layout->layout_runs_count;
	}
	assert(line->layout_run_range.end == layout->layout_runs_count);

	// Add the marker layout run front or back depending on the layout direction.
	skb_layout_run_t* marker_run = NULL;
	if (layout_is_rtl) {
		// Add back
		marker_run = &layout->layout_runs[line->layout_run_range.end - 1];
	} else {
		// Add front
		for (int32_t i = line->layout_run_range.end - 1; i > line->layout_run_range.start; i--)
			layout->layout_runs[i] = layout->layout_runs[i-1];
		marker_run = &layout->layout_runs[line->layout_run_range.start];
	}
	SKB_ZERO_STRUCT(marker_run);

	marker_run->type = SKB_CONTENT_RUN_UTF32;
	marker_run->direction = layout->resolved_direction;
	marker_run->script = script;
	marker_run->bidi_level = 0;
	marker_run->font_size = font_size;
	marker_run->ref_baseline = ref_baseline;
	marker_run->font_handle = font_handle;
	marker_run->content_run_idx = SKB_INVALID_INDEX; // Mark as invalid so that the run can be skipped e.g. by caret iterator.
	marker_run->content_run_id = 0;
	marker_run->attributes = layout->params.layout_attributes;
	marker_run->glyph_range.start = layout->glyphs_count;
	marker_run->glyph_range.end = layout->glyphs_count + marker_glyph_count;
	marker_run->cluster_range.start = layout->clusters_count;
	marker_run->cluster_range.end = layout->clusters_count + 1;

	SKB_ARRAY_RESERVE(layout->clusters, layout->clusters_count + 1);
	skb_cluster_t* marker_cluster = &layout->clusters[layout->clusters_count++];
	SKB_ZERO_STRUCT(marker_cluster);
	marker_cluster->text_offset = 0;
	marker_cluster->text_count = 0;
	marker_cluster->glyphs_offset = layout->glyphs_count;
	marker_cluster->glyphs_count = (uint8_t)marker_glyph_count;

	SKB_ARRAY_RESERVE(layout->glyphs, layout->glyphs_count + marker_glyph_count);

	float offset_x = 0.f;
	if (layout_is_rtl)
		offset_x = list_marker->spacing;
	else
		offset_x = -total_x_advance - list_marker->spacing;

	for (int32_t gi = 0; gi < marker_glyph_count; gi++) {
		skb_glyph_t* glyph = &layout->glyphs[layout->glyphs_count++];
		SKB_ZERO_STRUCT(glyph);
		glyph->offset_x = offset_x + marker_glyph_offset[gi].x;
		glyph->offset_y = offset_y + marker_glyph_offset[gi].y;
		glyph->advance_x = 0.f;
		glyph->gid = (uint16_t)marker_glyph_ids[gi];
		offset_x += marker_glyph_advance[gi];
	}
}

static void skb__truncate_line(skb_layout_t* layout, skb_layout_line_t* line, float line_break_width, bool add_ellipsis)
{
	const bool layout_is_rtl = skb_is_rtl(layout->resolved_direction);

	SKB_SET_FLAG(line->flags, SKB_LAYOUT_LINE_IS_TRUNCATED, true);

	// Prune characters to fit the line (this is common with clip and ellipsis).
	skb__prune_line_end(layout, line, line_break_width);

	if (!add_ellipsis)
		return;

	// Find a text run to use as reference for the ellipsis text.
	int32_t ref_layout_run_idx = SKB_INVALID_INDEX;
	if (layout_is_rtl) {
		// We're truncating front, search from front.
		for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
			const skb_layout_run_t* run = &layout->layout_runs[ri];
			if (run->type == SKB_CONTENT_RUN_UTF8 || run->type == SKB_CONTENT_RUN_UTF32) {
				ref_layout_run_idx = ri;
				break;
			}
		}
	} else {
		// We're truncating back, search from back.
		for (int32_t ri = line->layout_run_range.end - 1; ri >= line->layout_run_range.start; ri--) {
			const skb_layout_run_t* run = &layout->layout_runs[ri];
			if (run->type == SKB_CONTENT_RUN_UTF8 || run->type == SKB_CONTENT_RUN_UTF32) {
				ref_layout_run_idx = ri;
				break;
			}
		}
	}
	if (ref_layout_run_idx == SKB_INVALID_INDEX)
		return;

	const skb_layout_run_t ref_layout_run = layout->layout_runs[ref_layout_run_idx]; // We take copy, since the array may change later.
	if (!ref_layout_run.font_handle)
		return;

	// We expect these to exist, if the handle is valid.
	const skb_font_t* font = skb_font_collection_get_font(layout->params.font_collection, ref_layout_run.font_handle);
	assert(font);
	const skb_baseline_t baseline_align = skb_attributes_get_baseline_align(layout->params.layout_attributes, layout->params.attribute_collection);
	const float baseline = skb_font_get_baseline(
		layout->params.font_collection, ref_layout_run.font_handle, baseline_align,
		ref_layout_run.direction, ref_layout_run.script, ref_layout_run.font_size);
	const float offset_y = -baseline;

	// Try to use the actual ellipsis character, but fall back to 3 periods.
	int32_t ellipsis_glyph_count = 0;
	hb_codepoint_t ellipsis_gid = 0;
	if (hb_font_get_glyph(font->hb_font, 0x2026 /*ellipsis*/, 0, &ellipsis_gid))
		ellipsis_glyph_count = 1;
	else if (hb_font_get_glyph(font->hb_font, 0x2e /*period*/, 0, &ellipsis_gid))
		ellipsis_glyph_count = 3;

	if (!ellipsis_glyph_count)
		return;

	const float font_size = ref_layout_run.font_size;
	const float scale = font_size * font->upem_scale;
	const float ellipsis_x_advance = (float)hb_font_get_glyph_h_advance(font->hb_font, ellipsis_gid) * scale;
	const float ellipsis_width = ellipsis_x_advance * (float)ellipsis_glyph_count;

	// Prune the line further until the ellipsis fits.
	const float max_line_width = line_break_width - ellipsis_width;
	skb__prune_line_end(layout, line, max_line_width);

	// Place the ellipsis characters.
	assert(line->layout_run_range.end == layout->layout_runs_count);
	SKB_ARRAY_RESERVE(layout->layout_runs, layout->layout_runs_count + 1);
	layout->layout_runs_count++;
	line->layout_run_range.end++;
	assert(line->layout_run_range.end == layout->layout_runs_count);

	// Add the ellipsis layout run front or back depending on the layout direction.
	skb_layout_run_t* ellipsis_run = NULL;
	if (layout_is_rtl) {
		// Add front
		for (int32_t i = line->layout_run_range.end - 1; i > line->layout_run_range.start; i--)
			layout->layout_runs[i] = layout->layout_runs[i-1];
		ellipsis_run = &layout->layout_runs[line->layout_run_range.start];
	} else {
		// Add back
		ellipsis_run = &layout->layout_runs[line->layout_run_range.end - 1];
	}
	SKB_ZERO_STRUCT(ellipsis_run);

	ellipsis_run->type = SKB_CONTENT_RUN_UTF32;
	ellipsis_run->direction = ref_layout_run.direction;
	ellipsis_run->script = ref_layout_run.script;
	ellipsis_run->bidi_level = ref_layout_run.bidi_level;
	ellipsis_run->font_size = ref_layout_run.font_size;
	ellipsis_run->ref_baseline = ref_layout_run.ref_baseline;
	ellipsis_run->font_handle = ref_layout_run.font_handle;
	ellipsis_run->content_run_idx = ref_layout_run.content_run_idx;
	ellipsis_run->content_run_id = ref_layout_run.content_run_id;
	ellipsis_run->attributes = ref_layout_run.attributes;
	ellipsis_run->glyph_range.start = layout->glyphs_count;
	ellipsis_run->glyph_range.end = layout->glyphs_count + ellipsis_glyph_count;
	ellipsis_run->cluster_range.start = layout->clusters_count;
	ellipsis_run->cluster_range.end = layout->clusters_count + 1;

	SKB_ARRAY_RESERVE(layout->clusters, layout->clusters_count + 1);
	skb_cluster_t* ellipsis_cluster = &layout->clusters[layout->clusters_count++];
	SKB_ZERO_STRUCT(ellipsis_cluster);
	ellipsis_cluster->text_offset = 0; // TODO: this should account for the missing text, but it may be multiple spans, since we're truncating runs in visual order.
	ellipsis_cluster->text_count = 0;
	ellipsis_cluster->glyphs_offset = layout->glyphs_count;
	ellipsis_cluster->glyphs_count = (uint8_t)ellipsis_glyph_count;

	SKB_ARRAY_RESERVE(layout->glyphs, layout->glyphs_count + ellipsis_glyph_count);
	for (int32_t ei = 0; ei < ellipsis_glyph_count; ei++) {
		skb_glyph_t* glyph = &layout->glyphs[layout->glyphs_count++];
		SKB_ZERO_STRUCT(glyph);
		glyph->offset_x = 0.f;
		glyph->offset_y = offset_y;
		glyph->advance_x = ellipsis_x_advance;
		glyph->gid = (uint16_t)ellipsis_gid;

		line->bounds.width += ellipsis_x_advance;
	}
}

typedef struct skb__calculated_layout_size_t {
	float width;
	float height;
	float first_line_cap_height;
} skb__calculated_layout_size_t;

static bool skb__finalize_line(skb_layout_t* layout, skb_layout_line_t* line, bool is_last_line, skb__calculated_layout_size_t* layout_size)
{
	// Do not finalize line if it's empty.
	if (!is_last_line && line->layout_run_range.start == line->layout_run_range.end)
		return false;

	const bool layout_is_rtl = skb_is_rtl(layout->resolved_direction);
	const float line_break_width = layout->params.layout_width;
	const skb_text_overflow_t text_overflow = skb_attributes_get_text_overflow(layout->params.layout_attributes, layout->params.attribute_collection);
	const skb_baseline_t baseline_align = skb_attributes_get_baseline_align(layout->params.layout_attributes, layout->params.attribute_collection);

	const int32_t line_idx = (int32_t)(line - layout->lines);

	// The line is still in logical order and not truncated for overflow, grab the text range.
	if (line->layout_run_range.start != line->layout_run_range.end) {
		const skb_layout_run_t* first_layout_run = &layout->layout_runs[line->layout_run_range.start];
		const skb_layout_run_t* last_layout_run = &layout->layout_runs[line->layout_run_range.end - 1];
		const skb_cluster_t* first_cluster = &layout->clusters[first_layout_run->cluster_range.start];
		const skb_cluster_t* last_cluster = &layout->clusters[last_layout_run->cluster_range.end - 1];
		line->text_range.start = first_cluster->text_offset;
		line->text_range.end = last_cluster->text_offset + last_cluster->text_count;
		// Find beginning of the last grapheme on the line (needed or caret positioning, etc).
		line->last_grapheme_offset = skb_layout_align_grapheme_offset(layout, line->text_range.end - 1);
	} else {
		// The last line can be empty, if the last character is new line separator.
		assert(is_last_line);
		line->text_range.start = layout->text_count;
		line->text_range.end = layout->text_count;
		// This is intentionally past the last valid glyph so that we can uniquely address the line using text position.
		line->last_grapheme_offset = layout->text_count;
		// This is intentionally past the last valid glyph so that we can uniquely address the line using text position.
		line->last_grapheme_offset = layout->text_count;
	}

	// Sort in visual order
	skb__reorder_runs(layout, line->layout_run_range);

	// List marker
	if (line_idx == 0) {
		const skb_attribute_list_marker_t list_marker = skb_attributes_get_list_marker(layout->params.layout_attributes, layout->params.attribute_collection);
		if (list_marker.style != SKB_LIST_MARKER_NONE)
			skb__line_append_list_marker_run(layout, line, &list_marker);
	}

	//
	// Calculate line height and baseline
	//
	const float max_height = layout->params.layout_height;
	float line_height = 0.f;

	if (line->layout_run_range.start != line->layout_run_range.end) {

		for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
			skb_layout_run_t* layout_run = &layout->layout_runs[ri];
			float baseline_align_offset = 0.f;
			if (layout_run->type == SKB_CONTENT_RUN_OBJECT || layout_run->type == SKB_CONTENT_RUN_ICON) {

				const skb_attribute_object_align_t attr_object_align = skb_attributes_get_object_align(layout_run->attributes, layout->params.attribute_collection);
				const skb_attribute_object_padding_t attr_object_padding = skb_attributes_get_object_padding(layout_run->attributes, layout->params.attribute_collection);

				// Find index of the reference glyph to align to.
				int32_t ref_layout_run_idx = ri; // self
				if (attr_object_align.align_ref == SKB_OBJECT_ALIGN_TEXT_BEFORE || attr_object_align.align_ref == SKB_OBJECT_ALIGN_TEXT_BEFORE_OR_AFTER) {
					ref_layout_run_idx = skb__get_text_run_before(layout, ri);
					// If not found, try the other side of allowed.
					if (ref_layout_run_idx == SKB_INVALID_INDEX && attr_object_align.align_ref == SKB_OBJECT_ALIGN_TEXT_BEFORE_OR_AFTER)
						ref_layout_run_idx = skb__get_text_run_after(layout, ri);
				} else if (attr_object_align.align_ref == SKB_OBJECT_ALIGN_TEXT_AFTER || attr_object_align.align_ref == SKB_OBJECT_ALIGN_TEXT_AFTER_OR_BEFORE) {
					ref_layout_run_idx = skb__get_text_run_after(layout, ri);
					// If not found, try the other side of allowed.
					if (ref_layout_run_idx == SKB_INVALID_INDEX && attr_object_align.align_ref == SKB_OBJECT_ALIGN_TEXT_AFTER_OR_BEFORE)
						ref_layout_run_idx = skb__get_text_run_before(layout, ri);
				}

				// Find baseline to align to.
				float ref_baseline = 0.f;
				if (ref_layout_run_idx != SKB_INVALID_INDEX) {
					const skb_layout_run_t* ref_layout_run = &layout->layout_runs[ref_layout_run_idx];
					const skb_baseline_set_t baseline_set = skb_font_get_baseline_set(
						layout->params.font_collection, ref_layout_run->font_handle,
						ref_layout_run->direction, ref_layout_run->script, ref_layout_run->font_size);
					ref_baseline = baseline_set.baselines[attr_object_align.align_baseline] - baseline_set.baselines[baseline_align];
				}

				skb__content_run_t* content_run = &layout->content_runs[layout_run->content_run_idx];
				const float object_height = content_run->content_height + attr_object_padding.top + attr_object_padding.bottom;
				const float object_baseline = content_run->content_height * attr_object_align.baseline_ratio + attr_object_padding.top;

				line_height = skb_maxf(line_height, object_height);

				const float ascender = ref_baseline - object_baseline;
				const float descender = ref_baseline + object_height - object_baseline;
				line->ascender = skb_minf(line->ascender, ascender);
				line->descender = skb_maxf(line->descender, descender);

				baseline_align_offset = ref_baseline - object_baseline;

				// Calculate layout run bounds
				// The glyph offset for object/icon contains the padding at this point.
				skb_glyph_t* first_glyph = &layout->glyphs[layout_run->glyph_range.start];
				layout_run->bounds.x = first_glyph->offset_x;
				layout_run->bounds.y = first_glyph->offset_y + baseline_align_offset;
				layout_run->bounds.width = content_run->content_width;
				layout_run->bounds.height = content_run->content_height;
				layout_run->ref_baseline = layout_run->bounds.y + content_run->content_height;

			} else {

				const float font_size = layout_run->font_size;
				const skb_attribute_line_height_t attr_line_height = skb_attributes_get_line_height(layout_run->attributes, layout->params.attribute_collection);
				const skb_font_t* font = skb_font_collection_get_font(layout->params.font_collection, layout_run->font_handle);
				const float baseline = skb_font_get_baseline(layout->params.font_collection, layout_run->font_handle, baseline_align, layout_run->direction, layout_run->script, font_size);

				baseline_align_offset = -baseline;

				line_height = skb_maxf(line_height, skb_calculate_line_height(attr_line_height, font, font_size));
				line->ascender = skb_minf(line->ascender, font->metrics.ascender * font_size - baseline);
				line->descender = skb_maxf(line->descender, font->metrics.descender * font_size - baseline);
				if (line_idx == 0)
					layout_size->first_line_cap_height = skb_minf(layout_size->first_line_cap_height, font->metrics.cap_height * font_size - baseline);

				// Calculate layout run bounds
				skb_glyph_t* first_glyph = &layout->glyphs[layout_run->glyph_range.start];

				layout_run->bounds.x = first_glyph->offset_x;
				layout_run->bounds.y = line->ascender;
				layout_run->bounds.width = 0.f;
				layout_run->bounds.height = -line->ascender + line->descender;

				for (int32_t gi = layout_run->glyph_range.start; gi < layout_run->glyph_range.end; gi++) {
					const skb_glyph_t* glyph = &layout->glyphs[gi];
					layout_run->bounds.width += glyph->advance_x;
				}

				// Calculate reference baseline for the run. At this stage Y=0 is at baseline specified by params.baseline_align, calculate the reference baseline relative to that.
				// The OpenType file format is not specific about in which coordinate system the baseline metrics are reported.
				// We assume that they are relative to the alphabetic baseline, as that seems to be the recommendation to author fonts.
				const skb_baseline_set_t baseline_set = skb_font_get_baseline_set(layout->params.font_collection, layout_run->font_handle, layout_run->direction, layout_run->script, layout_run->font_size);
				layout_run->ref_baseline = baseline_set.alphabetic - baseline_set.baselines[baseline_align];
			}

			for (int32_t gi = layout_run->glyph_range.start; gi < layout_run->glyph_range.end; gi++) {
				skb_glyph_t* glyph = &layout->glyphs[gi];
				glyph->offset_y += baseline_align_offset;
			}
		}

	} else {
		// If we end up here, we're dealing with last empty new line.
		skb_attribute_set_t attributes = {0};
		float font_size = 0.f;
		if (layout->content_runs_count > 0) {
			const skb__content_run_t* last_content_run = &layout->content_runs[layout->content_runs_count - 1];
			attributes = last_content_run->attributes;
			font_size = last_content_run->font_size;
		} else {
			attributes = layout->params.layout_attributes;
			font_size = skb_attributes_get_font_size(attributes, layout->params.attribute_collection);
		}
		const uint8_t font_family = skb_attributes_get_font_family(attributes, layout->params.attribute_collection);
		const skb_attribute_line_height_t attr_line_height = skb_attributes_get_line_height(attributes, layout->params.attribute_collection);

		const skb_font_handle_t default_font_handle = skb_font_collection_get_default_font(layout->params.font_collection, font_family);
		const skb_font_t* font = skb_font_collection_get_font(layout->params.font_collection, default_font_handle);
		if (font) {
			line_height = skb_maxf(line_height, skb_calculate_line_height(attr_line_height, font, font_size));
			line->ascender = skb_minf(line->ascender, font->metrics.ascender * font_size);
			line->descender = skb_maxf(line->descender, font->metrics.descender * font_size);
			if (line_idx == 0)
				layout_size->first_line_cap_height = font->metrics.cap_height * font_size;
		}
	}

	if (text_overflow != SKB_OVERFLOW_NONE) {
		if (line_idx > 0 && (layout_size->height + line_height) > max_height) {
			// The line will overflow the max height for the layout, trim this and any following lines.

			// Remove the current line.
			assert(layout->layout_runs_count == line->layout_run_range.end);
			layout->layout_runs_count = line->layout_run_range.start;

			// Ellipsize previous line if needed.
			if (text_overflow == SKB_OVERFLOW_ELLIPSIS) {
				skb_layout_line_t* prev_line = &layout->lines[line_idx - 1];
				skb__truncate_line(layout, prev_line, line_break_width, true);
				layout_size->width = skb_maxf(layout_size->width, prev_line->bounds.width);
			}

			layout->lines_count--;

			// Report max height reached.
			return true;
		}
	}

	line->bounds.height = line_height;
	layout_size->height += line_height;

	if (text_overflow != SKB_OVERFLOW_NONE && line->bounds.width > line_break_width)
		skb__truncate_line(layout, line, line_break_width, text_overflow == SKB_OVERFLOW_ELLIPSIS);

	layout_size->width = skb_maxf(layout_size->width, line->bounds.width);

	return false;
}

void skb__layout_lines(skb__layout_build_context_t* build_context, skb_layout_t* layout)
{
	const bool ignore_must_breaks = layout->params.flags & SKB_LAYOUT_PARAMS_IGNORE_MUST_LINE_BREAKS;

	layout->bounds.x = 0.f;
	layout->bounds.y = 0.f;
	layout->bounds.width = 0.f;
	layout->bounds.height = 0.f;
	layout->advance_y = 0.f;

	layout->layout_runs_count = 0;

	skb__calculated_layout_size_t layout_size = {0};

	skb_layout_line_t* cur_line = skb__add_line(layout);

	const bool layout_is_rtl = skb_is_rtl(layout->resolved_direction);
	const skb_text_wrap_t text_wrap = skb_attributes_get_text_wrap(layout->params.layout_attributes, layout->params.attribute_collection);

	// Handle horizontal padding and indent
	const skb_attribute_list_marker_t list_marker = skb_attributes_get_list_marker(layout->params.layout_attributes, layout->params.attribute_collection);
	const skb_attribute_horizontal_padding_t horizontal_padding = skb_attributes_get_horizontal_padding(layout->params.layout_attributes, layout->params.attribute_collection);
	const skb_attribute_indent_increment_t indent_increment = skb_attributes_get_indent_increment(layout->params.layout_attributes, layout->params.attribute_collection);
	const int32_t indent_level = skb_attributes_get_indent_level(layout->params.layout_attributes, layout->params.attribute_collection);

	SKB_ARRAY_RESERVE(layout->layout_runs, layout->shaping_runs_count);

	// Wrapping
	bool max_heigh_reached = false;
 	skb_layout_run_t* cur_layout_run = NULL;
	skb__shaping_run_cluster_iter_t it = skb__shaping_run_cluster_iter_make(layout);

	const float horizontal_padding_start = skb_minf(horizontal_padding.start + (float)indent_level * indent_increment.level_increment + list_marker.indent, layout->params.layout_width);
	const float horizontal_padding_end = skb_minf(horizontal_padding.end, layout->params.layout_width - horizontal_padding_start);

	const float inner_layout_width = skb_maxf(0.f, layout->params.layout_width - (horizontal_padding_start + horizontal_padding_end));
	const float tab_stop_increment = skb_attributes_get_tab_stop_increment(layout->params.layout_attributes, layout->params.attribute_collection);

	// Init the line break width to the first line width (will be reset to inner_layout_width after first line).
	float line_break_width = skb_maxf(0.f, inner_layout_width - indent_increment.first_line_increment);

	while (skb__shaping_run_cluster_iter_is_valid(&it, layout) && !max_heigh_reached) {
		// Calc run up to the next line break.
		skb__shaping_run_cluster_iter_t start_it = it;
		skb__shaping_run_cluster_iter_t end_it = it;

		float run_end_whitespace_width = 0.f;
		float run_width = 0.f;

		bool tab_overflows = false;
		bool must_break = false;
		while (skb__shaping_run_cluster_iter_is_valid(&end_it, layout)) {

			// Advance whole glyph cluster, cannot split in between.
			const skb_cluster_t* cluster = &layout->clusters[end_it.cluster_idx]; //cluster_run_end];
			float cluster_width = skb__get_cluster_width(layout, end_it.cluster_idx); //cluster_run_end);

			const int cp_offset = cluster->text_offset + cluster->text_count - 1;

			// Handle tabs
			bool codepoint_is_tab = false;
			if (layout->text[cp_offset] == SKB_CHAR_HORIZONTAL_TAB && tab_stop_increment > 0.f) {
				// Calculate the next tab stop.
				const float cur_pos = cur_line->bounds.width + run_width + run_end_whitespace_width;
				const float next_tab_stop = floorf((cur_pos + tab_stop_increment) / tab_stop_increment) * tab_stop_increment;

				// Check if the tab will overflow the width, if so reset to the first tab on new line and signal to break line.
				float tab_width = 0.f;
				if (next_tab_stop > line_break_width) {
					tab_overflows = true;
					tab_width = tab_stop_increment;
				} else {
					tab_width = next_tab_stop - cur_pos;
				}

				// Update glyph width to match the tab width.
				const int32_t cluster_last_glyph_idx = cluster->glyphs_offset + cluster->glyphs_count - 1;
				layout->glyphs[cluster_last_glyph_idx].advance_x = tab_width;
				cluster_width = tab_width;
				codepoint_is_tab = true;
			}

			// Keep track of the white space after the run end, it will not be taken into account for the line breaking.
			// When the direction does not match, the space will be inside the line (not end of it), so we ignore that.
			// Treat tab as non-whitespace so that it allocates space at the end of the line too.
			const bool codepoint_is_rtl = skb_is_rtl(layout->shaping_runs[end_it.shaping_run_idx].direction); // shaping_run->direction);
			const bool codepoint_is_whitespace = (layout->text_props[cp_offset].flags & SKB_TEXT_PROP_WHITESPACE);
			const bool codepoint_is_control = (layout->text_props[cp_offset].flags & SKB_TEXT_PROP_CONTROL);
			if (codepoint_is_rtl == layout_is_rtl && (codepoint_is_whitespace || codepoint_is_control) && !codepoint_is_tab) {
				run_end_whitespace_width += cluster_width;
			} else {
				if (run_end_whitespace_width > 0.f) {
					run_width += run_end_whitespace_width;
					run_end_whitespace_width = 0.f;
				}
				run_width += cluster_width;
			}

			// Advance to next cluster.
			skb__shaping_run_cluster_iter_next(&end_it, layout);

			if (layout->text_props[cp_offset].flags & SKB_TEXT_PROP_MUST_LINE_BREAK) {
				must_break = true;
				break;
			}
			if (layout->text_props[cp_offset].flags & SKB_TEXT_PROP_ALLOW_LINE_BREAK)
				break;
		}

		if (text_wrap == SKB_WRAP_WORD_CHAR && run_width > line_break_width) {
			// If text wrap is set to word & char, allow to break at a character when the whole word does not fit.

			// Start a new line
			max_heigh_reached = skb__finalize_line(layout, cur_line, false, &layout_size);
			cur_line = NULL;
			if (max_heigh_reached)
				break;
			cur_line = skb__add_line(layout);
			cur_layout_run = NULL;
			line_break_width = inner_layout_width;

			// Fit as many glyphs as we can on the line, and adjust run_end up to that point.
			run_width = 0.f;
			skb__shaping_run_cluster_iter_t cit = start_it;
			while (cit.cluster_idx < end_it.cluster_idx) {
				float cluster_width = skb__get_cluster_width(layout, cit.cluster_idx);
				if ((cur_line->bounds.width + run_width + cluster_width) > line_break_width) {
					// This glyph would overflow, stop here. run_end is exclusive, so one past the last valid index.
					end_it = cit;
					break;
				}
				run_width += layout->glyphs[cit.cluster_idx].advance_x;
				skb__shaping_run_cluster_iter_next(&cit, layout);
			}
			// Consume at least one cluster so that we don't get stuck.
			if (start_it.cluster_idx == end_it.cluster_idx) {
				run_width = skb__get_cluster_width(layout, end_it.cluster_idx);
				skb__shaping_run_cluster_iter_next(&end_it, layout);
			}

			// Update width so far.
			cur_line->bounds.width += run_width;
			cur_layout_run = skb__line_append_shaping_run_range(layout, cur_line, cur_layout_run, start_it, end_it);

		} else {
			// If the word does not fit, or tab brought us to the next line, start a new line (unless it's an empty line).
			const bool width_overflows = (cur_line->bounds.width + run_width) > line_break_width;
			if (text_wrap != SKB_WRAP_NONE && (width_overflows || tab_overflows)) {
				max_heigh_reached = skb__finalize_line(layout, cur_line, false, &layout_size);
				cur_line = NULL;
				if (max_heigh_reached)
					break;
				cur_line = skb__add_line(layout);
				cur_layout_run = NULL;
				line_break_width = inner_layout_width;
			}

			// Update width so far.
			cur_line->bounds.width += run_width + run_end_whitespace_width;
			cur_layout_run = skb__line_append_shaping_run_range(layout, cur_line, cur_layout_run, start_it, end_it);

			if (must_break && !ignore_must_breaks) {
				// Line break character start a new line.
				max_heigh_reached = skb__finalize_line(layout, cur_line, false, &layout_size);
				cur_line = NULL;
				if (max_heigh_reached)
					break;
				cur_line = skb__add_line(layout);
				cur_layout_run = NULL;
				line_break_width = inner_layout_width;
			}
		}

		// We have consumed the clusters up to end_it-1, continue new word from end_t.
		it = end_it;
	}
	// Finalize last line
	if (cur_line)
		skb__finalize_line(layout, cur_line, true, &layout_size);

	//
	// Align layout and lines
	//

	const skb_align_t horizontal_align = skb_attributes_get_horizontal_align(layout->params.layout_attributes, layout->params.attribute_collection);
	const skb_align_t vertical_align = skb_attributes_get_vertical_align(layout->params.layout_attributes, layout->params.attribute_collection);
	const skb_vertical_trim_t vertical_trim = skb_attributes_get_vertical_trim(layout->params.layout_attributes, layout->params.attribute_collection);
	const skb_attribute_vertical_padding_t vertical_padding = skb_attributes_get_vertical_padding(layout->params.layout_attributes, layout->params.attribute_collection);

	if (vertical_trim == SKB_VERTICAL_TRIM_CAP_TO_BASELINE) {
		// Adjust the calculated_height so that first line only accounts for cap height (not all the way to ascender), and last line does not count descender.
		const float first_line_ascender = layout->lines[0].ascender;
		const float height_diff = layout_size.first_line_cap_height - first_line_ascender; // Note: cap height and ascender are negative.
		layout_size.height -= height_diff;
		const float last_line_descender = layout->lines[layout->lines_count-1].descender;
		layout_size.height -= last_line_descender;
	}

	// Align layout
	layout->bounds.x = skb_calc_align_offset(skb_get_directional_align(layout_is_rtl, horizontal_align), layout_size.width, inner_layout_width);
	if (layout_is_rtl)
		layout->bounds.x -= horizontal_padding_start;
	else
		layout->bounds.x += horizontal_padding_start;

	if (layout->params.flags & SKB_LAYOUT_PARAMS_IGNORE_VERTICAL_ALIGN)
		layout->bounds.y = 0.f;
	else
		layout->bounds.y = skb_calc_align_offset(vertical_align, layout_size.height, layout->params.layout_height);
	layout->bounds.y += vertical_padding.before;

	layout->bounds.width = layout_size.width;
	layout->bounds.height = layout_size.height;
	layout->advance_y = vertical_padding.before + layout_size.height + vertical_padding.after;

	float start_y = layout->bounds.y;

	if (vertical_trim == SKB_VERTICAL_TRIM_CAP_TO_BASELINE) {
		// Adjust start position so that the top is aligned to cap height.
		const float first_line_ascender = layout->lines[0].ascender;
		const float height_diff = layout_size.first_line_cap_height - first_line_ascender; // Note: cap height and ascender are negative.
		start_y -= height_diff;
	}

	// Align lines
	for (int32_t li = 0; li < layout->lines_count; li++) {
		skb_layout_line_t* line = &layout->lines[li];

		// Trim white space from end of the line.
		const float whitespace_width = skb__calc_run_end_whitespace(layout, line->layout_run_range);
		line->bounds.width -= whitespace_width;
		line->bounds.x = layout->bounds.x + skb_calc_align_offset(skb_get_directional_align(layout_is_rtl, horizontal_align), line->bounds.width, layout_size.width);
		// Handle first line indent.
		if (li == 0) {
			const float delta_x = skb_minf(inner_layout_width, indent_increment.first_line_increment);
			line->bounds.x += layout_is_rtl ? -delta_x : delta_x;
		}

		float start_x = 0.f;
		if (layout_is_rtl)
			start_x = line->bounds.x - whitespace_width;
		else
			start_x = line->bounds.x;

		line->bounds.y = start_y;

		// Update glyph offsets
		const float leading = line->bounds.height - (-line->ascender + line->descender);
		const float leading_above = leading * 0.5f;
		line->baseline = line->bounds.y + leading_above - line->ascender;

		float cur_x = start_x;
		for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
			skb_layout_run_t* layout_run = &layout->layout_runs[ri];
			layout_run->bounds.x += cur_x;
			layout_run->bounds.y += line->baseline;
			layout_run->ref_baseline += line->baseline;

			for (int32_t j = layout_run->glyph_range.start; j < layout_run->glyph_range.end; j++) {
				skb_glyph_t* glyph = &layout->glyphs[j];
				glyph->offset_x += cur_x;
				glyph->offset_y += line->baseline;
				cur_x += glyph->advance_x;
			}
		}

		start_y += line->bounds.height;
	}

	//
	// Calculate culling bounds
	//
	for (int32_t li = 0; li < layout->lines_count; li++) {
		skb_layout_line_t* line = &layout->lines[li];

		if (line->layout_run_range.start != line->layout_run_range.end) {
			line->culling_bounds = skb_rect2_make_undefined();
			line->common_glyph_bounds = skb_rect2_make_undefined();
			for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
				skb_layout_run_t* layout_run = &layout->layout_runs[ri];
				if (layout_run->type == SKB_CONTENT_RUN_OBJECT || layout_run->type == SKB_CONTENT_RUN_ICON) {
					// Object or Icon
					line->culling_bounds = skb_rect2_union(line->culling_bounds, layout_run->bounds);
				} else {
					// Text
					if (layout_run->glyph_range.start != layout_run->glyph_range.end) {
						for (int32_t gi = layout_run->glyph_range.start; gi < layout_run->glyph_range.end; gi++) {
							const skb_glyph_t* glyph = &layout->glyphs[gi];
							const skb_rect2_t glyph_bounds = skb_font_get_glyph_bounds(layout->params.font_collection, layout_run->font_handle, glyph->gid, layout_run->font_size);
							// Calculate glyph bounds that can encompass all the glyphs in the line, used for per glyph culling.
							line->common_glyph_bounds = skb_rect2_union(line->common_glyph_bounds, glyph_bounds);
							// Calculate line bounds, using glyph bounds offset to glyph position.
							line->culling_bounds = skb_rect2_union(line->culling_bounds, skb_rect2_translate(glyph_bounds, (skb_vec2_t){ glyph->offset_x, glyph->offset_y }));
						}
					}
				}
			}
		}
	}

	//
	// Build decorations.
	//
	layout->decorations_count = 0;

	for (int32_t li = 0; li < layout->lines_count; li++) {
		skb_layout_line_t* line = &layout->lines[li];

		line->decorations_range.start = layout->decorations_count;

		// Iterate over runs of same attribute span.
		for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {

			// Find range of runs that share same content run.
			skb_range_t decoration_run_range = { .start = ri, .end = ri + 1 };
			const int32_t content_run_idx = layout->layout_runs[decoration_run_range.start].content_run_idx;
			while (decoration_run_range.end < line->layout_run_range.end && layout->layout_runs[decoration_run_range.end].content_run_idx == content_run_idx)
				decoration_run_range.end++;
			ri = decoration_run_range.end - 1;

			// For each decoration.
			const float font_size = layout->layout_runs[decoration_run_range.start].font_size;

			const skb_attribute_t* decorations[16];
			const int32_t decorations_count = skb_attributes_get_by_kind(layout->layout_runs[decoration_run_range.start].attributes, layout->params.attribute_collection, SKB_ATTRIBUTE_DECORATION, decorations, SKB_COUNTOF(decorations));

			for (int32_t i = 0; i < decorations_count; i++) {
				if (decorations[i]->kind != SKB_ATTRIBUTE_DECORATION)
					continue;

				const skb_attribute_decoration_t attr_decoration = decorations[i]->decoration;

				// Find line position.
				float line_position = 0.f; // At baseline
				float line_position_div = 0.0f;
				float thickness = 0.f;
				float thickness_div = 0.f;

				// Calculate the position of the line.
				// The OpenType file format is not specific about in which coordinate system the baseline metrics are reported.
				// We assume that they are relative to the alphabetic baseline, as that seems to be the recommendation to author fonts.
				const float base_ref_baseline = layout->layout_runs[decoration_run_range.start].ref_baseline;
				skb_font_handle_t prev_font_handle = 0;
				for (int32_t sri = decoration_run_range.start; sri < decoration_run_range.end; sri++) {
					const skb_layout_run_t* run = &layout->layout_runs[sri];

					if (run->font_handle != prev_font_handle) {
						const skb_font_t* font = skb_font_collection_get_font(layout->params.font_collection, run->font_handle);
						if (font) {
							const float delta_y = run->ref_baseline - base_ref_baseline;
							if (attr_decoration.position == SKB_DECORATION_UNDERLINE) {
								line_position = skb_maxf(line_position, delta_y + font->metrics.underline_offset * font_size);
								thickness += font->metrics.underline_size * font_size;
							} else if (attr_decoration.position == SKB_DECORATION_BOTTOMLINE) {
								line_position = skb_maxf(line_position, delta_y + font->metrics.descender * font_size);
								thickness += font->metrics.underline_size * font_size;
							} else if (attr_decoration.position == SKB_DECORATION_OVERLINE) {
								line_position = skb_minf(line_position, delta_y + font->metrics.ascender * font_size);
								thickness += font->metrics.underline_size * font_size;
							} else if (attr_decoration.position == SKB_DECORATION_THROUGHLINE) {
								line_position += delta_y + font->metrics.strikeout_offset * font_size;
								line_position_div += 1.0f;
								thickness += font->metrics.strikeout_size * font_size;
							}
							thickness_div += 1.f;
						}
						prev_font_handle = run->font_handle;
					}
				}

				// Average position if requested.
				if (line_position_div > 0.f)
					line_position /= line_position_div;

				// Use thickness from the attribute if specified, or calculate average of thickness based on font data.
				if (attr_decoration.thickness > 0.f) {
					thickness = attr_decoration.thickness;
				} else {
					if (thickness_div > 0.f)
						thickness /= thickness_div;
				}

				// Apply offset.
				if (attr_decoration.position == SKB_DECORATION_UNDERLINE || attr_decoration.position == SKB_DECORATION_BOTTOMLINE)
					line_position += attr_decoration.offset;
				else if (attr_decoration.position == SKB_DECORATION_THROUGHLINE || attr_decoration.position == SKB_DECORATION_OVERLINE)
					line_position -= attr_decoration.offset;

				// Calculate position of the range.
				float start_x = 0.f, end_x = 0.f;
				skb__calc_run_range_end_points(layout, decoration_run_range, &start_x, &end_x);

				// Add decoration
				SKB_ARRAY_RESERVE(layout->decorations, layout->decorations_count + 1);
				skb_decoration_t* decoration = &layout->decorations[layout->decorations_count++];
				decoration->offset_x = start_x;
				decoration->offset_y = base_ref_baseline + line_position;
				decoration->length = end_x - start_x;
				decoration->pattern_offset = start_x;
				decoration->thickness = thickness;
				decoration->style = attr_decoration.style;
				decoration->position = attr_decoration.position;
				decoration->color = attr_decoration.color;
				decoration->layout_run_idx = (uint16_t)decoration_run_range.start;
			}
		}

		line->decorations_range.end = layout->decorations_count;
	}
}

//
// Layout
//

static void skb__override_line_breaks(skb_layout_t* layout, int32_t start_offset, int32_t end_offset, boundary_iterator_t iter)
{
	// Override line breaks.
	for (int32_t j = start_offset; j < end_offset; j++) {
		layout->text_props[j].flags &= ~SKB_TEXT_PROP_ALLOW_LINE_BREAK;
		// Allow line break before tabs.
		if (layout->text[j] == SKB_CHAR_HORIZONTAL_TAB && j > 0)
			layout->text_props[j-1].flags |= SKB_TEXT_PROP_ALLOW_LINE_BREAK;
	}

	int32_t range_start = 0, range_end = 0;
	while (boundary_iterator_next(&iter, &range_start, &range_end)) {
		// Include white space after the word to be consistent with unibreak.
		// The iterator is fed a substring starting at start_offset, we will need to adjust the iterator to start with start_offset.
		int32_t offset = start_offset + range_end-1;
		while ((offset+1) < layout->text_count && (layout->text_props[offset].flags & SKB_TEXT_PROP_WHITESPACE))
			offset++;
		layout->text_props[offset].flags |= SKB_TEXT_PROP_ALLOW_LINE_BREAK;
	}
}


static void skb__apply_lang_based_word_breaks(const skb__layout_build_context_t* build_context, skb_layout_t* layout)
{
	// Language based word breaks. These are applied only to specific sections of script.
	const hb_language_t lang_ja = hb_language_from_string("ja", 2);
	const hb_language_t lang_zh_hant = hb_language_from_string("zh-hant", 7);
	const hb_language_t lang_zh_hans = hb_language_from_string("zh-hans", 7);
	const hb_language_t lang_th = hb_language_from_string("th", 2);

	for (int32_t i = 0; i < layout->shaping_runs_count; ++i) {
		const skb__shaping_run_t* shaping_run = &layout->shaping_runs[i];
		const skb__content_run_t* content_run = &layout->content_runs[shaping_run->content_run_idx];

		const char* lang = skb_attributes_get_lang(content_run->attributes, layout->params.attribute_collection);
		const hb_language_t hb_lang = hb_language_from_string(lang, -1);

		if (skb__is_japanese_script(shaping_run->script) && hb_language_matches(lang_ja, hb_lang)) {
			// Merge supported runs into one longer one.
			const int32_t start = shaping_run->text_range.start;
			while ((i+1) < layout->shaping_runs_count && skb__is_japanese_script(layout->shaping_runs[i+1].script))
				i++;
			const int32_t end = layout->shaping_runs[i].text_range.end;
			boundary_iterator_t iter = boundary_iterator_init_ja_utf32(layout->text + start, end - start);
			skb__override_line_breaks(layout, start, end, iter);
		} else if (shaping_run->script == SBScriptHANI && (hb_language_matches(lang_zh_hant, hb_lang) || hb_language_matches(lang_zh_hans, hb_lang))) {
			const int32_t start = shaping_run->text_range.start;
			const int32_t end = shaping_run->text_range.end;
			boundary_iterator_t iter = {0};
			if (hb_language_matches(hb_lang, lang_zh_hans))
				iter = boundary_iterator_init_zh_hans_utf32(layout->text + start, end - start);
			else
				iter = boundary_iterator_init_zh_hant_utf32(layout->text + start, end - start);
			skb__override_line_breaks(layout, start, end, iter);
		} else if (shaping_run->script == SBScriptTHAI && hb_language_matches(lang_th, hb_lang)) {
			const int32_t start = shaping_run->text_range.start;
			const int32_t end = shaping_run->text_range.end;
			boundary_iterator_t iter = boundary_iterator_init_th_utf32(layout->text + start, end - start);
			skb__override_line_breaks(layout, start, end, iter);
		}
	}
}


static bool skb__allow_letter_spacing(uint8_t script)
{
	// These scripts have cursive connection, and can't handle letter spacing.
	switch (script) {
		case SBScriptARAB:	// Arabic
		case SBScriptNKOO:	// Nko
		case SBScriptPHLP:	// Psalter_Pahlavi
		case SBScriptMAND:	// Mandaic
		case SBScriptMONG:	// Mongolian
		case SBScriptPHAG:	// Phags_Pa
		case SBScriptDEVA:	// Devanagari
		case SBScriptBENG:	// Bengali
		case SBScriptGURU:	// Gurmukhi
		case SBScriptMODI:	// Modi
		case SBScriptSHRD:	// Sharada
		case SBScriptSYLO:	// Syloti_Nagric
		case SBScriptTIRH:	// Tirhuta
		case SBScriptOGAM:	// Ogham
			return false;
	}
	return true;
}

static void skb__build_layout(skb__layout_build_context_t* build_context, skb_layout_t* layout)
{
	// Itemize text into runs of same direction and script. A run of emojis is treated the same as script.
	skb__itemize(build_context, layout);

	// Apply run attribs to text properties
	for (int32_t i = 0; i < layout->shaping_runs_count; ++i) {
		const skb__shaping_run_t* shaping_run = &layout->shaping_runs[i];
		for (int32_t j = shaping_run->text_range.start; j < shaping_run->text_range.end; j++) {
			SKB_SET_FLAG(layout->text_props[j].flags, SKB_TEXT_PROP_EMOJI, shaping_run->is_emoji);
//			layout->text_props[j].direction = shaping_run->direction;
			layout->text_props[j].script = shaping_run->script;
		}
	}

	// Handle word breaks for languages what do not have word break characters.
	skb__apply_lang_based_word_breaks(build_context, layout);

	// Shape runs
	layout->clusters_count = 0;
	layout->glyphs_count = 0;

	hb_buffer_t* buffer = hb_buffer_create();
	for (int32_t i = 0; i < layout->shaping_runs_count; ++i) {
		skb__shaping_run_t* shaping_run = &layout->shaping_runs[i];
		const skb__content_run_t* content_run = &layout->content_runs[shaping_run->content_run_idx];

		// Check if this run is a replacement object.
		if (content_run->type == SKB_CONTENT_RUN_OBJECT || content_run->type == SKB_CONTENT_RUN_ICON) {
			// Add the replacement object as a glyph.

			const skb_attribute_object_padding_t attr_object_padding = skb_attributes_get_object_padding(content_run->attributes, layout->params.attribute_collection);

			SKB_ARRAY_RESERVE(layout->glyphs, layout->glyphs_count + 1);
			skb_glyph_t* glyph = &layout->glyphs[layout->glyphs_count++];
			glyph->gid = 0;
			glyph->offset_x = skb_is_rtl(shaping_run->direction) ? attr_object_padding.end : attr_object_padding.start;
			glyph->offset_y = attr_object_padding.top;
			glyph->advance_x = content_run->content_width + attr_object_padding.start + attr_object_padding.end;
			shaping_run->glyph_range.start = layout->glyphs_count-1;
			shaping_run->glyph_range.end = layout->glyphs_count;

			SKB_ARRAY_RESERVE(layout->clusters, layout->clusters_count + 1);
			skb_cluster_t* cluster = &layout->clusters[layout->clusters_count++];
			cluster->text_offset = shaping_run->text_range.start;
			cluster->text_count = (uint8_t)(shaping_run->text_range.end - shaping_run->text_range.start);
			cluster->glyphs_offset = layout->glyphs_count - 1;
			cluster->glyphs_count = 1;
			shaping_run->cluster_range.start = layout->clusters_count-1;
			shaping_run->cluster_range.end = layout->clusters_count;

		} else {

			hb_buffer_clear_contents(buffer);
			skb__shape_run(build_context, layout, shaping_run, content_run, buffer, &shaping_run->font_handle, 1, 0);

			// Apply letter and word spacing
			const float letter_spacing = skb_attributes_get_letter_spacing(content_run->attributes, layout->params.attribute_collection);
			const float word_spacing = skb_attributes_get_word_spacing(content_run->attributes, layout->params.attribute_collection);

			for (int32_t ci = shaping_run->cluster_range.start; ci < shaping_run->cluster_range.end; ci++) {
				const skb_cluster_t* cluster = &layout->clusters[ci];

				// Apply spacing at the end of a glyph cluster.
				skb_glyph_t* glyph = &layout->glyphs[cluster->glyphs_offset + cluster->glyphs_count - 1];
				const skb_text_property_t text_props = layout->text_props[cluster->text_offset + cluster->text_count - 1];

				// Apply letter spacing for each grapheme.
				if (text_props.flags & SKB_TEXT_PROP_GRAPHEME_BREAK) {
					if ((text_props.flags & SKB_TEXT_PROP_WHITESPACE) || skb__allow_letter_spacing(text_props.script))
						glyph->advance_x += letter_spacing;
				}

				// Apply word spacing for each white space.
				if (text_props.flags & SKB_TEXT_PROP_WHITESPACE)
					glyph->advance_x += word_spacing;
			}
		}
	}
	hb_buffer_destroy(buffer);

	// Break layout to lines.
	skb__layout_lines(build_context, layout);

	// There are freed in the order they are allocated so that the allocations get unwound.
	SKB_TEMP_FREE(build_context->temp_alloc, build_context->emoji_types_buffer);
}


//
// API
//

static void skb__copy_params_attributes(skb_layout_t* layout, const skb_layout_params_t* params)
{
	// Copy flattened layout attributes
	int32_t layout_attribute_count = skb_attributes_get_copy_flat_count(params->layout_attributes);
	if (layout_attribute_count > 0) {
		SKB_ARRAY_RESERVE(layout->attributes, layout->attributes_count + layout_attribute_count);
		skb_attribute_t* layout_attributes = layout->attributes + layout->attributes_count;
		layout->attributes_count += layout_attribute_count;

		skb_attributes_copy_flat(params->layout_attributes, layout_attributes, layout_attribute_count);
		layout->params.layout_attributes.attributes = layout_attributes;
		layout->params.layout_attributes.attributes_count = layout_attribute_count;
	}
}

skb_layout_t skb_layout_make_empty(void)
{
	return (skb_layout_t) { .should_free_instance = false, };
}

skb_layout_t* skb_layout_create(const skb_layout_params_t* params)
{
	skb_layout_t* layout = skb_malloc(sizeof(skb_layout_t));
	SKB_ZERO_STRUCT(layout);

	if (params) {
		layout->params = *params;
		layout->params.layout_attributes = (skb_attribute_set_t){0};
		skb__copy_params_attributes(layout, params);
	}

	layout->should_free_instance = true;

	return layout;
}

skb_layout_t* skb_layout_create_utf8(skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const char* text, int32_t text_count, skb_attribute_set_t attributes)
{
	const skb_content_run_t run = skb_content_run_make_utf8(text, text_count, attributes, 0);
	return skb_layout_create_from_runs(temp_alloc, params, &run, 1);
}

skb_layout_t* skb_layout_create_utf32(skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const uint32_t* text, int32_t text_count, skb_attribute_set_t attributes)
{
	const skb_content_run_t run = skb_content_run_make_utf32(text, text_count, attributes, 0);
	return skb_layout_create_from_runs(temp_alloc, params, &run, 1);
}

skb_layout_t* skb_layout_create_from_runs(skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const skb_content_run_t* runs, int32_t runs_count)
{
	skb_layout_t* layout = skb_layout_create(params);
	skb_layout_set_from_runs(layout, temp_alloc, params, runs, runs_count);
	return layout;
}

skb_layout_t* skb_layout_create_from_text(skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const skb_text_t* text, skb_attribute_set_t attributes)
{
	skb_layout_t* layout = skb_layout_create(params);
	skb_layout_set_from_text(layout, temp_alloc, params, text, attributes);
	return layout;
}

void skb_layout_set_utf8(skb_layout_t* layout, skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const char* text, int32_t text_count, skb_attribute_set_t attributes)
{
	const skb_content_run_t run = skb_content_run_make_utf8(text, text_count, attributes, 0);
	skb_layout_set_from_runs(layout, temp_alloc, params, &run, 1);
}

void skb_layout_set_utf32(skb_layout_t* layout, skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const uint32_t* text, int32_t text_count, skb_attribute_set_t attributes)
{
	const skb_content_run_t run = skb_content_run_make_utf32(text, text_count, attributes, 0);
	skb_layout_set_from_runs(layout, temp_alloc, params, &run, 1);
}

void skb_layout_reset(skb_layout_t* layout)
{
	assert(layout);

	// Reset without freeing memory.
	layout->text_count = 0;
	layout->layout_runs_count = 0;
	layout->decorations_count = 0;
	layout->glyphs_count = 0;
	layout->lines_count = 0;
	layout->content_runs_count = 0;
	layout->shaping_runs_count = 0;
	layout->attributes_count = 0; //layout->params.layout_attributes.attributes_count; // Keep layout attributes.
	layout->params = (skb_layout_params_t){0};
	layout->bounds = skb_rect2_make_undefined();
	layout->resolved_direction = SKB_DIRECTION_AUTO;
}


static void skb__reserve_text(skb_layout_t* layout, int32_t text_count)
{
	if (text_count > layout->text_cap) {
		layout->text_cap = text_count;
		layout->text = skb_realloc(layout->text, layout->text_cap * sizeof(uint32_t));
		assert(layout->text);
		layout->text_props = skb_realloc(layout->text_props, layout->text_cap * sizeof(skb_text_property_t));
		assert(layout->text_props);
	}
}

static int32_t skb__append_text_utf8(skb_layout_t* layout, const char* utf8, int32_t utf8_len)
{
	if (utf8_len < 0) utf8_len = utf8 ? (int32_t)strlen(utf8) : 0;
	const int32_t new_text_offset = layout->text_count;
	const int32_t new_text_count = skb_utf8_to_utf32(utf8, utf8_len, NULL, 0);
	if (!new_text_count)
		return new_text_count;

	layout->text_count += new_text_count;
	skb__reserve_text(layout, layout->text_count);

	// Convert utf-8 to utf-32 codepoints.
	skb_utf8_to_utf32(utf8, utf8_len, layout->text + new_text_offset, new_text_count);

	memset(layout->text_props + new_text_offset, 0, new_text_count * sizeof(skb_text_property_t));

	return new_text_count;
}

static int32_t skb__append_text_utf32(skb_layout_t* layout, const uint32_t* utf32, int32_t utf32_len)
{
	if (utf32_len < 0) utf32_len = utf32 ? skb_utf32_strlen(utf32) : 0;
	const int32_t new_text_offset = layout->text_count;
	const int32_t new_text_count = utf32_len;
	if (!new_text_count)
		return new_text_count;

	layout->text_count += new_text_count;
	skb__reserve_text(layout, layout->text_count);

	memcpy(layout->text + new_text_offset, utf32, new_text_count * sizeof(uint32_t));
	memset(layout->text_props + new_text_offset, 0, new_text_count * sizeof(skb_text_property_t));

	return utf32_len;
}

static void skb__init_text_props(skb_temp_alloc_t* temp_alloc, const char* lang, const uint32_t* text, skb_text_property_t* text_props, int32_t text_count)
{
	if (!text_count)
		return;

	char* breaks = SKB_TEMP_ALLOC(temp_alloc, char, text_count);

	set_graphemebreaks_utf32(text, text_count, lang, breaks);
	for (int i = 0; i < text_count; i++) {
		if (breaks[i] == GRAPHEMEBREAK_BREAK)
			text_props[i].flags |= SKB_TEXT_PROP_GRAPHEME_BREAK;
	}

	set_wordbreaks_utf32(text, text_count, lang, breaks);
	for (int i = 0; i < text_count; i++) {
		if (breaks[i] == WORDBREAK_BREAK)
			text_props[i].flags |= SKB_TEXT_PROP_WORD_BREAK;
	}

	set_linebreaks_utf32(text, text_count, lang, breaks);
	for (int i = 0; i < text_count; i++) {
		if (breaks[i] == LINEBREAK_MUSTBREAK)
			text_props[i].flags |= SKB_TEXT_PROP_MUST_LINE_BREAK;
		if (breaks[i] == LINEBREAK_ALLOWBREAK)
			text_props[i].flags |= SKB_TEXT_PROP_ALLOW_LINE_BREAK;
		// Allow line break before tabs.
		if (text[i] == SKB_CHAR_HORIZONTAL_TAB && i > 0)
			text_props[i-1].flags |= SKB_TEXT_PROP_ALLOW_LINE_BREAK;
	}

	for (int i = 0; i < text_count; i++) {
		SBGeneralCategory category = SBCodepointGetGeneralCategory(text[i]);
		SKB_SET_FLAG(text_props[i].flags, SKB_TEXT_PROP_CONTROL, category == SBGeneralCategoryCC);
		SKB_SET_FLAG(text_props[i].flags, SKB_TEXT_PROP_WHITESPACE, SBGeneralCategoryIsSeparator(category));
		SKB_SET_FLAG(text_props[i].flags, SKB_TEXT_PROP_PUNCTUATION, SBGeneralCategoryIsPunctuation(category));
	}

	SKB_TEMP_FREE(temp_alloc, breaks);
}

static void skb__init_text_props_from_attributes(skb_temp_alloc_t* temp_alloc, skb_layout_t* layout)
{
	// Init text props for contiguous runs of same language.
	int32_t start_offset = 0;
	int32_t cur_offset = 0;
	const char* prev_lang = NULL;
	for (int32_t i = 0; i < layout->content_runs_count; i++) {
		const skb__content_run_t* content_run = &layout->content_runs[i];
		const char* run_lang = skb_attributes_get_lang(content_run->attributes, layout->params.attribute_collection);

		if (run_lang != prev_lang) {
			if (cur_offset > start_offset)
				skb__init_text_props(temp_alloc, prev_lang, layout->text + start_offset, layout->text_props + start_offset, cur_offset - start_offset);
			prev_lang = run_lang;
			start_offset = cur_offset;
		}
		cur_offset = content_run->text_range.end;
	}
	if (cur_offset > start_offset)
		skb__init_text_props(temp_alloc, prev_lang, layout->text + start_offset, layout->text_props + start_offset, cur_offset - start_offset);
}

typedef struct skb__text_to_runs_context_t {
	skb_content_run_t* content_runs;
	int32_t content_runs_count;
	int32_t content_runs_cap;
	skb_attribute_set_t attributes;
	skb_temp_alloc_t* temp_alloc;
} skb__text_to_runs_context_t;

static void skb__iter_text_run(const skb_text_t* text, skb_range_t range, skb_attribute_span_t** active_spans, int32_t active_spans_count, void* context)
{
	skb__text_to_runs_context_t* ctx = context;

	const uint32_t* utf32 = skb_text_get_utf32(text);

	SKB_TEMP_RESERVE(ctx->temp_alloc, ctx->content_runs, ctx->content_runs_count + 1);
	skb_content_run_t* run = &ctx->content_runs[ctx->content_runs_count++];

	skb_attribute_set_t run_attributes = {
		.parent_set = &ctx->attributes,
	};

	if (active_spans_count > 0) {
		skb_attribute_t* attributes = SKB_TEMP_ALLOC(ctx->temp_alloc, skb_attribute_t, active_spans_count);
		for (int32_t i = 0; i < active_spans_count; i++)
			attributes[i] = active_spans[i]->attribute;
		run_attributes.attributes = attributes;
		run_attributes.attributes_count = active_spans_count;
	}

	*run = skb_content_run_make_utf32(utf32 + range.start, range.end - range.start, run_attributes, 0);
}


void skb_layout_set_from_text(skb_layout_t* layout, skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const skb_text_t* text, skb_attribute_set_t attributes)
{
	assert(layout);
	assert(params);

	skb_temp_alloc_mark_t mark = skb_temp_alloc_save(temp_alloc);

	skb__text_to_runs_context_t ctx = {
		.temp_alloc = temp_alloc,
		.attributes = attributes,
	};
	SKB_TEMP_RESERVE(temp_alloc, ctx.content_runs, 16);

	skb_text_iterate_attribute_runs(text, skb__iter_text_run, &ctx);

	skb_layout_set_from_runs(layout, temp_alloc, params, ctx.content_runs, ctx.content_runs_count);

	skb_temp_alloc_restore(temp_alloc, mark);
}

void skb_layout_set_from_runs(skb_layout_t* layout, skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const skb_content_run_t* runs, int32_t runs_count)
{
	assert(layout);
	assert(params);

	skb_layout_reset(layout);

	layout->params = *params;
	layout->params.layout_attributes = (skb_attribute_set_t){0};
	skb__copy_params_attributes(layout, params);

	int32_t* text_counts = SKB_TEMP_ALLOC(temp_alloc, int32_t, runs_count);

	skb__layout_build_context_t build_context = {0};
	build_context.temp_alloc = temp_alloc;

	// Reserve memory for the text and attributes
	int32_t total_text_count = 0;
	int32_t total_attribs_count = 0;
	for (int32_t i = 0; i < runs_count; i++) {
		const skb_content_run_t* run = &runs[i];
		if (run->type == SKB_CONTENT_RUN_UTF8)
			text_counts[i] = runs[i].utf8.text_count >= 0 ? runs[i].utf8.text_count : (int32_t)strlen(runs[i].utf8.text);
		else if (run->type == SKB_CONTENT_RUN_UTF32)
			text_counts[i] = runs[i].utf32.text_count >= 0 ? runs[i].utf32.text_count : skb_utf32_strlen(runs[i].utf32.text);
		else if (run->type == SKB_CONTENT_RUN_OBJECT || run->type == SKB_CONTENT_RUN_ICON)
			text_counts[i] = 1;
		total_text_count += text_counts[i];
		total_attribs_count += skb_attributes_get_copy_flat_count(runs[i].attributes);
	}
	skb__reserve_text(layout, total_text_count);

	// Reserve space for spans and font features.
	SKB_ARRAY_RESERVE(layout->content_runs, runs_count);
	layout->content_runs_count = runs_count;

	SKB_ARRAY_RESERVE(layout->attributes, total_attribs_count);

	for (int32_t i = 0; i < runs_count; i++) {
		const skb_content_run_t* run = &runs[i];

		int32_t offset = layout->text_count;
		int32_t count = 0;

		float content_width = 0.f;
		float content_height = 0.f;
		intptr_t content_data = 0;

		if (run->type == SKB_CONTENT_RUN_UTF8) {
			count = skb__append_text_utf8(layout, run->utf8.text, text_counts[i]);
		} else if (run->type == SKB_CONTENT_RUN_UTF32) {
			count = skb__append_text_utf32(layout, run->utf32.text, text_counts[i]);
		} else if (run->type == SKB_CONTENT_RUN_OBJECT) {
			// Add replacement text.
			const uint32_t object_str = SKB_CHAR_REPLACEMENT_OBJECT;
			count = skb__append_text_utf32(layout, &object_str, 1);
			content_width = run->object.width;
			content_height = run->object.height;
			content_data = run->object.data;
		} else if (run->type == SKB_CONTENT_RUN_ICON) {
			// Add replacement text.
			const uint32_t object_str = SKB_CHAR_REPLACEMENT_OBJECT;
			count = skb__append_text_utf32(layout, &object_str, 1);
			if (layout->params.icon_collection) {
				if (run->icon.icon_handle) {
					const skb_vec2_t icon_size = skb_icon_collection_calc_proportional_size(layout->params.icon_collection, run->icon.icon_handle, run->icon.width, run->icon.height);
					content_width = icon_size.x;
					content_height = icon_size.y;
					content_data = run->icon.icon_handle;
				}
			}
		}

		// Init temporary content run.
		skb__content_run_t* content_run = &layout->content_runs[i];
		SKB_ZERO_STRUCT(content_run);

		content_run->type = run->type;
		content_run->run_id = run->run_id;
		content_run->text_range.start = offset;
		content_run->text_range.end = offset + count;
		content_run->content_width = content_width;
		content_run->content_height = content_height;
		content_run->content_data = content_data;

		// Copy attributes
		int32_t run_attributes_count = skb_attributes_get_copy_flat_count(run->attributes);
		if (run_attributes_count > 0) {
			SKB_ARRAY_RESERVE(layout->attributes, layout->attributes_count + run_attributes_count);
			skb_attribute_t* run_attributes = layout->attributes + layout->attributes_count;
			layout->attributes_count += run_attributes_count;

			content_run->attributes.attributes = run_attributes;
			content_run->attributes.attributes_count = run_attributes_count;
			skb_attributes_copy_flat(run->attributes, run_attributes, run_attributes_count);
		}
		// Attributes inherit layout's base attributes.
		if (layout->params.layout_attributes.attributes_count > 0)
			content_run->attributes.parent_set = &layout->params.layout_attributes;
	}

	// Patch pointers in case we ended up reallocating attributes.
	int32_t count = 0;
	// Base attributes
	layout->params.layout_attributes.attributes = &layout->attributes[count];
	count += layout->params.layout_attributes.attributes_count;
	// Per run attributes
	for (int32_t i = 0; i < layout->content_runs_count; i++) {
		layout->content_runs[i].attributes.attributes = &layout->attributes[count];
		count += layout->content_runs[i].attributes.attributes_count;
		// Cache font size as it is used a lot.
		layout->content_runs[i].font_size = skb_attributes_get_font_size(layout->content_runs[i].attributes, layout->params.attribute_collection);
	}
	assert(count == layout->attributes_count);

	skb__init_text_props_from_attributes(temp_alloc, layout);

	skb__build_layout(&build_context, layout);

	SKB_TEMP_FREE(build_context.temp_alloc, text_counts);
}

void skb_layout_destroy(skb_layout_t* layout)
{
	if (!layout) return;

	skb_free(layout->attributes);
	skb_free(layout->content_runs);
	skb_free(layout->shaping_runs);
	skb_free(layout->glyphs);
	skb_free(layout->clusters);
	skb_free(layout->layout_runs);
	skb_free(layout->decorations);
	skb_free(layout->text);
	skb_free(layout->text_props);

	bool should_free_instance = layout->should_free_instance;
	SKB_ZERO_STRUCT(layout);

	if (should_free_instance)
		skb_free(layout);
}

const skb_layout_params_t* skb_layout_get_params(const skb_layout_t* layout)
{
	assert(layout);
	return &layout->params;
}

int32_t skb_layout_get_text_count(const skb_layout_t* layout)
{
	assert(layout);
	return layout->text_count;
}

const uint32_t* skb_layout_get_text(const skb_layout_t* layout)
{
	assert(layout);
	return layout->text;
}

const skb_text_property_t* skb_layout_get_text_properties(const skb_layout_t* layout)
{
	assert(layout);
	return layout->text_props;
}

int32_t skb_layout_get_layout_runs_count(const skb_layout_t* layout)
{
	assert(layout);
	return layout->layout_runs_count;
}

const skb_layout_run_t* skb_layout_get_layout_runs(const skb_layout_t* layout)
{
	assert(layout);
	return layout->layout_runs;
}

int32_t skb_layout_get_glyphs_count(const skb_layout_t* layout)
{
	assert(layout);
	return layout->glyphs_count;
}

const skb_glyph_t* skb_layout_get_glyphs(const skb_layout_t* layout)
{
	assert(layout);
	return layout->glyphs;
}

int32_t skb_layout_get_clusters_count(const skb_layout_t* layout)
{
	assert(layout);
	return layout->clusters_count;
}

const skb_cluster_t* skb_layout_get_clusters(const skb_layout_t* layout)
{
	assert(layout);
	return layout->clusters;
}

int32_t skb_layout_get_decorations_count(const skb_layout_t* layout)
{
	assert(layout);
	return layout->decorations_count;
}

const skb_decoration_t* skb_layout_get_decorations(const skb_layout_t* layout)
{
	assert(layout);
	return layout->decorations;
}

int32_t skb_layout_get_lines_count(const skb_layout_t* layout)
{
	assert(layout);
	return layout->lines_count;
}

const skb_layout_line_t* skb_layout_get_lines(const skb_layout_t* layout)
{
	assert(layout);
	return layout->lines;
}

skb_rect2_t skb_layout_get_bounds(const skb_layout_t* layout)
{
	assert(layout);
	return layout->bounds;
}

float skb_layout_get_advance_y(const skb_layout_t* layout)
{
	assert(layout);
	return layout->advance_y;
}

skb_text_direction_t skb_layout_get_resolved_direction(const skb_layout_t* layout)
{
	assert(layout);
	return layout->resolved_direction;
}

int32_t skb_layout_next_grapheme_offset(const skb_layout_t* layout, int32_t text_offset)
{
	text_offset = skb_clampi(text_offset, 0, layout->text_count); // We allow one past the last codepoint as valid insertion point.

	// Find end of the current grapheme.
	while (text_offset < layout->text_count && !(layout->text_props[text_offset].flags & SKB_TEXT_PROP_GRAPHEME_BREAK))
		text_offset++;

	if (text_offset >= layout->text_count)
		return layout->text_count;

	// Step over.
	text_offset++;

	return text_offset;
}

int32_t skb_layout_prev_grapheme_offset(const skb_layout_t* layout, int32_t text_offset)
{
	text_offset = skb_clampi(text_offset, 0, layout->text_count); // We allow one past the last codepoint as valid insertion point.

	if (!layout->text_count)
		return text_offset;

	// Find begining of the current grapheme.
	if (layout->text_count) {
		while ((text_offset - 1) >= 0 && !(layout->text_props[text_offset - 1].flags & SKB_TEXT_PROP_GRAPHEME_BREAK))
			text_offset--;
	}

	if (text_offset <= 0)
		return 0;

	// Step over.
	text_offset--;

	// Find beginning of the previous grapheme.
	while ((text_offset - 1) >= 0 && !(layout->text_props[text_offset - 1].flags & SKB_TEXT_PROP_GRAPHEME_BREAK))
		text_offset--;

	return text_offset;
}


int32_t skb_layout_align_grapheme_offset(const skb_layout_t* layout, int32_t text_offset)
{
	text_offset = skb_clampi(text_offset, 0, layout->text_count); // We allow one past the last codepoint as valid insertion point.

	if (!layout->text_count)
		return text_offset;

	// Find beginning of the current grapheme.
	while ((text_offset - 1) >= 0 && !(layout->text_props[text_offset - 1].flags & SKB_TEXT_PROP_GRAPHEME_BREAK))
		text_offset--;

	if (text_offset <= 0)
		return 0;

	return text_offset;
}


skb_text_position_t skb__caret_prune_control_eol(const skb_layout_t* layout, const skb_layout_line_t* line, skb_text_position_t caret)
{
	if (layout->text_count > 0) {
		// If the caret is at the leading edge of a control character and the end of line, move it to trailing.
		// This is used for selection, mouse drag can place the caret at the "forbidden" location, but mouse click should not.
		if ((caret.affinity == SKB_AFFINITY_LEADING || caret.affinity == SKB_AFFINITY_EOL) && caret.offset == line->last_grapheme_offset) {
			if (layout->text_props[line->last_grapheme_offset].flags & SKB_TEXT_PROP_CONTROL) {
				caret.affinity = SKB_AFFINITY_TRAILING;
			}
		}
	}

	return caret;
}

int32_t skb_layout_get_line_index(const skb_layout_t* layout, skb_text_position_t pos)
{
	int32_t line_idx = SKB_INVALID_INDEX;
	for (int32_t i = 0; i < layout->lines_count; i++) {
		const skb_layout_line_t* line = &layout->lines[i];
		if (pos.offset >= line->text_range.start && pos.offset < line->text_range.end) {
			line_idx = i;
			break;
		}
	}
	if (line_idx == SKB_INVALID_INDEX) {
		if (pos.offset < layout->lines[0].text_range.start)
			line_idx = 0;
		else if (pos.offset >= layout->lines[layout->lines_count-1].text_range.end)
			line_idx = layout->lines_count-1;
	}

	return line_idx;
}

int32_t skb_layout_get_text_offset(const skb_layout_t* layout, skb_text_position_t pos)
{
	if (pos.affinity == SKB_AFFINITY_LEADING || pos.affinity == SKB_AFFINITY_EOL)
		return skb_layout_next_grapheme_offset(layout, pos.offset);
	return skb_clampi(pos.offset, 0, layout->text_count);
}

static skb_range_t skb__get_layout_run_text_range(const skb_layout_t* layout, int32_t run_idx)
{
	const skb_layout_run_t* layout_run = &layout->layout_runs[run_idx];
	if (skb_range_is_empty(layout_run->cluster_range))
		return (skb_range_t){0};

	const skb_cluster_t* first_cluster = &layout->clusters[layout_run->cluster_range.start];
	const skb_cluster_t* last_cluster = &layout->clusters[layout_run->cluster_range.end - 1];
	return (skb_range_t) {
		.start = first_cluster->text_offset,
		.end = last_cluster->text_offset + last_cluster->text_count,
	};
}

static int32_t skb__get_layout_run_index(const skb_layout_t* layout, skb_text_position_t pos)
{
	if (pos.offset < 0 || pos.offset >= layout->text_count)
		return SKB_INVALID_INDEX;

	for (int32_t li = 0; li < layout->lines_count; li++) {
		const skb_layout_line_t* line = &layout->lines[li];
		if (pos.offset >= line->text_range.start && pos.offset < line->text_range.end) {
			for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
				const skb_range_t run_text_range = skb__get_layout_run_text_range(layout, ri);
				if (pos.offset >= run_text_range.start && pos.offset < run_text_range.end)
					return ri;
			}
			break;
		}
	}

	return SKB_INVALID_INDEX;
}

skb_text_direction_t skb_layout_get_text_direction_at(const skb_layout_t* layout, skb_text_position_t pos)
{
	assert(layout);
	const int32_t run_idx = skb__get_layout_run_index(layout, pos);
	if (run_idx != SKB_INVALID_INDEX)
		return layout->layout_runs[run_idx].direction;
	return layout->resolved_direction;
}

skb_text_position_t skb_layout_hit_test_at_line(const skb_layout_t* layout, skb_movement_type_t type, int32_t line_idx, float hit_x)
{
	const skb_layout_line_t* line = &layout->lines[line_idx];

	skb_text_position_t result = {0};

	if (hit_x < line->bounds.x) {
		if (skb_is_rtl(layout->resolved_direction)) {
			result = (skb_text_position_t) {
				.offset = line->last_grapheme_offset,
				.affinity = SKB_AFFINITY_EOL,
			};
		} else {
			result = (skb_text_position_t) {
				.offset = line->text_range.start,
				.affinity = SKB_AFFINITY_SOL,
			};
		}
	} else if (hit_x >= (line->bounds.x + line->bounds.width)) {
		if (skb_is_rtl(layout->resolved_direction)) {
			result = (skb_text_position_t) {
				.offset = line->text_range.start,
				.affinity = SKB_AFFINITY_SOL,
			};
		} else {
			result = (skb_text_position_t) {
				.offset = line->last_grapheme_offset,
				.affinity = SKB_AFFINITY_EOL,
			};
		}
	} else {

		skb_caret_iterator_t caret_iter = skb_caret_iterator_make(layout, line_idx);

		float x = 0.f;
		float advance = 0.f;
		skb_caret_iterator_result_t left = {0};
		skb_caret_iterator_result_t right = {0};

		while (skb_caret_iterator_next(&caret_iter, &x, &advance, &left, &right)) {
			if (hit_x < x) {
				result = left.text_position;
				break;
			}
			if (hit_x < x + advance * 0.5f) {
				result = right.text_position;
				break;
			}
		}
	}

	if (type ==	SKB_MOVEMENT_CARET) {
		// When placing caret, do not allow to place caret after the newline character.
		// Selection can get there, though, so that we can select a whole line.
		result = skb__caret_prune_control_eol(layout, line, result);
	}

	return result;
}

skb_text_position_t skb_layout_hit_test(const skb_layout_t* layout, skb_movement_type_t type, float hit_x, float hit_y)
{
	if (layout->lines_count == 0)
		return (skb_text_position_t){0};

	// Find the row the hit position is at.
	int32_t line_idx = layout->lines_count - 1;
	for (int32_t i = 0; i < layout->lines_count; i++) {
		skb_layout_line_t* line = &layout->lines[i];
		const float bot_y = line->bounds.y + -line->ascender + line->descender;
		if (hit_y < bot_y) {
			line_idx = i;
			break;
		}
	}

	return skb_layout_hit_test_at_line(layout, type, line_idx, hit_x);
}



skb_layout_content_hit_t skb_layout_hit_test_content_at_line(const skb_layout_t* layout, int32_t line_idx, float hit_x)
{
	const skb_layout_line_t* line = &layout->lines[line_idx];

	skb_layout_content_hit_t result = { 0 };

	if (hit_x > line->bounds.x && hit_x < (line->bounds.x + line->bounds.width)) {
		for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
			const skb_layout_run_t* run = &layout->layout_runs[ri];
			if (hit_x < (run->bounds.x + run->bounds.width)) {
				const intptr_t run_id = run->content_run_id;
				if (run_id != 0) {
					result.line_idx = line_idx;
					result.layout_run_idx = ri;
					result.run_id = run->content_run_id;
				}
				break;
			}
		}
	}

	return result;
}

skb_layout_content_hit_t skb_layout_hit_test_content(const skb_layout_t* layout, float hit_x, float hit_y)
{
	if (layout->lines_count == 0)
		return (skb_layout_content_hit_t){ 0 };

	if (hit_y < layout->bounds.y || hit_y > (layout->bounds.y + layout->bounds.height))
		return (skb_layout_content_hit_t){ 0 };

	// Find the row the hit position is at.
	int32_t line_idx = layout->lines_count - 1;
	for (int32_t i = 0; i < layout->lines_count; i++) {
		skb_layout_line_t* line = &layout->lines[i];
		const float bot_y = line->bounds.y + -line->ascender + line->descender;
		if (hit_y < bot_y) {
			line_idx = i;
			break;
		}
	}

	return skb_layout_hit_test_content_at_line(layout, line_idx, hit_x);
}

void skb_layout_get_content_bounds_at_line(const skb_layout_t* layout, int32_t line_idx, intptr_t run_id, skb_content_rect_func_t* callback, void* context)
{
	assert(layout);
	assert(callback);

	// If run id is invalid, nothing to do.
	if (run_id == 0)
		return;

	if (line_idx < 0 || line_idx >= layout->lines_count)
		return;

	const skb_layout_line_t* line = &layout->lines[line_idx];

	for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
		const skb_layout_run_t* run = &layout->layout_runs[ri];
		if (run->content_run_id == run_id) {
			// Combine consecutive runs of same span into one rectangle.
			skb_rect2_t rect = run->bounds;
			while (ri+1 < line->layout_run_range.end && (int32_t)layout->layout_runs[ri+1].content_run_id == run_id) {
				const skb_layout_run_t* next_run = &layout->layout_runs[ri+1];
				rect = skb_rect2_union(rect, next_run->bounds);
				ri++;
			}
			callback(rect, ri, line_idx, context);
		}
	}
}

void skb_layout_get_content_bounds(const skb_layout_t* layout, intptr_t run_id, skb_content_rect_func_t* callback, void* context)
{
	assert(layout);
	assert(callback);

	// If run id is invalid, nothing to do.
	if (run_id == 0)
		return;

	for (int32_t li = 0; li < layout->lines_count; li++) {
		const skb_layout_line_t* line = &layout->lines[li];

		for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
			const skb_layout_run_t* run = &layout->layout_runs[ri];
			if (run->content_run_id == run_id) {
				// Combine consecutive runs of same span into one rectangle.
				skb_rect2_t rect = run->bounds;
				while (ri+1 < line->layout_run_range.end && (int32_t)layout->layout_runs[ri+1].content_run_id == run_id) {
					const skb_layout_run_t* next_run = &layout->layout_runs[ri+1];
					rect = skb_rect2_union(rect, next_run->bounds);
					ri++;
				}
				callback(rect, ri, li, context);
			}
		}
	}
}

skb_text_position_t skb__sanitize_offset(const skb_layout_t* layout, const skb_layout_line_t* line, const skb_text_position_t caret)
{
	bool start_of_line = false;
	bool end_of_line = false;
	int32_t offset = caret.offset;
	if (offset < line->text_range.start) {
		offset = line->text_range.start;
		start_of_line = true;
	}
	if (offset > line->last_grapheme_offset) {
		offset = line->last_grapheme_offset;
		end_of_line = true;
	}

	// Make sure the offset is at the beginning of a grapheme.
	offset = skb_layout_align_grapheme_offset(layout, offset);

	uint8_t affinity = caret.affinity;
	if (affinity == SKB_AFFINITY_NONE)
		affinity = SKB_AFFINITY_TRAILING;

	if (affinity == SKB_AFFINITY_EOL && offset != line->last_grapheme_offset)
		affinity = SKB_AFFINITY_LEADING;
	if (affinity == SKB_AFFINITY_SOL && offset != line->text_range.start)
		affinity = SKB_AFFINITY_TRAILING;

	// Set out of bounds indices to SOL/EOL. This can happen e.g. with insert position.
	if (start_of_line && offset == line->text_range.start)
		affinity = SKB_AFFINITY_SOL;
	if (end_of_line && offset == line->last_grapheme_offset)
		affinity = SKB_AFFINITY_EOL;

	return (skb_text_position_t) {
		.offset = offset,
		.affinity = affinity,
	};
}

skb_visual_caret_t skb_layout_get_visual_caret_at_line(const skb_layout_t* layout, int32_t line_idx, skb_text_position_t pos)
{
	assert(layout);
	assert(layout->lines_count > 0);

	const skb_layout_line_t* line = &layout->lines[line_idx];
	pos = skb__sanitize_offset(layout, line, pos);

	skb_visual_caret_t vis_caret = {
		.x = line->bounds.x,
		.y = line->baseline,
		.slope = 0.f,
		.ascender = line->ascender,
		.descender = line->descender,
		.direction = layout->resolved_direction,
	};

	skb_caret_iterator_t caret_iter = skb_caret_iterator_make(layout, line_idx);

	// Caret style is picked from previous character.
	int32_t caret_style_text_offset = skb_layout_get_text_offset(layout, pos);
	caret_style_text_offset = skb_layout_prev_grapheme_offset(layout, caret_style_text_offset);
	caret_style_text_offset = skb_clampi(caret_style_text_offset, line->text_range.start, line->text_range.end - 1);

	int32_t layout_run_idx = SKB_INVALID_INDEX;
	int32_t glyph_idx = SKB_INVALID_INDEX;
	float x = 0.f;
	float advance = 0.f;
	skb_caret_iterator_result_t left = {0};
	skb_caret_iterator_result_t right = {0};
	bool found_x = false;
	bool found_style = false;

	while (skb_caret_iterator_next(&caret_iter, &x, &advance, &left, &right) && (!found_style || !found_x)) {

		if (left.text_position.offset == caret_style_text_offset && left.text_position.affinity == SKB_AFFINITY_TRAILING) {
			layout_run_idx = left.layout_run_idx;
			glyph_idx = left.glyph_idx;
			found_style = true;
		}
		if (right.text_position.offset == caret_style_text_offset && right.text_position.affinity == SKB_AFFINITY_TRAILING) {
			layout_run_idx = right.layout_run_idx;
			glyph_idx = right.glyph_idx;
			found_style = true;
		}

		if (left.text_position.offset == pos.offset && left.text_position.affinity == pos.affinity) {
			vis_caret.x = x;
			vis_caret.direction = left.direction;
			found_x = true;
		}
		if (right.text_position.offset == pos.offset && right.text_position.affinity == pos.affinity) {
			vis_caret.x = x;
			vis_caret.direction = right.direction;
			found_x = true;
		}
	}

	if (layout_run_idx != SKB_INVALID_INDEX && glyph_idx != SKB_INVALID_INDEX) {
		const skb_layout_run_t* layout_run = &layout->layout_runs[layout_run_idx];
		const float font_size = layout_run->font_size;
		const skb_font_handle_t font_handle = layout_run->font_handle;

		const skb_glyph_t* glyph = &layout->glyphs[right.glyph_idx];
		vis_caret.y = glyph->offset_y;

		if (font_handle) {
			const skb_font_metrics_t font_metrics = skb_font_get_metrics(layout->params.font_collection, font_handle);
			const skb_caret_metrics_t caret_metrics = skb_font_get_caret_metrics(layout->params.font_collection, font_handle);
			vis_caret.ascender = font_metrics.ascender * font_size;
			vis_caret.descender = font_metrics.descender * font_size;
			vis_caret.slope = caret_metrics.slope;
		}
	}

	return vis_caret;
}

skb_visual_caret_t skb_layout_get_visual_caret_at(const skb_layout_t* layout, skb_text_position_t pos)
{
	if (!layout->lines)
		return (skb_visual_caret_t) { 0 };

	const int32_t line_idx = skb_layout_get_line_index(layout, pos);

	return skb_layout_get_visual_caret_at_line(layout, line_idx, pos);
}


skb_text_position_t skb_layout_get_line_start_at(const skb_layout_t* layout, skb_text_position_t pos)
{
	const int32_t line_idx = skb_layout_get_line_index(layout, pos);
	const skb_layout_line_t* line = &layout->lines[line_idx];
	skb_text_position_t result = {
		.offset = line->text_range.start,
		.affinity = SKB_AFFINITY_SOL,
	};
	return result;
}

skb_text_position_t skb_layout_get_line_end_at(const skb_layout_t* layout, skb_text_position_t pos)
{
	const int32_t line_idx = skb_layout_get_line_index(layout, pos);
	const skb_layout_line_t* line = &layout->lines[line_idx];
	skb_text_position_t result = {
		.offset = line->last_grapheme_offset,
		.affinity = SKB_AFFINITY_EOL,
	};
	return skb__caret_prune_control_eol(layout, line, result);
}

skb_text_position_t skb_layout_get_word_start_at(const skb_layout_t* layout, skb_text_position_t pos)
{
	const int32_t line_idx = skb_layout_get_line_index(layout, pos);
	const skb_layout_line_t* line = &layout->lines[line_idx];

	pos = skb__sanitize_offset(layout, line, pos);

	// Not using insert position here, since we want to start from the "character" the user has hit.
	int32_t offset = pos.offset;

	while (offset >= 0) {
		if (layout->text_props[offset-1].flags & SKB_TEXT_PROP_WORD_BREAK) {
			offset = skb_layout_align_grapheme_offset(layout, offset);
			break;
		}
		offset--;
	}

	if (offset < 0)
		offset = 0;

	return (skb_text_position_t) {
		.offset = offset,
		.affinity = SKB_AFFINITY_TRAILING,
	};
}

skb_text_position_t skb_layout_get_word_end_at(const skb_layout_t* layout, skb_text_position_t pos)
{
	const int32_t line_idx = skb_layout_get_line_index(layout, pos);
	const skb_layout_line_t* line = &layout->lines[line_idx];

	pos = skb__sanitize_offset(layout, line, pos);

	// Not using insert position here, since we want to start from the "character" the user has hit.
	int32_t offset = pos.offset;

	while (offset < layout->text_count) {
		if (layout->text_props[offset].flags & SKB_TEXT_PROP_WORD_BREAK) {
			offset = skb_layout_align_grapheme_offset(layout, offset);
			break;
		}
		offset++;
	}

	if (offset >= layout->text_count)
		offset = skb_layout_align_grapheme_offset(layout, layout->text_count-1);

	return (skb_text_position_t) {
		.offset = offset,
		.affinity = SKB_AFFINITY_LEADING,
	};
}

skb_text_position_t skb_layout_get_selection_ordered_start(const skb_layout_t* layout, skb_text_selection_t selection)
{
	const int32_t start_offset = skb_layout_get_text_offset(layout, selection.start_pos);
	const int32_t end_offset = skb_layout_get_text_offset(layout, selection.end_pos);

	if (skb_is_rtl(layout->resolved_direction))
		return start_offset > end_offset ? selection.start_pos : selection.end_pos;

	return start_offset <= end_offset ? selection.start_pos : selection.end_pos;
}

skb_text_position_t skb_layout_get_selection_ordered_end(const skb_layout_t* layout, skb_text_selection_t selection)
{
	const int32_t start_offset = skb_layout_get_text_offset(layout, selection.start_pos);
	const int32_t end_offset = skb_layout_get_text_offset(layout, selection.end_pos);

	if (skb_is_rtl(layout->resolved_direction))
		return start_offset <= end_offset ? selection.start_pos : selection.end_pos;

	return start_offset > end_offset ? selection.start_pos : selection.end_pos;
}

skb_range_t skb_layout_get_selection_text_offset_range(const skb_layout_t* layout, skb_text_selection_t selection)
{
	int32_t start_offset = skb_layout_get_text_offset(layout, selection.start_pos);
	int32_t end_offset = skb_layout_get_text_offset(layout, selection.end_pos);
	return (skb_range_t) {
		.start = skb_mini(start_offset, end_offset),
		.end = skb_maxi(start_offset, end_offset),
	};
}

int32_t skb_layout_get_selection_count(const skb_layout_t* layout, skb_text_selection_t selection)
{
	skb_range_t range = skb_layout_get_selection_text_offset_range(layout, selection);
	return range.end - range.start;
}

void skb_layout_get_selection_bounds(const skb_layout_t* layout, skb_text_selection_t selection, skb_selection_rect_func_t* callback, void* context)
{
	skb_layout_get_selection_bounds_with_offset(layout, 0.f, selection, callback, context);
}

void skb_layout_get_selection_bounds_with_offset(const skb_layout_t* layout, float offset_y, skb_text_selection_t selection, skb_selection_rect_func_t* callback, void* context)
{
	assert(layout);
	assert(callback);

	skb_range_t sel_range = skb_layout_get_selection_text_offset_range(layout, selection);

	for (int32_t li = 0; li < layout->lines_count; li++) {
		const skb_layout_line_t* line = &layout->lines[li];
		if (skb_range_overlap((skb_range_t){line->text_range.start, line->text_range.end}, sel_range)) {

			skb_range_t rect_text_range = {0};
			float rect_start_x = line->bounds.x;
			float rect_end_x = line->bounds.x;
			float x = line->bounds.x;
			bool prev_is_right_adjacent = false;

			for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
				const skb_layout_run_t* layout_run = &layout->layout_runs[ri];

				skb_range_t cluster_range = layout_run->cluster_range;
				int32_t cluster_range_delta = 1;
				if (skb_is_rtl(layout_run->direction)) {
					cluster_range.start = layout_run->cluster_range.end - 1;
					cluster_range.end = layout_run->cluster_range.start - 1;
					cluster_range_delta = -1;
				}

				const bool is_rtl = skb_is_rtl(layout_run->direction);

				for (int32_t ci = cluster_range.start; ci != cluster_range.end; ci += cluster_range_delta) {
					const skb_cluster_t* cluster = &layout->clusters[ci];
					const skb_range_t cluster_text_range = { .start = cluster->text_offset, .end = cluster->text_offset + cluster->text_count };
					const skb_range_t cluster_glyph_range = { .start = cluster->glyphs_offset, .end = cluster->glyphs_offset + cluster->glyphs_count };

					float cluster_width = 0.f;
					for (int32_t gi = cluster_glyph_range.start; gi != cluster_glyph_range.end; gi++)
						cluster_width += layout->glyphs[gi].advance_x;

					skb_range_t selected_cluster_text_range = {
						.start = skb_maxi(cluster_text_range.start, sel_range.start),
						.end = skb_mini(cluster_text_range.end, sel_range.end)
					};

					if (selected_cluster_text_range.start < selected_cluster_text_range.end) {

						// Code codepoint_idx is inside this run.
						// Find number of graphemes and the grapheme index of the cp_offset
						int32_t grapheme_start_idx = 0;
						int32_t grapheme_end_idx = 0;
						int32_t grapheme_count = 0;

						for (int32_t cp_offset = cluster_text_range.start; cp_offset < cluster_text_range.end; cp_offset++) {
							if (cp_offset == selected_cluster_text_range.start)
								grapheme_start_idx = grapheme_count;
							if (cp_offset == selected_cluster_text_range.end)
								grapheme_end_idx = grapheme_count;
							if (layout->text_props[cp_offset].flags & SKB_TEXT_PROP_GRAPHEME_BREAK)
								grapheme_count++;
						}
						if (selected_cluster_text_range.end == cluster_text_range.end)
							grapheme_end_idx = grapheme_count;

						// Interpolate caret location.
						float start_u = (float)(grapheme_start_idx) / (float)grapheme_count;
						float end_u = (float)(grapheme_end_idx) / (float)grapheme_count;

						if (is_rtl) {
							float u = start_u;
							start_u = 1.f - end_u;
							end_u = 1.f - u;
						}

						bool is_left_adjacent = false;
						bool is_right_adjacent = false;
						if (is_rtl) {
							is_left_adjacent = selected_cluster_text_range.end == cluster_text_range.end;
							is_right_adjacent = selected_cluster_text_range.start == cluster_text_range.start;
						} else {
							is_left_adjacent = selected_cluster_text_range.start == cluster_text_range.start;
							is_right_adjacent = selected_cluster_text_range.end == cluster_text_range.end;
						}

						if (prev_is_right_adjacent && is_left_adjacent) {
							// Adjacent, merge with existing.
							rect_text_range.start = skb_mini(rect_text_range.start, selected_cluster_text_range.start);
							rect_text_range.end = skb_maxi(rect_text_range.end, selected_cluster_text_range.end);
							rect_end_x = x + cluster_width * end_u;
						} else {
							// Start new rect
							if (skb_absf(rect_end_x - rect_start_x) > 0.01f) {
								skb_rect2_t rect = {
									.x = rect_start_x,
									.y = offset_y + line->baseline + line->ascender,
									.width = rect_end_x - rect_start_x,
									.height = -line->ascender + line->descender,
								};
								callback(rect, context);
							}
							rect_text_range.start = selected_cluster_text_range.start;
							rect_text_range.end = selected_cluster_text_range.end;
							rect_start_x = x + cluster_width * start_u;
							rect_end_x = x + cluster_width * end_u;
						}

						prev_is_right_adjacent = is_right_adjacent;
					} else {
						prev_is_right_adjacent = 0;
					}

					x += cluster_width;
				}

				if (skb_absf(rect_end_x - rect_start_x) > 0.01f) {
					// Output rect.
					skb_rect2_t rect = {
						.x = rect_start_x,
						.y = offset_y + line->baseline + line->ascender,
						.width = rect_end_x - rect_start_x,
						.height = -line->ascender + line->descender,
					};
					callback(rect, context);
				}
			}
		}
	}
}

// Initializes the iterator to iterate over graphemes in the cluster.
static bool skb__init_cluster_iter(skb_caret_iterator_t* iter)
{
	const skb_layout_t* layout = iter->layout;

	if (iter->cluster_idx == iter->cluster_end) {
		iter->grapheme_pos = 0;
		iter->grapheme_end = 0;
		iter->advance = 0.f;
		iter->glyph_idx = SKB_INVALID_INDEX;
		return false;
	}

	const skb_layout_run_t* cur_layout_run = &layout->layout_runs[iter->layout_run_idx];
	const skb_cluster_t* cur_cluster = &layout->clusters[iter->cluster_idx];
	skb_range_t text_range = { .start = cur_cluster->text_offset, .end = cur_cluster->text_offset + cur_cluster->text_count };
	skb_range_t glyph_range = { .start = cur_cluster->glyphs_offset, .end = cur_cluster->glyphs_offset + cur_cluster->glyphs_count };

	int32_t grapheme_count = 0;
	for (int32_t ti = text_range.start; ti < text_range.end; ti++) {
		if (layout->text_props[ti].flags & SKB_TEXT_PROP_GRAPHEME_BREAK)
			grapheme_count++;
	}

	float cluster_width = 0.f;
	for (int32_t gi = glyph_range.start; gi < glyph_range.end; gi++)
		cluster_width += layout->glyphs[gi].advance_x;

	iter->advance = (grapheme_count > 0) ? (cluster_width / (float)grapheme_count) : 0.f;

	if (skb_is_rtl(cur_layout_run->direction)) {
		iter->grapheme_pos = skb_layout_align_grapheme_offset(layout, text_range.end - 1);
		iter->grapheme_end = text_range.start - 1;
	} else {
		iter->grapheme_pos = text_range.start;
		iter->grapheme_end = text_range.end;
	}

	iter->glyph_idx = cur_cluster->glyphs_offset;

	return true;
}

skb_caret_iterator_t skb_caret_iterator_make(const skb_layout_t* layout, int32_t line_idx)
{
	assert(layout);
	assert(line_idx >= 0 && line_idx < layout->lines_count);

	const bool line_is_rtl = skb_is_rtl(layout->resolved_direction);
	skb_caret_iterator_t iter = {0};
	const skb_layout_line_t* line = &layout->lines[line_idx];

	iter.layout = layout;
	iter.line_first_grapheme_offset = line->text_range.start;
	iter.line_last_grapheme_offset = line->last_grapheme_offset;
	iter.end_of_line = false;

	iter.x = line->bounds.x;
	iter.advance = 0.f;

	// Previous caret is at the start of the line.
	if (line_is_rtl) {
		iter.pending_left.text_position.offset = iter.line_last_grapheme_offset;
		iter.pending_left.text_position.affinity = SKB_AFFINITY_EOL;
	} else {
		iter.pending_left.text_position.offset = iter.line_first_grapheme_offset;
		iter.pending_left.text_position.affinity = SKB_AFFINITY_SOL;
	}
	iter.pending_left.direction = layout->resolved_direction;
	iter.pending_left.layout_run_idx = line->layout_run_range.start;
	iter.pending_left.glyph_idx = SKB_INVALID_INDEX;
	iter.pending_left.cluster_idx = SKB_INVALID_INDEX;
	if (iter.layout_run_idx != iter.layout_run_end) {
		const skb_layout_run_t* first_run = &layout->layout_runs[iter.layout_run_idx];
		iter.pending_left.glyph_idx = first_run->glyph_range.start;
		iter.pending_left.cluster_idx = skb_is_rtl(first_run->direction) ? first_run->cluster_range.end - 1 : first_run->cluster_range.start;
	}

	// Iterate over layout runs on the line.
	iter.layout_run_idx = line->layout_run_range.start;
	iter.layout_run_end = line->layout_run_range.end;

	// Prune layout runs that cannot be selected.
	if (iter.layout_run_idx != iter.layout_run_end && layout->layout_runs[iter.layout_run_idx].content_run_idx == SKB_INVALID_INDEX)
		iter.layout_run_idx++;
	if (iter.layout_run_idx != iter.layout_run_end && layout->layout_runs[iter.layout_run_end-1].content_run_idx == SKB_INVALID_INDEX)
		iter.layout_run_end--;

	// Iterate over clusters on the layout run.
	if (iter.layout_run_idx != iter.layout_run_end) {
		const skb_layout_run_t* first_run = &layout->layout_runs[iter.layout_run_idx];
		iter.cluster_idx = skb_is_rtl(first_run->direction) ? first_run->cluster_range.end - 1 : first_run->cluster_range.start;
		iter.cluster_end = skb_is_rtl(first_run->direction) ? first_run->cluster_range.start - 1 : first_run->cluster_range.end;
	} else {
		iter.layout_run_idx = SKB_INVALID_INDEX;
		iter.layout_run_end = SKB_INVALID_INDEX;
		iter.end_of_runs = true;
	}

	// Iterate over graphemes on the cluster.
	skb__init_cluster_iter(&iter);

	return iter;
}

bool skb_caret_iterator_next(skb_caret_iterator_t* iter, float* x, float* advance, skb_caret_iterator_result_t* left, skb_caret_iterator_result_t* right)
{
	if (iter->end_of_line)
		return false;

	const skb_layout_t* layout = iter->layout;
	const bool line_is_rtl = skb_is_rtl(layout->resolved_direction);

	// Carry over from previous update.
	*left = iter->pending_left;
	*x = iter->x;
	*advance = iter->advance;

	if (iter->end_of_runs) {
		// End of line
		if (line_is_rtl) {
			right->text_position.offset = iter->line_first_grapheme_offset;
			right->text_position.affinity = SKB_AFFINITY_SOL;
		} else {
			right->text_position.offset = iter->line_last_grapheme_offset;
			right->text_position.affinity = SKB_AFFINITY_EOL;
		}
		right->direction = layout->resolved_direction;
		if (iter->layout_run_end != SKB_INVALID_INDEX) {
			const skb_layout_run_t* cur_layout_run = &layout->layout_runs[iter->layout_run_end - 1];
			right->layout_run_idx = iter->layout_run_end - 1;
			right->glyph_idx = cur_layout_run->glyph_range.end - 1;
			right->cluster_idx = skb_is_rtl(cur_layout_run->direction) ? cur_layout_run->cluster_range.end - 1 : cur_layout_run->cluster_range.start;
		} else {
			right->layout_run_idx = SKB_INVALID_INDEX;
			right->glyph_idx = SKB_INVALID_INDEX;
			right->cluster_idx = SKB_INVALID_INDEX;
		}

		iter->end_of_line = true;
	} else {
		const skb_layout_run_t* cur_layout_run = &layout->layout_runs[iter->layout_run_idx];

		right->text_position.offset = iter->grapheme_pos;
		right->text_position.affinity = skb_is_rtl(cur_layout_run->direction) ? SKB_AFFINITY_LEADING : SKB_AFFINITY_TRAILING; // LTR = trailing;
		right->direction = cur_layout_run->direction;
		right->glyph_idx = iter->glyph_idx;
		right->cluster_idx = iter->cluster_idx;
		right->layout_run_idx = iter->layout_run_idx;

		iter->pending_left.text_position.offset = iter->grapheme_pos;
		iter->pending_left.text_position.affinity = skb_is_rtl(cur_layout_run->direction) ? SKB_AFFINITY_TRAILING : SKB_AFFINITY_LEADING; // LTR = leading;
		iter->pending_left.direction = cur_layout_run->direction;
		iter->pending_left.glyph_idx = iter->glyph_idx;
		iter->pending_left.cluster_idx = iter->cluster_idx;
		iter->pending_left.layout_run_idx = iter->layout_run_idx;

		// Advance to next state
		if (!iter->end_of_runs) {
			iter->x += iter->advance;

			// Advance cluster
			bool end_of_graphemes = false;
			if (skb_is_rtl(cur_layout_run->direction)) {
				iter->grapheme_pos = iter->grapheme_pos > 0 ? skb_layout_prev_grapheme_offset(layout, iter->grapheme_pos) : -1;
				end_of_graphemes = iter->grapheme_pos <= iter->grapheme_end;
			} else {
				iter->grapheme_pos = skb_layout_next_grapheme_offset(layout, iter->grapheme_pos);
				end_of_graphemes = iter->grapheme_pos >= iter->grapheme_end;
			}

			bool end_of_clusters = false;
			if (end_of_graphemes) {
				if (skb_is_rtl(cur_layout_run->direction))
					iter->cluster_idx--;
				else
					iter->cluster_idx++;
				end_of_clusters = iter->cluster_idx == iter->cluster_end;
				if (!end_of_clusters) {
					// Start new cluster
					skb__init_cluster_iter(iter);
				}
			}

			if (end_of_clusters) {
				iter->layout_run_idx++;
				iter->end_of_runs = iter->layout_run_idx == iter->layout_run_end;
				if (!iter->end_of_runs) {
					// Start new run
					cur_layout_run = &layout->layout_runs[iter->layout_run_idx];
					iter->cluster_idx = skb_is_rtl(cur_layout_run->direction) ? cur_layout_run->cluster_range.end - 1 : cur_layout_run->cluster_range.start;
					iter->cluster_end = skb_is_rtl(cur_layout_run->direction) ? cur_layout_run->cluster_range.start - 1 : cur_layout_run->cluster_range.end;
					// Start new cluster
					skb__init_cluster_iter(iter);
				}
			}
		}
	}

	return true;
}
