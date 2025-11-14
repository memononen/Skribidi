// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_RICH_TEXT_H
#define SKB_RICH_TEXT_H

#include <stdint.h>
#include <stdbool.h>
#include "skb_common.h"
#include "skb_attributes.h"
#include "skb_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup rich_text Rich Text
 *
 * Rich text defines multiple paragraphs of text. Each paragraph can have its own paragraph attributes, and the paragraph text is represented as attributed text.
 *
 * @{
 */

typedef struct skb_rich_text_t skb_rich_text_t;

typedef struct skb_rich_text_change_t {
	int32_t start_paragraph_idx;
	int32_t removed_paragraph_count;
	int32_t inserted_paragraph_count;
	skb_text_position_t edit_end_position;
} skb_rich_text_change_t;

skb_rich_text_t* skb_rich_text_create(void);
void skb_rich_text_destroy(skb_rich_text_t* rich_text);
void skb_rich_text_reset(skb_rich_text_t* rich_text);

int32_t skb_rich_text_get_utf32_count(const skb_rich_text_t* rich_text);
int32_t skb_rich_text_get_utf8_count_in_range(const skb_rich_text_t* rich_text, skb_text_range_t text_range);
int32_t skb_rich_text_get_utf8_in_range(const skb_rich_text_t* rich_text, skb_text_range_t text_range, char* utf8, int32_t utf8_cap);
int32_t skb_rich_text_get_utf32_count_in_range(const skb_rich_text_t* rich_text, skb_text_range_t text_range);
int32_t skb_rich_text_get_utf32_in_range(const skb_rich_text_t* rich_text, skb_text_range_t text_range, uint32_t* utf32, int32_t utf32_cap);

skb_range_t skb_rich_text_get_paragraphs_range_from_text_range(skb_rich_text_t* rich_text, skb_text_range_t text_range);
int32_t skb_rich_text_get_paragraphs_count(const skb_rich_text_t* rich_text);
const skb_text_t* skb_rich_text_get_paragraph_text(const skb_rich_text_t* rich_text, int32_t paragraph_idx);
skb_attribute_set_t skb_rich_text_get_paragraph_attributes(const skb_rich_text_t* rich_text, int32_t paragraph_idx);
int32_t skb_rich_text_get_paragraph_text_utf32_count(const skb_rich_text_t* rich_text, int32_t paragraph_idx);
int32_t skb_rich_text_get_paragraph_text_offset(const skb_rich_text_t* text, int32_t paragraph_idx);
uint32_t skb_rich_text_get_paragraph_version(const skb_rich_text_t* rich_text, int32_t paragraph_idx);

skb_rich_text_change_t skb_rich_text_append(skb_rich_text_t* rich_text, const skb_rich_text_t* source_rich_text);
skb_rich_text_change_t skb_rich_text_append_range(skb_rich_text_t* rich_text, const skb_rich_text_t* source_rich_text, skb_text_range_t source_text_range);
skb_rich_text_change_t skb_rich_text_append_paragraph(skb_rich_text_t* rich_text, skb_attribute_set_t paragraph_attributes);
skb_rich_text_change_t skb_rich_text_append_text(skb_rich_text_t* rich_text, skb_temp_alloc_t* temp_alloc, const skb_text_t* source_text);
skb_rich_text_change_t skb_rich_text_append_text_range(skb_rich_text_t* rich_text, skb_temp_alloc_t* temp_alloc, const skb_text_t* source_text, skb_text_range_t source_text_range);
skb_rich_text_change_t skb_rich_text_append_utf8(skb_rich_text_t* rich_text, skb_temp_alloc_t* temp_alloc, const char* utf8, int32_t utf8_count, skb_attribute_set_t attributes);
skb_rich_text_change_t skb_rich_text_append_utf8_with_payload(skb_rich_text_t* rich_text, skb_temp_alloc_t* temp_alloc, const char* utf8, int32_t utf8_count, skb_attribute_set_t attributes, uint8_t span_flags, const skb_data_blob_t* payload);
skb_rich_text_change_t skb_rich_text_append_utf32(skb_rich_text_t* rich_text, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_count, skb_attribute_set_t attributes);
skb_rich_text_change_t skb_rich_text_append_utf32_with_payload(skb_rich_text_t* rich_text, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_count, skb_attribute_set_t attributes, uint8_t span_flags, const skb_data_blob_t* payload);

skb_rich_text_change_t skb_rich_text_insert(skb_rich_text_t* rich_text, skb_text_range_t text_range, const skb_rich_text_t* source_rich_text);
skb_rich_text_change_t skb_rich_text_insert_range(skb_rich_text_t* rich_text, skb_text_range_t text_range, const skb_rich_text_t* source_rich_text, skb_text_range_t source_text_range);

skb_rich_text_change_t skb_rich_text_remove(skb_rich_text_t* rich_text, skb_text_range_t text_range);

void skb_rich_text_copy_attributes_in_range(skb_rich_text_t* rich_text, const skb_rich_text_t* source_rich_text, skb_text_range_t source_text_range);
void skb_rich_text_insert_attributes(skb_rich_text_t* rich_text, skb_text_range_t text_range, const skb_rich_text_t* source_rich_text);

void skb_rich_text_set_paragraph_attribute(skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute);
void skb_rich_text_set_paragraph_attribute_delta(skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute);

void skb_rich_text_set_attribute(skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute);
void skb_rich_text_set_attribute_with_payload(skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute, uint8_t span_flags, const skb_data_blob_t* payload);

void skb_rich_text_clear_attribute(skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute);
void skb_rich_text_clear_all_attributes(skb_rich_text_t* rich_text, skb_text_range_t text_range);
int32_t skb_rich_text_get_attribute_count(const skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute);
bool skb_rich_text_has_attribute(const skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute);
skb_text_range_t skb_rich_text_get_attribute_text_range(const skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute);
skb_data_blob_t* skb_rich_text_get_attribute_payload(const skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_attribute_t attribute);

typedef bool skb_rich_text_remove_func_t(uint32_t codepoint, int32_t paragraph_idx, int32_t text_offset, void* context);

void skb_rich_text_remove_if(skb_rich_text_t* rich_text, skb_rich_text_remove_func_t* filter_func, void* context);

skb_paragraph_position_t skb_rich_text_get_paragraph_position_from_text_position(const skb_rich_text_t* rich_text, skb_text_position_t text_pos, skb_affinity_usage_t affinity_usage);
skb_paragraph_range_t skb_rich_text_get_paragraph_range_from_text_range(const skb_rich_text_t* rich_text, skb_text_range_t text_range, skb_affinity_usage_t affinity_usage);

int32_t skb_rich_text_get_offset_from_text_position(const skb_rich_text_t* rich_text, skb_text_position_t text_pos);
skb_range_t skb_rich_text_get_offset_range_from_text_range(const skb_rich_text_t* rich_text, skb_text_range_t text_range);

skb_text_position_t skb_rich_text_get_next_grapheme_pos(const skb_rich_text_t* text, skb_text_position_t text_pos);
skb_text_position_t skb_rich_text_get_prev_grapheme_pos(const skb_rich_text_t* text, skb_text_position_t text_pos);
skb_text_position_t skb_rich_text_align_grapheme_pos(const skb_rich_text_t* text, skb_text_position_t text_pos);

/** @} */

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // SKB_RICH_TEXT_H
