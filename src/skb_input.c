// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "skb_input.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "graphemebreak.h"
#include "hb.h"

#include "skb_layout.h"
#include "skb_common.h"

// From skb_layout.c
skb_text_position_t skb__caret_prune_control_eol(const skb_layout_t* layout, const skb_layout_line_t* line, skb_text_position_t caret);

//
// Text edit
//

typedef struct skb__input_position_t {
	int32_t paragraph_idx;
	int32_t line_idx;
	int32_t paragraph_offset;
	int32_t text_offset;
} skb__input_position_t;

typedef struct skb__input_range_t {
	skb__input_position_t start;
	skb__input_position_t end;
} skb__input_range_t;

typedef enum {
	SKB_SANITIZE_ADJUST_AFFINITY,
	SKB_SANITIZE_IGNORE_AFFINITY = 1,
} skb_sanitize_affinity_t;

typedef struct skb__input_paragraph_t {
	skb_layout_t* layout;
	uint32_t* text;
	int32_t text_count;
	int32_t text_cap;
	int32_t text_start_offset;
	float y;
} skb__input_paragraph_t;

typedef struct skb_input_t {
	skb_input_params_t params;
	skb_input_on_change_t* on_change_callback;
	void* on_change_context;
	
	skb__input_paragraph_t* paragraphs;
	int32_t paragraphs_count;
	int32_t paragraphs_cap;

	skb_text_selection_t selection;

	double last_click_time;
	float drag_start_x;
	float drag_start_y;
	float preferred_x;
	int32_t click_count;
	skb_text_selection_t drag_initial_selection;
	uint8_t drag_moved;
	uint8_t drag_mode;
} skb_input_t;


static void skb__update_layout(skb_input_t* input, skb_temp_alloc_t* temp_alloc)
{
	assert(input);

	skb_layout_params_t layout_params = input->params.layout_params;
	layout_params.origin = (skb_vec2_t){ 0 };
	layout_params.ignore_must_line_breaks = 1;
	// TODO: we will need to improve the logic pick up the direction automatically.
	// If left to AUTO, each split paragraph will adjust separately and it's confusing.
	if (layout_params.base_direction == SKB_DIR_AUTO)
		layout_params.base_direction = SKB_DIR_LTR;

	float y = 0.f;

	for (int32_t i = 0; i < input->paragraphs_count; i++) {
		skb__input_paragraph_t* paragraph = &input->paragraphs[i];

		// Layout is removed from the lines where we 
		if (!paragraph->layout)
			paragraph->layout = skb_layout_create_utf32(temp_alloc, &layout_params, paragraph->text, paragraph->text_count, &input->params.text_attribs);
		assert(paragraph->layout);
		
		paragraph->y = y;
		
		skb_rect2_t layout_bounds = skb_layout_get_bounds(paragraph->layout);
		y += layout_bounds.height;
	}
}

static skb_range_t* skb__split_text_into_paragraphs(skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len, int32_t* out_paragraphs_count)
{
	// Split input to paragraphs.
	int32_t start_offset = 0;
	int32_t offset = 0;

	int32_t paragraphs_count = 0;
	int32_t paragraphs_cap = 8;
	skb_range_t* paragraphs = SKB_TEMP_ALLOC(temp_alloc, skb_range_t, paragraphs_cap);
	
	while (offset < utf32_len) {
		if (skb_is_paragraph_separator(utf32[offset])) {
			// Handle CRLF
			if (offset + 1 < utf32_len && utf32[offset] == SKB_CHAR_CARRIAGE_RETURN && utf32[offset+1] == SKB_CHAR_LINE_FEED)
				offset++; // Skip over the separator
			offset++; // Skip over the separator

			// Create new paragraph
			if (paragraphs_count + 1 > paragraphs_cap) {
				paragraphs_cap += paragraphs_cap/2;
				paragraphs = SKB_TEMP_REALLOC(temp_alloc, paragraphs, skb_range_t, paragraphs_cap);
			}
			skb_range_t* new_paragraph = &paragraphs[paragraphs_count++];
			memset(new_paragraph, 0, sizeof(skb_range_t));
			new_paragraph->start = start_offset;
			new_paragraph->end = offset;
			start_offset = offset;
		} else {
			offset++;
		}
	}

	// The rest
	if (paragraphs_count + 1 > paragraphs_cap) {
		paragraphs_cap += paragraphs_cap/2;
		paragraphs = SKB_TEMP_REALLOC(temp_alloc, paragraphs, skb_range_t, paragraphs_cap);
	}
	skb_range_t* new_paragraph = &paragraphs[paragraphs_count++];
	memset(new_paragraph, 0, sizeof(skb_range_t));
	new_paragraph->start = start_offset;
	new_paragraph->end = offset;

	*out_paragraphs_count = paragraphs_count;
	return paragraphs;
}

static void skb__emit_on_change(skb_input_t* input)
{
	if (input->on_change_callback)
		input->on_change_callback(input, input->on_change_context);
}

skb_input_t* skb_input_create(const skb_input_params_t* params)
{
	assert(params);
	
	skb_input_t* input = skb_malloc(sizeof(skb_input_t));
	memset(input, 0, sizeof(skb_input_t));

	input->params = *params;

	input->preferred_x = -1.f;
	
	return input;
}

void skb_input_set_on_change_callback(skb_input_t* input, skb_input_on_change_t* callback, void* context)
{
	assert(input);
	input->on_change_callback = callback;
	input->on_change_context = context;
}


void skb_input_destroy(skb_input_t* input)
{
	if (!input) return;

	for (int32_t i = 0; i < input->paragraphs_count; i++) {
		skb_layout_destroy(input->paragraphs[i].layout);
		input->paragraphs[i].layout = NULL;
		skb_free(input->paragraphs[i].text);
	}
	skb_free(input->paragraphs);

	memset(input, 0, sizeof(skb_input_t));
	
	skb_free(input);
}

void skb_input_reset(skb_input_t* input, const skb_input_params_t* params)
{
	assert(input);

	if (params)
		input->params = *params;

	for (int32_t i = 0; i < input->paragraphs_count; i++) {
		skb_layout_destroy(input->paragraphs[i].layout);
		input->paragraphs[i].layout = NULL;
		skb_free(input->paragraphs[i].text);
	}
	input->paragraphs_count = 0;

	skb__emit_on_change(input);
}


void skb_input_set_text_utf8(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const char* utf8, int32_t utf8_len)
{
	assert(input);
	
	skb_input_reset(input, NULL);
	
	if (utf8_len < 0) utf8_len = (int32_t)strlen(utf8);

	const int32_t utf32_len = skb_utf8_to_utf32(utf8, utf8_len, NULL, 0);
	uint32_t* utf32 = SKB_TEMP_ALLOC(temp_alloc, uint32_t, utf32_len);
	skb_utf8_to_utf32(utf8, utf8_len, utf32, utf32_len);

	int32_t input_paragraph_count = 0;
	skb_range_t* input_paragraph_ranges = skb__split_text_into_paragraphs(temp_alloc, utf32, utf32_len, &input_paragraph_count);
	assert(input_paragraph_count > 0); // we assume that even for empty input there's one item.

	for (int32_t i = 0; i < input_paragraph_count; i++) {
		// Create new paragraph
		skb_range_t paragraph_range = input_paragraph_ranges[i];
		SKB_ARRAY_RESERVE(input->paragraphs, input->paragraphs_count+1);
		skb__input_paragraph_t* new_paragraph = &input->paragraphs[input->paragraphs_count++];
		memset(new_paragraph, 0, sizeof(skb__input_paragraph_t));
		new_paragraph->text_start_offset = paragraph_range.start;
		new_paragraph->text_count = paragraph_range.end - paragraph_range.start;
		if (new_paragraph->text_count > 0) {
			SKB_ARRAY_RESERVE(new_paragraph->text, new_paragraph->text_count);
			memcpy(new_paragraph->text, utf32 + paragraph_range.start, new_paragraph->text_count * sizeof(uint32_t));
		}
	}
	
	SKB_TEMP_FREE(temp_alloc, input_paragraph_ranges);
	SKB_TEMP_FREE(temp_alloc, utf32);
	
	skb__update_layout(input, temp_alloc);
	skb__emit_on_change(input);
}

void skb_input_set_text_utf32(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len)
{
	assert(input);
	
	skb_input_reset(input, NULL);
	
	if (utf32_len < 0) utf32_len = skb_utf32_strlen(utf32);

	int32_t input_paragraph_count = 0;
	skb_range_t* input_paragraph_ranges = skb__split_text_into_paragraphs(temp_alloc, utf32, utf32_len, &input_paragraph_count);
	assert(input_paragraph_count > 0); // we assume that even for empty input there's one item.

	for (int32_t i = 0; i < input_paragraph_count; i++) {
		// Create new paragraph
		skb_range_t paragraph_range = input_paragraph_ranges[i];
		SKB_ARRAY_RESERVE(input->paragraphs, input->paragraphs_count+1);
		skb__input_paragraph_t* new_paragraph = &input->paragraphs[input->paragraphs_count++];
		memset(new_paragraph, 0, sizeof(skb__input_paragraph_t));
		new_paragraph->text_start_offset = paragraph_range.start;
		new_paragraph->text_count = paragraph_range.end - paragraph_range.start;
		if (new_paragraph->text_count > 0) {
			SKB_ARRAY_RESERVE(new_paragraph->text, new_paragraph->text_count);
			memcpy(new_paragraph->text, utf32 + paragraph_range.start, new_paragraph->text_count * sizeof(uint32_t));
		}
	}

	SKB_TEMP_FREE(temp_alloc, input_paragraph_ranges);

	skb__update_layout(input, temp_alloc);
	skb__emit_on_change(input);
}

int32_t skb_input_get_paragraph_count(skb_input_t* input)
{
	assert(input);
	return input->paragraphs_count;
}

const skb_layout_t* skb_input_get_paragraph_layout(skb_input_t* input, int32_t index)
{
	assert(input);
	return input->paragraphs[index].layout;
}

float skb_input_get_paragraph_offset_y(skb_input_t* input, int32_t index)
{
	assert(input);
	return input->paragraphs[index].y;
}

int32_t skb_input_get_paragraph_text_offset(skb_input_t* input, int32_t index)
{
	assert(input);
	return input->paragraphs[index].text_start_offset;
}

const skb_input_params_t* skb_input_get_params(skb_input_t* input)
{
	assert(input);
	return &input->params;
}

int32_t skb_input_get_text_utf8_count(const skb_input_t* input)
{
	assert(input);

	int32_t count = 0;
	for (int32_t i = 0; i < input->paragraphs_count; i++) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[i];
		count += skb_utf32_to_utf8_count(paragraph->text, paragraph->text_count);
	}
	return count;
}

int32_t skb_input_get_text_utf8(const skb_input_t* input, char* buf, int32_t buf_cap)
{
	assert(input);
	assert(buf);

	int32_t count = 0;
	for (int32_t i = 0; i < input->paragraphs_count; i++) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[i];
		const int32_t cur_buf_cap = skb_maxi(0, buf_cap - count);
		if (cur_buf_cap == 0)
			break;
		char* cur_buf = buf + count;
		count += skb_utf32_to_utf8(paragraph->text, paragraph->text_count, cur_buf, cur_buf_cap);
	}
	return skb_mini(count, buf_cap);
}

int32_t skb_input_get_text_utf32_count(const skb_input_t* input)
{
	assert(input);

	int32_t count = 0;
	for (int32_t i = 0; i < input->paragraphs_count; i++) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[i];
		count += paragraph->text_count;
	}
	
	return count;
}

int32_t skb_input_get_text_utf32(const skb_input_t* input, uint32_t* buf, int32_t buf_cap)
{
	assert(input);

	int32_t count = 0;
	for (int32_t i = 0; i < input->paragraphs_count; i++) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[i];
		const int32_t cur_buf_cap = skb_maxi(0, buf_cap - count);
		const int32_t copy_count = skb_mini(cur_buf_cap, paragraph->text_count);
		if (buf && copy_count > 0)
			memcpy(buf + count, paragraph->text, copy_count * sizeof(uint32_t));
		count += paragraph->text_count;
	}
	
	return count;
}

static skb__input_position_t skb__get_sanitized_position(const skb_input_t* input, skb_text_position_t pos, skb_sanitize_affinity_t sanitize_affinity)
{
	assert(input);
	assert(input->paragraphs_count > 0);

	skb__input_position_t edit_pos = {0};
	
	// Find edit line.
	const skb__input_paragraph_t* last_paragraph = &input->paragraphs[input->paragraphs_count - 1];
	const int32_t total_text_count = last_paragraph->text_start_offset + last_paragraph->text_count;
	if (pos.offset < 0) {
		edit_pos.paragraph_idx = 0;
	} else if (pos.offset >= total_text_count) {
		edit_pos.paragraph_idx = input->paragraphs_count - 1;
	} else {
		for (int32_t i = 0; i < input->paragraphs_count; i++) {
			const skb__input_paragraph_t* paragraph = &input->paragraphs[i];
			if (pos.offset < (paragraph->text_start_offset + paragraph->text_count)) {
				edit_pos.paragraph_idx = i;
				break;
			}
		}
	}

	// Find line
	const skb__input_paragraph_t* cur_paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	
	edit_pos.paragraph_offset = pos.offset - cur_paragraph->text_start_offset;

	const skb_layout_line_t* lines = skb_layout_get_lines(cur_paragraph->layout);
	const int32_t lines_count = skb_layout_get_lines_count(cur_paragraph->layout);
	
	if (edit_pos.paragraph_offset < 0) {
		// We should hit this only when the pos.offset is before the first line.
		edit_pos.line_idx = 0;
		edit_pos.paragraph_offset = 0;
	} else if (edit_pos.paragraph_offset > cur_paragraph->text_count) {
		// We should hit this only when the pos.offset is past the last line.
		edit_pos.line_idx = lines_count - 1;
		edit_pos.paragraph_offset = cur_paragraph->text_count;
	} else {
		for (int32_t i = 0; i < lines_count; i++) {
			const skb_layout_line_t* line = &lines[i];
			if (edit_pos.paragraph_offset < line->text_range.end) {
				edit_pos.line_idx = i;
				break;
			}
		}
	}

	// Align to nearest grapheme.
	edit_pos.paragraph_offset = skb_layout_align_grapheme_offset(cur_paragraph->layout, edit_pos.paragraph_offset);

	// Adjust position based on affinity
	if (sanitize_affinity == SKB_SANITIZE_ADJUST_AFFINITY) {
		if (pos.affinity == SKB_AFFINITY_LEADING || pos.affinity == SKB_AFFINITY_EOL) {
			edit_pos.paragraph_offset = skb_layout_next_grapheme_offset(cur_paragraph->layout, edit_pos.paragraph_offset);
			// Affinity adjustment may push the offset to next edit line
			if (edit_pos.paragraph_offset >= cur_paragraph->text_count) {
				if ((edit_pos.paragraph_idx + 1) < input->paragraphs_count) {
					edit_pos.paragraph_offset = 0;
					edit_pos.paragraph_idx++;
					cur_paragraph = &input->paragraphs[edit_pos.paragraph_idx];
				}
			}
		}
	}

	edit_pos.text_offset = cur_paragraph->text_start_offset + edit_pos.paragraph_offset;

	return edit_pos;
}

static skb__input_range_t skb__get_sanitized_range(const skb_input_t* input, skb_text_selection_t selection)
{
	skb__input_position_t start = skb__get_sanitized_position(input, selection.start_pos, SKB_SANITIZE_ADJUST_AFFINITY);
	skb__input_position_t end = skb__get_sanitized_position(input, selection.end_pos, SKB_SANITIZE_ADJUST_AFFINITY);

	skb__input_range_t result = {0};
	if (start.text_offset <= end.text_offset) {
		result.start = start;
		result.end = end;
	} else {
		result.start = end;
		result.end = start;
	}
	return result;
}

static skb__input_position_t skb__get_next_grapheme_pos(const skb_input_t* input, skb__input_position_t edit_pos)
{
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];

	edit_pos.paragraph_offset = skb_layout_next_grapheme_offset(paragraph->layout, edit_pos.paragraph_offset);
	
	// Affinity adjustment may push the offset to next edit line
	if (edit_pos.paragraph_offset >= paragraph->text_count) {
		if ((edit_pos.paragraph_idx + 1) < input->paragraphs_count) {
			edit_pos.paragraph_offset = 0;
			edit_pos.paragraph_idx++;
			paragraph = &input->paragraphs[edit_pos.paragraph_idx];
		}
	}

	// Update layout line index
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const int32_t lines_count = skb_layout_get_lines_count(paragraph->layout);

	edit_pos.line_idx = lines_count - 1;
	for (int32_t i = 0; i < lines_count; i++) {
		const skb_layout_line_t* line = &lines[i];
		if (edit_pos.paragraph_offset < line->text_range.end) {
			edit_pos.line_idx = i;
			break;
		}
	}
	
	return edit_pos;
}

static skb__input_position_t skb__get_prev_grapheme_pos(const skb_input_t* input, skb__input_position_t edit_pos)
{
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];

	if (edit_pos.paragraph_offset == 0) {
		if ((edit_pos.paragraph_idx - 1) >= 0) {
			edit_pos.paragraph_idx--;
			paragraph = &input->paragraphs[edit_pos.paragraph_idx];
			edit_pos.paragraph_offset = skb_layout_get_text_count(paragraph->layout);
		}
	}
	
	edit_pos.paragraph_offset = skb_layout_prev_grapheme_offset(paragraph->layout, edit_pos.paragraph_offset);
	
	// Update layout line index
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const int32_t lines_count = skb_layout_get_lines_count(paragraph->layout);

	edit_pos.line_idx = lines_count - 1;
	for (int32_t i = 0; i < lines_count; i++) {
		const skb_layout_line_t* line = &lines[i];
		if (edit_pos.paragraph_offset < line->text_range.end) {
			edit_pos.line_idx = i;
			break;
		}
	}

	return edit_pos;
}


static skb_text_position_t skb_input_get_line_start_at(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_ADJUST_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];

	skb_text_position_t result = {
		.offset = paragraph->text_start_offset + line->text_range.start,
		.affinity = SKB_AFFINITY_SOL,
	};
	return result;
}

skb_text_position_t skb_input_get_line_end_at(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_ADJUST_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];

	skb_text_position_t result = {
		.offset = paragraph->text_start_offset + line->last_grapheme_offset,
		.affinity = SKB_AFFINITY_EOL,
	};
	return skb__caret_prune_control_eol(paragraph->layout, line, result);
} 

skb_text_position_t skb_input_get_word_start_at(const skb_input_t* input, skb_text_position_t pos)
{
	// Ignoring affinity, since we want to start from the "character" the user has hit.
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];

	int32_t offset = edit_pos.paragraph_offset;

	const skb_text_property_t* text_props = skb_layout_get_text_properties(paragraph->layout);

	if (text_props) {
		while (offset >= 0) {
			if (text_props[offset-1].is_word_break) {
				offset = skb_layout_align_grapheme_offset(paragraph->layout, offset);
				break;
			}
			offset--;
		}
	}

	if (offset < 0)
		offset = 0;
	
	return (skb_text_position_t) {
		.offset = paragraph->text_start_offset + offset,
		.affinity = SKB_AFFINITY_TRAILING,
	};
}

skb_text_position_t skb_input_get_word_end_at(const skb_input_t* input, skb_text_position_t pos)
{
	// Ignoring affinity, since we want to start from the "character" the user has hit.
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];

	int32_t offset = edit_pos.paragraph_offset;

	const int32_t text_count = skb_layout_get_text_count(paragraph->layout);
	const skb_text_property_t* text_props = skb_layout_get_text_properties(paragraph->layout);

	if (text_props) {
		while (offset < text_count) {
			if (text_props[offset].is_word_break) {
				offset = skb_layout_align_grapheme_offset(paragraph->layout, offset);
				break;
			}
			offset++;
		}
	}

	if (offset >= text_count)
		offset = skb_layout_align_grapheme_offset(paragraph->layout, text_count-1);
	
	return (skb_text_position_t) {
		.offset = paragraph->text_start_offset + offset,
		.affinity = SKB_AFFINITY_LEADING,
	};
}

skb_text_position_t skb_input_get_selection_ordered_start(const skb_input_t* input, skb_text_selection_t selection)
{
	skb__input_position_t start = skb__get_sanitized_position(input, selection.start_pos, SKB_SANITIZE_ADJUST_AFFINITY);
	skb__input_position_t end = skb__get_sanitized_position(input, selection.end_pos, SKB_SANITIZE_ADJUST_AFFINITY);

	const skb__input_paragraph_t* paragraph = &input->paragraphs[end.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const bool line_is_rtl = lines[end.line_idx].is_rtl;

	if (line_is_rtl)
		return start.text_offset > end.text_offset ? selection.start_pos : selection.end_pos;
	
	return start.text_offset <= end.text_offset ? selection.start_pos : selection.end_pos;
}

skb_text_position_t skb_input_get_selection_ordered_end(const skb_input_t* input, skb_text_selection_t selection)
{
	skb__input_position_t start = skb__get_sanitized_position(input, selection.start_pos, SKB_SANITIZE_ADJUST_AFFINITY);
	skb__input_position_t end = skb__get_sanitized_position(input, selection.end_pos, SKB_SANITIZE_ADJUST_AFFINITY);

	const skb__input_paragraph_t* paragraph = &input->paragraphs[end.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const bool line_is_rtl = lines[end.line_idx].is_rtl;

	if (line_is_rtl)
		return start.text_offset <= end.text_offset ? selection.start_pos : selection.end_pos;
	
	return start.text_offset > end.text_offset ? selection.start_pos : selection.end_pos;
}

static bool skb__is_at_first_line(const skb_input_t* input, skb__input_position_t edit_pos)
{
	return edit_pos.paragraph_idx == 0 && edit_pos.line_idx == 0;
}

static bool skb__is_at_last_line(const skb_input_t* input, skb__input_position_t edit_pos)
{
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const int32_t lines_count = skb_layout_get_lines_count(paragraph->layout);
	return (edit_pos.paragraph_idx == input->paragraphs_count - 1) && (edit_pos.line_idx == lines_count - 1);
}

static bool skb__is_at_start_of_line(const skb_input_t* input, skb__input_position_t edit_pos)
{
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];
	return edit_pos.paragraph_offset == line->text_range.start;
}

static bool skb__is_past_end_of_line(const skb_input_t* input, skb__input_position_t edit_pos)
{
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];
	return edit_pos.paragraph_offset > line->last_grapheme_offset;
}

static bool skb__is_rtl(const skb_input_t* input, skb__input_position_t edit_pos, uint8_t affinity)
{
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];

	if (affinity == SKB_AFFINITY_EOL || affinity == SKB_AFFINITY_SOL)
		return line->is_rtl;

	if (edit_pos.paragraph_offset > line->last_grapheme_offset)
		return line->is_rtl;

	const int32_t text_count = skb_layout_get_text_count(paragraph->layout);

	if (text_count == 0)
		return line->is_rtl;

	const skb_text_property_t* text_props = skb_layout_get_text_properties(paragraph->layout);

	return text_props[edit_pos.paragraph_offset].is_rlt;
}

static bool skb__are_on_same_line(skb__input_position_t a, skb__input_position_t b)
{
	return a.paragraph_idx == b.paragraph_idx && a.line_idx == b.line_idx;
}

static skb_text_position_t skb__advance_forward(const skb_input_t* input, skb__input_position_t cur_edit_pos, uint8_t cur_affinity)
{
	skb__input_position_t next_edit_pos = skb__get_next_grapheme_pos(input, cur_edit_pos);

	bool is_next_last_line = skb__is_at_last_line(input, next_edit_pos);

	bool cur_is_rtl = skb__is_rtl(input, cur_edit_pos, cur_affinity);
	bool next_is_rtl = skb__is_rtl(input, next_edit_pos, SKB_AFFINITY_TRAILING);

	// Do not add extra stop at the end of the line on intermediate lines.
	const bool stop_at_dir_change = input->params.caret_mode == SKB_CARET_MODE_SKRIBIDI &&  (is_next_last_line || skb__are_on_same_line(cur_edit_pos, next_edit_pos));

	uint8_t affinity = SKB_AFFINITY_TRAILING;
	bool check_eol = true;

	if (stop_at_dir_change && cur_is_rtl != next_is_rtl) {
		// Text direction change.
		if (cur_affinity == SKB_AFFINITY_LEADING || cur_affinity == SKB_AFFINITY_EOL) {
			// Switch over to the next character.
			affinity = SKB_AFFINITY_TRAILING;
			cur_edit_pos = next_edit_pos;
		} else {
			// On a trailing edge, and the direction will change in next character.
			// Move up to the leading edge before proceeding.
			affinity = SKB_AFFINITY_LEADING;
			check_eol = false;
		}
	} else {
		if (cur_affinity == SKB_AFFINITY_LEADING || cur_affinity == SKB_AFFINITY_EOL) {
			// If on leading edge, normalize the index to next trailing location.
			cur_is_rtl = next_is_rtl;
			cur_edit_pos = next_edit_pos;

			// Update next
			next_edit_pos = skb__get_next_grapheme_pos(input, cur_edit_pos);
			next_is_rtl = skb__is_rtl(input, next_edit_pos, SKB_AFFINITY_TRAILING);
		}

		if (stop_at_dir_change && cur_is_rtl != next_is_rtl) {
			// On a trailing edge, and the direction will change in next character.
			// Move up to the leading edge before proceeding.
			affinity = SKB_AFFINITY_LEADING;
			check_eol = false;
		} else {
			// Direction will stay the same, advance.
			affinity = SKB_AFFINITY_TRAILING;
			cur_edit_pos = next_edit_pos;
		}
	}

	if (check_eol) {
		if (skb__is_at_last_line(input, cur_edit_pos) && skb__is_past_end_of_line(input, cur_edit_pos)) {
			const skb__input_paragraph_t* paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];
			const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
			const skb_layout_line_t* line = &lines[cur_edit_pos.line_idx];
			affinity = SKB_AFFINITY_EOL;
			cur_edit_pos.paragraph_offset = line->last_grapheme_offset;
		}
	}

	const skb__input_paragraph_t* paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];
	return (skb_text_position_t) {
		.offset = paragraph->text_start_offset + cur_edit_pos.paragraph_offset,
		.affinity = affinity,
	};
}

static skb_text_position_t skb__advance_backward(const skb_input_t* input, skb__input_position_t cur_edit_pos, uint8_t cur_affinity)
{
	skb__input_position_t prev_edit_pos = skb__get_prev_grapheme_pos(input, cur_edit_pos);

	bool cur_is_rtl = skb__is_rtl(input, cur_edit_pos, cur_affinity);
	bool prev_is_rtl = skb__is_rtl(input, prev_edit_pos, SKB_AFFINITY_TRAILING);

	// Do not add extra stop at the end of the line on intermediate lines.
	const bool stop_at_dir_change = input->params.caret_mode == SKB_CARET_MODE_SKRIBIDI && skb__are_on_same_line(cur_edit_pos, prev_edit_pos);

	uint8_t affinity = SKB_AFFINITY_TRAILING;

	if (stop_at_dir_change && prev_is_rtl != cur_is_rtl) {
		if (cur_affinity == SKB_AFFINITY_EOL) {
			// At the end of line, but the direction is changing. Move to leading edge first.
			affinity = SKB_AFFINITY_LEADING;
		} else if (cur_affinity == SKB_AFFINITY_LEADING) {
			// On a leading edge, and the direction will change in next character. Move to trailing edge first.
			affinity = SKB_AFFINITY_TRAILING;
		} else {
			// On a trailing edge, and the direction will change in next character.
			// Switch over to the leading edge of the previous character.
			affinity = SKB_AFFINITY_LEADING;
			cur_edit_pos = prev_edit_pos;
		}
	} else {
		if (cur_affinity == SKB_AFFINITY_LEADING || (!skb__is_at_start_of_line(input, cur_edit_pos) && cur_affinity == SKB_AFFINITY_EOL)) {
			// On leading edge, normalize the index to next trailing location.
			// Special handling for empty lines to avoid extra stop.
			affinity = SKB_AFFINITY_TRAILING;
		} else {
			// On a trailing edge, advance to the next character.
			affinity = SKB_AFFINITY_TRAILING;
			cur_edit_pos = prev_edit_pos;
		}
	}

	const skb__input_paragraph_t* paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];

	return (skb_text_position_t) {
		.offset = paragraph->text_start_offset + cur_edit_pos.paragraph_offset,
		.affinity = affinity,
	};
}

skb_text_position_t skb_input_move_to_next_char(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];
	if (line->is_rtl)
		return skb__advance_backward(input, edit_pos, pos.affinity);
	return skb__advance_forward(input, edit_pos, pos.affinity);
}

skb_text_position_t skb_input_move_to_prev_char(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];
	if (line->is_rtl)
		return skb__advance_forward(input, edit_pos, pos.affinity);
	return skb__advance_backward(input, edit_pos, pos.affinity);
}

static skb_text_position_t skb__advance_word_forward(const skb_input_t* input, skb__input_position_t cur_edit_pos)
{
	const skb__input_paragraph_t* paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];

	int32_t offset = cur_edit_pos.paragraph_offset;
	const int32_t text_count = skb_layout_get_text_count(paragraph->layout);
	const skb_text_property_t* text_props = skb_layout_get_text_properties(paragraph->layout);

	while (offset < text_count) {
		if (text_props[offset].is_word_break) {
			int32_t next_offset = skb_layout_next_grapheme_offset(paragraph->layout, offset);
			if (!text_props[next_offset].is_whitespace) {
				offset = next_offset;
				break;
			}
		}
		offset++;
	}
	
	if (offset == text_count) {
		if (cur_edit_pos.paragraph_idx + 1 < input->paragraphs_count) {
			cur_edit_pos.paragraph_idx++;
			paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];
			offset = 0; // Beginning of layout
		} else {
			offset = skb_layout_align_grapheme_offset(paragraph->layout, text_count-1);
			return (skb_text_position_t) {
				.offset = paragraph->text_start_offset + offset,
				.affinity = SKB_AFFINITY_EOL,
			};
		}
	}

	return (skb_text_position_t) {
		.offset = paragraph->text_start_offset + offset,
		.affinity = SKB_AFFINITY_TRAILING,
	};
}

static skb_text_position_t skb__advance_word_backward(const skb_input_t* input, skb__input_position_t cur_edit_pos)
{
	const skb__input_paragraph_t* paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];

	int32_t offset = cur_edit_pos.paragraph_offset;

	const int32_t text_count = skb_layout_get_text_count(paragraph->layout);
	const skb_text_property_t* text_props = skb_layout_get_text_properties(paragraph->layout);

	if (offset == 0) {
		if (cur_edit_pos.paragraph_idx - 1 >= 0) {
			// Goto previous paragraph
			cur_edit_pos.paragraph_idx--;
			paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];
			offset = skb_layout_align_grapheme_offset(paragraph->layout, text_count-1); // Last grapheme of the paragraph. 
			return (skb_text_position_t) {
				.offset = paragraph->text_start_offset + offset,
				.affinity = SKB_AFFINITY_TRAILING,
			};
		}
		offset = 0;
		return (skb_text_position_t) {
			.offset = paragraph->text_start_offset + offset,
			.affinity = SKB_AFFINITY_SOL,
		};
	}

	offset = skb_layout_prev_grapheme_offset(paragraph->layout, offset);

	while (offset > 0) {
		if (text_props[offset-1].is_word_break) {
			int32_t next_offset = skb_layout_next_grapheme_offset(paragraph->layout, offset-1);
			if (!text_props[next_offset].is_whitespace) {
				offset = next_offset;
				break;
			}
		}
		offset--;
	}
	
	return (skb_text_position_t) {
		.offset = paragraph->text_start_offset + offset,
		.affinity = SKB_AFFINITY_TRAILING,
	};
}

skb_text_position_t skb_input_move_to_next_word(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];
	if (line->is_rtl)
		return skb__advance_word_backward(input, edit_pos);
	return skb__advance_word_forward(input, edit_pos);
}

skb_text_position_t skb_input_move_to_prev_word(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];
	if (line->is_rtl)
		return skb__advance_word_forward(input, edit_pos);
	return skb__advance_word_backward(input, edit_pos);
}

skb_text_position_t skb_input_move_to_next_line(const skb_input_t* input, skb_text_position_t pos, float preferred_x)
{
	skb__input_position_t cur_edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];

	if (skb__is_at_last_line(input, cur_edit_pos)) {
		// Goto end of the text
		return skb_input_get_line_end_at(input, pos);
	}

	const int32_t lines_count = skb_layout_get_lines_count(paragraph->layout);

	// Goto next line
	if (cur_edit_pos.line_idx + 1 >= lines_count) {
		// End of current paragraph, goto first line of next paragraph.
		assert(cur_edit_pos.paragraph_idx + 1 < input->paragraphs_count); // should have been handled by skb__is_at_last_line() above.
		cur_edit_pos.paragraph_idx++;
		paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];
		cur_edit_pos.line_idx = 0;
	} else {
		cur_edit_pos.line_idx++;
	}

	skb_text_position_t hit_pos = skb_layout_hit_test_at_line(paragraph->layout, SKB_MOVEMENT_CARET, cur_edit_pos.line_idx, preferred_x);
	hit_pos.offset += paragraph->text_start_offset;

	return hit_pos;
}

skb_text_position_t skb_input_move_to_prev_line(const skb_input_t* input, skb_text_position_t pos, float preferred_x)
{
	skb__input_position_t cur_edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];

	if (skb__is_at_first_line(input, cur_edit_pos)) {
		// Goto beginning of the text
		return skb_input_get_line_start_at(input, pos);
	}

	const int32_t lines_count = skb_layout_get_lines_count(paragraph->layout);

	// Goto prev line
	if (cur_edit_pos.line_idx - 1 < 0) {
		// Beginning of current paragraph, goto last line of prev paragraph.
		assert(cur_edit_pos.paragraph_idx - 1 >= 0); // should have been handled by skb__is_at_first_line() above.
		cur_edit_pos.paragraph_idx--;
		paragraph = &input->paragraphs[cur_edit_pos.paragraph_idx];
		cur_edit_pos.line_idx = lines_count - 1;
	} else {
		cur_edit_pos.line_idx--;
	}

	skb_text_position_t hit_pos = skb_layout_hit_test_at_line(paragraph->layout, SKB_MOVEMENT_CARET, cur_edit_pos.line_idx, preferred_x);
	hit_pos.offset += paragraph->text_start_offset;

	return hit_pos;
}

int32_t skb_input_get_selection_text_utf8_count(const skb_input_t* input, skb_text_selection_t selection)
{
	assert(input);

	skb__input_range_t sel_range = skb__get_sanitized_range(input, selection);

	if (sel_range.start.paragraph_idx == sel_range.end.paragraph_idx) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[sel_range.start.paragraph_idx];
		int32_t count = sel_range.end.text_offset - sel_range.start.text_offset;
		return skb_utf32_to_utf8_count(paragraph->text + sel_range.start.paragraph_offset, count);
	} else {
		int32_t count = 0;
		// First line
		const skb__input_paragraph_t* first_paragraph = &input->paragraphs[sel_range.start.paragraph_idx];
		const int32_t first_line_count = first_paragraph->text_count - first_paragraph->text_start_offset;
		count += skb_utf32_to_utf8_count(first_paragraph->text + sel_range.start.paragraph_offset, first_line_count);
		// Middle lines
		for (int32_t line_idx = sel_range.start.paragraph_idx + 1; line_idx < sel_range.end.paragraph_idx; line_idx++) {
			const skb__input_paragraph_t* paragraph = &input->paragraphs[line_idx];
			count += skb_utf32_to_utf8_count(paragraph->text, paragraph->text_count);
		}
		// Last line
		const skb__input_paragraph_t* last_paragraph = &input->paragraphs[sel_range.end.paragraph_idx];
		const int32_t last_line_count = skb_mini(last_paragraph->text_start_offset + 1, last_paragraph->text_count);
		count += skb_utf32_to_utf8_count(last_paragraph->text, last_line_count);

		return count;
	}
}

int32_t skb_input_get_selection_text_utf8(const skb_input_t* input, skb_text_selection_t selection, char* buf, int32_t buf_cap)
{
	assert(input);

	skb__input_range_t sel_range = skb__get_sanitized_range(input, selection);

	if (sel_range.start.paragraph_idx == sel_range.end.paragraph_idx) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[sel_range.start.paragraph_idx];
		int32_t count = sel_range.end.text_offset - sel_range.start.text_offset;
		return skb_utf32_to_utf8(paragraph->text + sel_range.start.paragraph_offset, count, buf, buf_cap);
	} else {
		int32_t count = 0;
		// First line
		const skb__input_paragraph_t* first_paragraph = &input->paragraphs[sel_range.start.paragraph_idx];
		const int32_t first_line_count = first_paragraph->text_count - first_paragraph->text_start_offset;
		count += skb_utf32_to_utf8(first_paragraph->text + sel_range.start.paragraph_offset, first_line_count, buf + count, buf_cap - count);
		// Middle lines
		for (int32_t line_idx = sel_range.start.paragraph_idx + 1; line_idx < sel_range.end.paragraph_idx; line_idx++) {
			const skb__input_paragraph_t* paragraph = &input->paragraphs[line_idx];
			count += skb_utf32_to_utf8(paragraph->text, paragraph->text_count, buf + count, buf_cap - count);
		}
		// Last line
		const skb__input_paragraph_t* last_paragraph = &input->paragraphs[sel_range.end.paragraph_idx];
		const int32_t last_line_count = skb_mini(last_paragraph->text_start_offset + 1, last_paragraph->text_count);
		count += skb_utf32_to_utf8(last_paragraph->text, last_line_count, buf + count, buf_cap - count);

		return count;
	}
}

static int32_t skb__copy_utf32(const uint32_t* src, int32_t count, uint32_t* dst, int32_t max_dst)
{
	const int32_t copy_count = skb_mini(max_dst, count);
	if (copy_count > 0)
		memcpy(dst, src, copy_count * sizeof(uint32_t));
	return count;
}

int32_t skb_input_get_selection_text_utf32_count(const skb_input_t* input, skb_text_selection_t selection)
{
	assert(input);

	skb__input_range_t sel_range = skb__get_sanitized_range(input, selection);

	if (sel_range.start.paragraph_idx == sel_range.end.paragraph_idx) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[sel_range.start.paragraph_idx];
		return sel_range.end.text_offset - sel_range.start.text_offset;
	} else {
		int32_t count = 0;
		// First line
		const skb__input_paragraph_t* first_paragraph = &input->paragraphs[sel_range.start.paragraph_idx];
		const int32_t first_line_count = first_paragraph->text_count - first_paragraph->text_start_offset;
		count += first_line_count;
		// Middle lines
		for (int32_t line_idx = sel_range.start.paragraph_idx + 1; line_idx <= sel_range.end.paragraph_idx - 1; line_idx++) {
			const skb__input_paragraph_t* paragraph = &input->paragraphs[line_idx];
			count += count;
		}
		// Last line
		const skb__input_paragraph_t* last_paragraph = &input->paragraphs[sel_range.end.paragraph_idx];
		const int32_t last_line_count = skb_mini(last_paragraph->text_start_offset + 1, last_paragraph->text_count);
		count += last_line_count;

		return count;
	}
}

int32_t skb_input_get_selection_text_utf32(const skb_input_t* input, skb_text_selection_t selection, uint32_t* buf, int32_t buf_cap)
{
	assert(input);

	skb__input_range_t sel_range = skb__get_sanitized_range(input, selection);

	if (sel_range.start.paragraph_idx == sel_range.end.paragraph_idx) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[sel_range.start.paragraph_idx];
		int32_t count = sel_range.end.text_offset - sel_range.start.text_offset;
		return skb__copy_utf32(paragraph->text + sel_range.start.paragraph_offset, count, buf, buf_cap);
	} else {
		int32_t count = 0;
		// First line
		const skb__input_paragraph_t* first_paragraph = &input->paragraphs[sel_range.start.paragraph_idx];
		const int32_t first_line_count = first_paragraph->text_count - first_paragraph->text_start_offset;
		count += skb__copy_utf32(first_paragraph->text + sel_range.start.paragraph_offset, first_line_count, buf + count, buf_cap - count);
		// Middle lines
		for (int32_t line_idx = sel_range.start.paragraph_idx + 1; line_idx <= sel_range.end.paragraph_idx - 1; line_idx++) {
			const skb__input_paragraph_t* paragraph = &input->paragraphs[line_idx];
			count += skb__copy_utf32(paragraph->text, paragraph->text_count, buf + count, buf_cap - count);
		}
		// Last line
		const skb__input_paragraph_t* last_paragraph = &input->paragraphs[sel_range.end.paragraph_idx];
		const int32_t last_line_count = skb_mini(last_paragraph->text_start_offset + 1, last_paragraph->text_count);
		count += skb__copy_utf32(last_paragraph->text, last_line_count, buf + count, buf_cap - count);

		return count;
	}
}

skb_text_selection_t skb_input_get_current_selection(skb_input_t* input)
{
	assert(input);
	return input->selection;	
}

void skb_input_select_all(skb_input_t* input)
{
	assert(input);

	if (input->paragraphs_count > 0) {
		input->selection.start_pos = (skb_text_position_t) { .offset = 0, .affinity = SKB_AFFINITY_SOL };	
		const skb__input_paragraph_t* last_paragraph = &input->paragraphs[input->paragraphs_count - 1];
		const int32_t last_text_count = skb_layout_get_text_count(last_paragraph->layout);
		const int32_t last_grapheme_offset = skb_layout_align_grapheme_offset(last_paragraph->layout, last_text_count-1);
		input->selection.end_pos = (skb_text_position_t) { .offset = last_paragraph->text_start_offset + last_grapheme_offset, .affinity = SKB_AFFINITY_EOL };
	} else {
		input->selection.start_pos = (skb_text_position_t) { 0 };	
		input->selection.end_pos = (skb_text_position_t) { 0 };
	}
}

void skb_input_select_none(skb_input_t* input)
{
	// Clear selection, but retain current caret position.
	input->selection.start_pos = input->selection.end_pos;
}

void skb_input_select(skb_input_t* input, skb_text_selection_t selection)
{
	input->selection = selection;
}

skb_text_position_t skb_input_hit_test(const skb_input_t* input, skb_movement_type_t type, float hit_x, float hit_y)
{
	assert(input);
	assert(input->paragraphs_count > 0);
	
	const skb__input_paragraph_t* hit_paragraph = NULL;
	int32_t hit_line_idx = SKB_INVALID_INDEX;

	const skb__input_paragraph_t* first_paragraph = &input->paragraphs[0];
	const skb__input_paragraph_t* last_paragraph = &input->paragraphs[input->paragraphs_count - 1];

	const skb_rect2_t first_paragraph_bounds = skb_layout_get_bounds(first_paragraph->layout);
	const skb_rect2_t last_paragraph_bounds = skb_layout_get_bounds(last_paragraph->layout);
	
	const float first_top_y = first_paragraph->y + first_paragraph_bounds.y;
	
	const float last_top_y = last_paragraph->y + last_paragraph_bounds.y;
	const float last_bot_y = last_paragraph->y + last_paragraph_bounds.y + last_paragraph_bounds.height;

	if (hit_y < first_top_y) {
		hit_paragraph = first_paragraph;
		hit_line_idx = 0;
	} else if (hit_y >= last_bot_y) {
		hit_paragraph = last_paragraph;
		hit_line_idx = skb_layout_get_lines_count(last_paragraph->layout) - 1;
	} else {
		for (int32_t i = 0; i < input->paragraphs_count; i++) {
			const skb__input_paragraph_t* paragraph = &input->paragraphs[i];
			const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
			const int32_t lines_count = skb_layout_get_lines_count(paragraph->layout);
			for (int32_t j = 0; j < lines_count; j++) {
				const skb_layout_line_t* line = &lines[j];
				const float bot_y = paragraph->y + line->bounds.y + -line->ascender + line->descender;
				if (hit_y < bot_y) {
					hit_line_idx = j;
					break;
				}
			}
			if (hit_line_idx != SKB_INVALID_INDEX) {
				hit_paragraph = paragraph;
				break;
			}
		}
		if (hit_line_idx == SKB_INVALID_INDEX) {
			hit_paragraph = last_paragraph;
			hit_line_idx = skb_layout_get_lines_count(last_paragraph->layout) - 1;
		}
	}

	skb_text_position_t pos = skb_layout_hit_test_at_line(hit_paragraph->layout, type, hit_line_idx, hit_x);
	pos.offset += hit_paragraph->text_start_offset;

	return pos;
}

enum {
	SKB_DRAG_NONE,
	SKB_DRAG_CHAR,
	SKB_DRAG_WORD,
	SKB_DRAG_LINE,
};

void skb_input_mouse_click(skb_input_t* input, float x, float y, uint32_t mods, double time)
{
	static const double double_click_duration = 0.4;
	
	if (input->paragraphs_count <= 0)
		return;

	const double dt = time - input->last_click_time;

	if (dt < double_click_duration)
		input->click_count++;
	else
		input->click_count = 1;
			
	if (input->click_count > 3)
		input->click_count = 1;
			
	input->last_click_time = time;

	skb_text_position_t hit_caret = skb_input_hit_test(input, SKB_MOVEMENT_CARET, x, y);

	if (mods & SKB_MOD_SHIFT) {
		// Shift click makes selection from current start pos to the new hit pos.
		input->selection.end_pos = hit_caret;
		input->drag_mode = SKB_DRAG_CHAR;
	} else {
		if (input->click_count == 1) {
			input->selection.end_pos = hit_caret;
			input->selection.start_pos = input->selection.end_pos;
			input->drag_mode = SKB_DRAG_CHAR;
		} else if (input->click_count == 2) {
			input->selection.start_pos = skb_input_get_word_start_at(input, hit_caret);
			input->selection.end_pos = skb_input_get_word_end_at(input, hit_caret);
			input->drag_mode = SKB_DRAG_WORD;
		} else if (input->click_count == 3) {
			input->selection.start_pos = skb_input_get_line_start_at(input, hit_caret);
			input->selection.end_pos = skb_input_get_line_end_at(input, hit_caret);
			input->drag_mode = SKB_DRAG_LINE;
		}
	}

	input->drag_initial_selection = input->selection;

	input->drag_start_x = x;
	input->drag_start_y = y;
	input->drag_moved = false;
}

void skb_input_mouse_drag(skb_input_t* input, float x, float y)
{
	static const float move_threshold = 5.f;
	
	if (!input->drag_moved) {
		float dx = input->drag_start_x - x;
		float dy = input->drag_start_y - y;
		float len_sqr = dx*dx + dy*dy;
		if (len_sqr > move_threshold*move_threshold)
			input->drag_moved = true;
	}
	
	if (input->drag_moved) {

		skb_text_position_t hit_pos = skb_input_hit_test(input, SKB_MOVEMENT_SELECTION, x, y);

		skb_text_position_t sel_start_pos = hit_pos;
		skb_text_position_t sel_end_pos = hit_pos;
				
		if (input->drag_mode == SKB_DRAG_CHAR) {
			sel_start_pos = hit_pos;
			sel_end_pos = hit_pos;
		} else if (input->drag_mode == SKB_DRAG_WORD) {
			sel_start_pos = skb_input_get_word_start_at(input, hit_pos);
			sel_end_pos = skb_input_get_word_end_at(input, hit_pos);
		} else if (input->drag_mode == SKB_DRAG_LINE) {
			sel_start_pos = skb_input_get_line_start_at(input, hit_pos);
			sel_end_pos = skb_input_get_line_end_at(input, hit_pos);
		}

		// Note: here the start/end positions are in order (not generally true).
		const skb__input_position_t sel_start = skb__get_sanitized_position(input, sel_start_pos, SKB_SANITIZE_ADJUST_AFFINITY);
		const skb__input_position_t sel_end = skb__get_sanitized_position(input, sel_end_pos, SKB_SANITIZE_ADJUST_AFFINITY);

		const skb__input_position_t initial_start = skb__get_sanitized_position(input, input->drag_initial_selection.start_pos, SKB_SANITIZE_ADJUST_AFFINITY);
		const skb__input_position_t initial_end = skb__get_sanitized_position(input, input->drag_initial_selection.end_pos, SKB_SANITIZE_ADJUST_AFFINITY);

		if (sel_start.text_offset < initial_start.text_offset) {
			// The selection got expanded before the initial selection range start.
			input->selection.start_pos = sel_start_pos;
			input->selection.end_pos = input->drag_initial_selection.end_pos;
		} else if (sel_end.text_offset > initial_end.text_offset) {
			// The selection got expanded past the initial selection range end.
			input->selection.start_pos = input->drag_initial_selection.start_pos;
			input->selection.end_pos = sel_end_pos;
		} else {
			// Restore
			input->selection.start_pos = input->drag_initial_selection.start_pos;
			input->selection.end_pos = input->drag_initial_selection.end_pos;
		}

		input->preferred_x = -1.f; // reset preferred.
	}
}

static void skb__set_line_combined_text(skb__input_paragraph_t* paragraph, const uint32_t* a, int32_t a_count, const uint32_t* b, int32_t b_count, const uint32_t* c, int32_t c_count)
{
	paragraph->text_count = a_count + b_count + c_count;
	if (paragraph->text_count > 0) {
		SKB_ARRAY_RESERVE(paragraph->text, paragraph->text_count);
		memcpy(paragraph->text, a, a_count * sizeof(uint32_t));
		memcpy(paragraph->text + a_count, b, b_count * sizeof(uint32_t));
		memcpy(paragraph->text + a_count + b_count, c, c_count * sizeof(uint32_t));
	}
}

static void skb__replace_range(skb_input_t* input, skb_temp_alloc_t* temp_alloc, skb__input_position_t start, skb__input_position_t end, const uint32_t* utf32, int32_t utf32_len)
{
	int32_t input_paragraph_count = 0;
	skb_range_t* input_paragraph_ranges = skb__split_text_into_paragraphs(temp_alloc, utf32, utf32_len, &input_paragraph_count);
	assert(input_paragraph_count > 0); // we assume that even for empty input there's one item.
	
	// Save start and end edit lines from being freed (these may be the same).
	uint32_t* start_paragraph_text = input->paragraphs[start.paragraph_idx].text;
	uint32_t* end_paragraph_text = input->paragraphs[end.paragraph_idx].text;
	const int32_t end_paragraph_text_count = input->paragraphs[end.paragraph_idx].text_count;
	
	input->paragraphs[start.paragraph_idx].text = NULL;
	input->paragraphs[start.paragraph_idx].text_count = 0;
	input->paragraphs[end.paragraph_idx].text = NULL;
	input->paragraphs[end.paragraph_idx].text_count = 0;

	// Free lines that we'll remove or rebuild.
	for (int32_t i = start.paragraph_idx; i <= end.paragraph_idx; i++) {
		skb__input_paragraph_t* paragraph = &input->paragraphs[i];
		skb_free(paragraph->text);
		skb_layout_destroy(paragraph->layout);
		memset(paragraph, 0, sizeof(skb__input_paragraph_t));
	}

	// Allocate new lines or prune.
	const int32_t selection_paragraph_count = (end.paragraph_idx + 1) - start.paragraph_idx;
	const int32_t new_paragraphs_count = skb_maxi(0, input->paragraphs_count - selection_paragraph_count + input_paragraph_count);
	const int32_t old_paragraphs_count = input->paragraphs_count;
	SKB_ARRAY_RESERVE(input->paragraphs, new_paragraphs_count);
	input->paragraphs_count = new_paragraphs_count;

	// Move tail of the text to create space for the lines to be inserted, accounting for the removed lines.
	const int32_t old_tail_idx = end.paragraph_idx + 1; // range_end is the last one to remove.
	const int32_t tail_count = old_paragraphs_count - old_tail_idx;
	const int32_t new_tail_idx = start.paragraph_idx + input_paragraph_count;
	if (new_tail_idx != old_tail_idx && tail_count > 0)
		memmove(input->paragraphs + new_tail_idx, input->paragraphs + old_tail_idx, tail_count * sizeof(skb__input_paragraph_t));

	// Create new lines.
	const int32_t first_new_paragraph_idx = start.paragraph_idx;
	int32_t paragraph_idx = first_new_paragraph_idx;

	const int32_t start_paragraph_copy_count = start.paragraph_offset;
	const int32_t end_paragraph_copy_offset = end.paragraph_offset;
	const int32_t end_paragraph_copy_count = skb_maxi(0, end_paragraph_text_count - end_paragraph_copy_offset);

	skb__input_paragraph_t* last_paragraph = NULL;
	int32_t last_paragraph_offset = 0;

	if (input_paragraph_count == 1) {
		skb__input_paragraph_t* new_paragraph = &input->paragraphs[paragraph_idx++];
		memset(new_paragraph, 0, sizeof(skb__input_paragraph_t));
		skb__set_line_combined_text(new_paragraph,
			start_paragraph_text, start_paragraph_copy_count,
			utf32, utf32_len,
			end_paragraph_text + end_paragraph_copy_offset, end_paragraph_copy_count);
		// Keep track of last paragraph and last codepoint inserted for caret positioning.
		last_paragraph = new_paragraph;
		last_paragraph_offset = start_paragraph_copy_count + utf32_len - 1;
	} else if (input_paragraph_count > 0) {
		// Start
		const skb_range_t start_paragraph_range = input_paragraph_ranges[0];
		skb__input_paragraph_t* new_start_paragraph = &input->paragraphs[paragraph_idx++];
		memset(new_start_paragraph, 0, sizeof(skb__input_paragraph_t));
		skb__set_line_combined_text(new_start_paragraph,
			start_paragraph_text, start_paragraph_copy_count,
			utf32 + start_paragraph_range.start, start_paragraph_range.end - start_paragraph_range.start,
			NULL, 0);

		// Middle
		for (int32_t i = 1; i < input_paragraph_count - 1; i++) {
			const skb_range_t paragraph_range = input_paragraph_ranges[i];
			skb__input_paragraph_t* new_paragraph = &input->paragraphs[paragraph_idx++];
			memset(new_paragraph, 0, sizeof(skb__input_paragraph_t));
			new_paragraph->text_count = paragraph_range.end - paragraph_range.start;
			if (new_paragraph->text_count > 0) {
				SKB_ARRAY_RESERVE(new_paragraph->text, new_paragraph->text_count);
				memcpy(new_paragraph->text, utf32 + paragraph_range.start, new_paragraph->text_count * sizeof(uint32_t));
			}
		}

		// End
		const skb_range_t end_paragraph_range = input_paragraph_ranges[input_paragraph_count - 1];
		skb__input_paragraph_t* new_end_paragraph = &input->paragraphs[paragraph_idx++];
		memset(new_end_paragraph, 0, sizeof(skb__input_paragraph_t));
		skb__set_line_combined_text(new_end_paragraph,
			utf32 + end_paragraph_range.start, end_paragraph_range.end - end_paragraph_range.start,
			end_paragraph_text + end_paragraph_copy_offset, end_paragraph_copy_count,
			NULL, 0);
		
		// Keep track of last paragraph and last codepoint inserted for caret positioning.
		last_paragraph = new_end_paragraph;
		last_paragraph_offset = end_paragraph_range.end - end_paragraph_range.start - 1;
	}


	// Update start offsets.
	int32_t start_offset = (first_new_paragraph_idx > 0) ? (input->paragraphs[first_new_paragraph_idx - 1].text_start_offset + input->paragraphs[first_new_paragraph_idx - 1].text_count) : 0;
	for (int32_t i = first_new_paragraph_idx; i < input->paragraphs_count; i++) {
		input->paragraphs[i].text_start_offset = start_offset;
		start_offset += input->paragraphs[i].text_count;
	}

	// Free old lines
	skb_free(start_paragraph_text);
	if (end_paragraph_text != start_paragraph_text)
		skb_free(end_paragraph_text);


	// Find offset of the last grapheme, this is needed to place the caret on the leading edge of the last grapheme.
	// We use leading edge of last grapheme so that the caret stays in context when typing at the direction change of a bidi text.
	if (last_paragraph->text_count > 0) {
		char* grapheme_breaks = SKB_TEMP_ALLOC(temp_alloc, char, last_paragraph->text_count);
		set_graphemebreaks_utf32(last_paragraph->text, last_paragraph->text_count, input->params.layout_params.lang, grapheme_breaks);
		
		// Find beginning of the last grapheme.
		while ((last_paragraph_offset - 1) >= 0 && grapheme_breaks[last_paragraph_offset - 1] != GRAPHEMEBREAK_BREAK)
			last_paragraph_offset--;
		
		SKB_TEMP_FREE(temp_alloc, grapheme_breaks);
	}
	
	// Set selection to the end of the inserted text.
	if (last_paragraph_offset < 0) {
		// This can happen when we delete the first character.
		input->selection.start_pos = (skb_text_position_t){
			.offset = last_paragraph->text_start_offset,
			.affinity = SKB_AFFINITY_TRAILING
		};
	} else {
		input->selection.start_pos = (skb_text_position_t){
			.offset = last_paragraph->text_start_offset + last_paragraph_offset,
			.affinity = SKB_AFFINITY_LEADING
		};
	}
	
	input->selection.end_pos = input->selection.start_pos;
	input->preferred_x = -1.f; // reset preferred.

	SKB_TEMP_FREE(temp_alloc, input_paragraph_ranges);
}

static void skb__replace_selection(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len)
{
	// Insert pos gets clamped to the layout text size.
	skb__input_range_t sel_range = skb__get_sanitized_range(input, input->selection);
	skb__replace_range(input, temp_alloc, sel_range.start, sel_range.end, utf32, utf32_len);
}

// Based on android.text.method.BaseKeyListener.getOffsetForBackspaceKey().
enum {
	BACKSPACE_STATE_START = 0,	// Initial state
	BACKSPACE_STATE_LF = 1,	// The offset is immediately before line feed.
	BACKSPACE_STATE_BEFORE_KEYCAP = 2, // The offset is immediately before a KEYCAP.
	BACKSPACE_STATE_BEFORE_VS_AND_KEYCAP = 3,	// The offset is immediately before a variation selector and a KEYCAP.
	BACKSPACE_STATE_BEFORE_EMOJI_MODIFIER = 4,	// The offset is immediately before an emoji modifier.
	BACKSPACE_STATE_BEFORE_VS_AND_EMOJI_MODIFIER = 5, // The offset is immediately before a variation selector and an emoji modifier.
	BACKSPACE_STATE_BEFORE_VS = 6, // The offset is immediately before a variation selector.
	BACKSPACE_STATE_BEFORE_EMOJI = 7, // The offset is immediately before an emoji.
	BACKSPACE_STATE_BEFORE_ZWJ = 8, // The offset is immediately before a ZWJ that were seen before a ZWJ emoji.
	BACKSPACE_STATE_BEFORE_VS_AND_ZWJ = 9, // The offset is immediately before a variation selector and a ZWJ that were seen before a ZWJ emoji.
	BACKSPACE_STATE_ODD_NUMBERED_RIS = 10, // The number of following RIS code points is odd.
	BACKSPACE_STATE_EVEN_NUMBERED_RIS = 11, // // The number of following RIS code points is even.
	BACKSPACE_STATE_IN_TAG_SEQUENCE = 12, // The offset is in emoji tag sequence.
	BACKSPACE_STATE_FINISHED = 13, // The state machine has been stopped.
};

static skb__input_position_t skb__get_backspace_start_offset(const skb_input_t* input, skb__input_position_t pos)
{
	assert(input);

	// If at beginning of line, go to the end of the previous line.
	if (pos.paragraph_offset == 0) {
		if (pos.paragraph_idx > 0) {
			pos.paragraph_idx--;
			const skb__input_paragraph_t* paragraph = &input->paragraphs[pos.paragraph_idx];
			pos.paragraph_offset = paragraph->text_count;
			pos.text_offset = paragraph->text_start_offset + pos.paragraph_offset;
		}
	}

	if (pos.paragraph_offset <= 0)
		return pos;

	const skb__input_paragraph_t* paragraph = &input->paragraphs[pos.paragraph_idx];
	int32_t offset = pos.paragraph_offset;

	int32_t delete_char_count = 0;  // Char count to be deleted by backspace.
	int32_t last_seen_var_sel_char_count = 0;  // Char count of previous variation selector.
	int32_t state = BACKSPACE_STATE_START;
	int32_t cur_offset = offset;

	do {
		const uint32_t cp = paragraph->text[cur_offset - 1];
		cur_offset--;
		switch (state) {
		case BACKSPACE_STATE_START:
			delete_char_count = 1;
			if (cp == SKB_CHAR_LINE_FEED)
				state = BACKSPACE_STATE_LF;
			else if (skb_is_variation_selector(cp))
				state = BACKSPACE_STATE_BEFORE_VS;
			else if (skb_is_regional_indicator_symbol(cp))
				state = BACKSPACE_STATE_ODD_NUMBERED_RIS;
			else if (skb_is_emoji_modifier(cp))
				state = BACKSPACE_STATE_BEFORE_EMOJI_MODIFIER;
			else if (cp == SKB_CHAR_COMBINING_ENCLOSING_KEYCAP)
				state = BACKSPACE_STATE_BEFORE_KEYCAP;
			else if (skb_is_emoji(cp))
				state = BACKSPACE_STATE_BEFORE_EMOJI;
			else if (cp == SKB_CHAR_CANCEL_TAG)
				state = BACKSPACE_STATE_IN_TAG_SEQUENCE;
			else
				state = BACKSPACE_STATE_FINISHED;
			break;
		case BACKSPACE_STATE_LF:
			if (cp == SKB_CHAR_CARRIAGE_RETURN)
				delete_char_count++;
			state = BACKSPACE_STATE_FINISHED;
			break;
		case BACKSPACE_STATE_ODD_NUMBERED_RIS:
			if (skb_is_regional_indicator_symbol(cp)) {
				delete_char_count += 2; /* Char count of RIS */
				state = BACKSPACE_STATE_EVEN_NUMBERED_RIS;
			} else {
				state = BACKSPACE_STATE_FINISHED;
			}
			break;
		case BACKSPACE_STATE_EVEN_NUMBERED_RIS:
			if (skb_is_regional_indicator_symbol(cp)) {
				delete_char_count -= 2; /* Char count of RIS */
				state = BACKSPACE_STATE_ODD_NUMBERED_RIS;
			} else {
				state = BACKSPACE_STATE_FINISHED;
			}
			break;
		case BACKSPACE_STATE_BEFORE_KEYCAP:
			if (skb_is_variation_selector(cp)) {
				last_seen_var_sel_char_count = 1;
				state = BACKSPACE_STATE_BEFORE_VS_AND_KEYCAP;
				break;
			}
			if (skb_is_keycap_base(cp))
				delete_char_count++;
			state = BACKSPACE_STATE_FINISHED;
			break;
		case BACKSPACE_STATE_BEFORE_VS_AND_KEYCAP:
			if (skb_is_keycap_base(cp))
				delete_char_count += last_seen_var_sel_char_count + 1;
			state = BACKSPACE_STATE_FINISHED;
			break;
		case BACKSPACE_STATE_BEFORE_EMOJI_MODIFIER:
			if (skb_is_variation_selector(cp)) {
				last_seen_var_sel_char_count = 1;
				state = BACKSPACE_STATE_BEFORE_VS_AND_EMOJI_MODIFIER;
				break;
			}
			if (skb_is_emoji_modifier_base(cp)) {
				delete_char_count++;
				state = BACKSPACE_STATE_BEFORE_EMOJI;
				break;
			}
			state = BACKSPACE_STATE_FINISHED;
			break;
		case BACKSPACE_STATE_BEFORE_VS_AND_EMOJI_MODIFIER:
			if (skb_is_emoji_modifier_base(cp))
				delete_char_count += last_seen_var_sel_char_count + 1;
			state = BACKSPACE_STATE_FINISHED;
			break;
		case BACKSPACE_STATE_BEFORE_VS:
			if (skb_is_emoji(cp)) {
				delete_char_count++;
				state = BACKSPACE_STATE_BEFORE_EMOJI;
				break;
			}
			if (!skb_is_variation_selector(cp) && hb_unicode_combining_class(hb_unicode_funcs_get_default(), cp) == HB_UNICODE_COMBINING_CLASS_NOT_REORDERED)
				delete_char_count++;
			state = BACKSPACE_STATE_FINISHED;
			break;
		case BACKSPACE_STATE_BEFORE_EMOJI:
			if (cp == SKB_CHAR_ZERO_WIDTH_JOINER)
				state = BACKSPACE_STATE_BEFORE_ZWJ;
			else
				state = BACKSPACE_STATE_FINISHED;
			break;
		case BACKSPACE_STATE_BEFORE_ZWJ:
			if (skb_is_emoji(cp)) {
				delete_char_count += 1 + 1;  // +1 for ZWJ.
				state = skb_is_emoji_modifier(cp) ? BACKSPACE_STATE_BEFORE_EMOJI_MODIFIER : BACKSPACE_STATE_BEFORE_EMOJI;
			} else if (skb_is_variation_selector(cp)) {
				last_seen_var_sel_char_count = 1;
				state = BACKSPACE_STATE_BEFORE_VS_AND_ZWJ;
			} else {
				state = BACKSPACE_STATE_FINISHED;
			}
			break;
		case BACKSPACE_STATE_BEFORE_VS_AND_ZWJ:
			if (skb_is_emoji(cp)) {
				delete_char_count += last_seen_var_sel_char_count + 1 + 1; // +1 for ZWJ.
				last_seen_var_sel_char_count = 0;
				state = BACKSPACE_STATE_BEFORE_EMOJI;
			} else {
				state = BACKSPACE_STATE_FINISHED;
			}
			break;
		case BACKSPACE_STATE_IN_TAG_SEQUENCE:
			if (skb_is_tag_spec_char(cp)) {
				delete_char_count++;
				// Keep the same state.
			} else if (skb_is_emoji(cp)) {
				delete_char_count++;
				state = BACKSPACE_STATE_FINISHED;
			} else {
				// Couldn't find tag_base character. Delete the last tag_term character.
				delete_char_count = 2;  // for U+E007F
				state = BACKSPACE_STATE_FINISHED;
			}
			break;
		default:
			assert(0);
			state = BACKSPACE_STATE_FINISHED;
			break;
		}
	} while (cur_offset > 0 && state != BACKSPACE_STATE_FINISHED);
	
	pos.paragraph_offset -= delete_char_count;
	pos.text_offset = paragraph->text_start_offset + pos.paragraph_offset;

	return pos;
}

void skb_input_key_pressed(skb_input_t* input, skb_temp_alloc_t* temp_alloc, skb_input_key_t key, uint32_t mods)
{
	if (key == SKB_KEY_RIGHT) {
		if (mods & SKB_MOD_SHIFT) {
			if (mods & SKB_MOD_CONTROL)
				input->selection.end_pos = skb_input_move_to_next_word(input, input->selection.end_pos);
			else
				input->selection.end_pos = skb_input_move_to_next_char(input, input->selection.end_pos);
			// Do not move g_selection_start_caret, to allow the selection to grow.
		} else {
			if (mods & SKB_MOD_CONTROL) {
				input->selection.end_pos = skb_input_move_to_next_word(input, input->selection.end_pos);
			} else {
				// Reset selection, choose left-most caret position.
				if (skb_input_get_selection_count(input, input->selection) > 0)
					input->selection.end_pos = skb_input_get_selection_ordered_end(input, input->selection);
				else
					input->selection.end_pos = skb_input_move_to_next_char(input, input->selection.end_pos);
			}
			input->selection.start_pos = input->selection.end_pos;
		}
		input->preferred_x = -1.f; // reset preferred.
	}
	
	if (key == SKB_KEY_LEFT) {
		if (mods & SKB_MOD_SHIFT) {
			if (mods & SKB_MOD_CONTROL)
				input->selection.end_pos = skb_input_move_to_prev_word(input, input->selection.end_pos);
			else
				input->selection.end_pos = skb_input_move_to_prev_char(input, input->selection.end_pos);
			// Do not move g_selection_start_caret, to allow the selection to grow.
		} else {
			// Reset selection, choose right-most caret position.
			if (mods & SKB_MOD_CONTROL) {
				input->selection.end_pos = skb_input_move_to_prev_word(input, input->selection.end_pos);
			} else {
				if (skb_input_get_selection_count(input, input->selection) > 0)
					input->selection.end_pos = skb_input_get_selection_ordered_start(input, input->selection);
				else
					input->selection.end_pos = skb_input_move_to_prev_char(input, input->selection.end_pos);
			}
			input->selection.start_pos = input->selection.end_pos;
		}
		input->preferred_x = -1.f; // reset preferred.
	}

	if (key == SKB_KEY_HOME) {
		input->selection.end_pos = skb_input_get_line_start_at(input, input->selection.end_pos);
		if ((mods & SKB_MOD_SHIFT) == 0) {
			input->selection.start_pos = input->selection.end_pos;
		}
		input->preferred_x = -1.f; // reset preferred.
	}

	if (key == SKB_KEY_END) {
		input->selection.end_pos = skb_input_get_line_end_at(input, input->selection.end_pos);
		if ((mods & SKB_MOD_SHIFT) == 0) {
			input->selection.start_pos = input->selection.end_pos;
		}
		input->preferred_x = -1.f; // reset preferred.
	}

	if (key == SKB_KEY_UP) {
		if (input->preferred_x < 0.f) {
			skb_visual_caret_t vis = skb_input_get_visual_caret(input, input->selection.end_pos);
			input->preferred_x = vis.x;
		}

		input->selection.end_pos = skb_input_move_to_prev_line(input, input->selection.end_pos, input->preferred_x);
		
		if ((mods & SKB_MOD_SHIFT) == 0) {
			input->selection.start_pos = input->selection.end_pos;
		}
	}
	if (key == SKB_KEY_DOWN) {
		if (input->preferred_x < 0.f) {
			skb_visual_caret_t vis = skb_input_get_visual_caret(input, input->selection.end_pos);
			input->preferred_x = vis.x;
		}
		
		input->selection.end_pos = skb_input_move_to_next_line(input, input->selection.end_pos, input->preferred_x);
		
		if ((mods & SKB_MOD_SHIFT) == 0) {
			input->selection.start_pos = input->selection.end_pos;
		}
	}
	
	if (key == SKB_KEY_BACKSPACE) {
		if (skb_input_get_selection_count(input, input->selection) > 0) {
			skb__replace_selection(input, temp_alloc, NULL, 0);
			skb__update_layout(input, temp_alloc);
			skb__emit_on_change(input);
		} else {
			skb__input_position_t range_end = skb__get_sanitized_position(input, input->selection.end_pos, SKB_SANITIZE_ADJUST_AFFINITY);
			skb__input_position_t range_start = skb__get_backspace_start_offset(input, range_end);
			skb__replace_range(input, temp_alloc, range_start, range_end, NULL, 0);
			skb__update_layout(input, temp_alloc);
			skb__emit_on_change(input);
		}
	}
	
	if (key == SKB_KEY_DELETE) {
		if (skb_input_get_selection_count(input, input->selection) > 0) {
			skb__replace_selection(input, temp_alloc, NULL, 0);
			skb__update_layout(input, temp_alloc);
			skb__emit_on_change(input);
		} else {
			skb__input_position_t range_start = skb__get_sanitized_position(input, input->selection.end_pos, SKB_SANITIZE_ADJUST_AFFINITY);
			skb__input_position_t range_end = skb__get_next_grapheme_pos(input, range_start);
			skb__replace_range(input, temp_alloc, range_start, range_end, NULL, 0);
			skb__update_layout(input, temp_alloc);
			skb__emit_on_change(input);
		}
	}
	
	if (key == SKB_KEY_ENTER) {
		const uint32_t cp = SKB_CHAR_LINE_FEED;
		skb__replace_selection(input, temp_alloc, &cp, 1);
		skb__update_layout(input, temp_alloc);
		// The call to skb_input_replace_selection() changes selection to after the inserted text.
		// The caret is placed on the leading edge, which is usually good, but for new line we want trailing.
		skb__input_position_t range_start = skb__get_sanitized_position(input, input->selection.end_pos, SKB_SANITIZE_ADJUST_AFFINITY);
		input->selection.end_pos = (skb_text_position_t) {
			.offset = range_start.text_offset,
			.affinity = SKB_AFFINITY_TRAILING,
		};
		input->selection.start_pos = input->selection.end_pos;
		skb__emit_on_change(input);
	}
}

void skb_input_insert_codepoint(skb_input_t* input, skb_temp_alloc_t* temp_alloc, uint32_t codepoint)
{
	skb__replace_selection(input, temp_alloc, &codepoint, 1);
	skb__update_layout(input, temp_alloc);
	skb__emit_on_change(input);
}

void skb_input_paste_utf8(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const char* utf8, int32_t utf8_len)
{
	if (utf8_len < 0) utf8_len = (int32_t)strlen(utf8);
	const int32_t utf32_len = skb_utf8_to_utf32(utf8, utf8_len, NULL, 0);

	uint32_t* utf32 = SKB_TEMP_ALLOC(temp_alloc, uint32_t, utf32_len);
	
	skb_utf8_to_utf32(utf8, utf8_len, utf32, utf32_len);
	
	skb__replace_selection(input, temp_alloc, utf32, utf32_len);

	SKB_TEMP_FREE(temp_alloc, utf32);

	skb__update_layout(input, temp_alloc);
	skb__emit_on_change(input);
}

void skb_input_paste_utf32(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len)
{
	if (utf32_len < 0) utf32_len = skb_utf32_strlen(utf32);
	skb__replace_selection(input, temp_alloc, utf32, utf32_len);
	skb__update_layout(input, temp_alloc);
	skb__emit_on_change(input);
}

void skb_input_cut(skb_input_t* input, skb_temp_alloc_t* temp_alloc)
{
	skb__replace_selection(input, temp_alloc, NULL, 0);
	skb__update_layout(input, temp_alloc);
	skb__emit_on_change(input);
}

int32_t skb_input_get_line_index_at(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_ADJUST_AFFINITY);

	int32_t total_line_count = 0;
	// Lines up to the text position.
	for (int32_t i = 0; i < edit_pos.paragraph_idx; i++) {
		skb__input_paragraph_t* paragraph = &input->paragraphs[i];
		total_line_count += skb_layout_get_lines_count(paragraph->layout);
	}
	total_line_count += edit_pos.line_idx;

	return total_line_count;
}

int32_t skb_input_get_column_index_at(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_ADJUST_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	const skb_layout_line_t* lines = skb_layout_get_lines(paragraph->layout);
	const skb_layout_line_t* line = &lines[edit_pos.line_idx];
	return edit_pos.paragraph_offset - line->text_range.start;
}

int32_t skb_input_get_text_offset_at(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_ADJUST_AFFINITY);
	return edit_pos.text_offset;
}

bool skb_input_is_character_rtl_at(const skb_input_t* input, skb_text_position_t pos)
{
	skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	skb_text_position_t layout_pos = {
		.offset = edit_pos.paragraph_offset,
		.affinity = SKB_AFFINITY_TRAILING,
	};
	return skb_layout_is_character_rtl_at(paragraph->layout, layout_pos);
}

skb_visual_caret_t skb_input_get_visual_caret(const skb_input_t* input, skb_text_position_t pos)
{
	const skb__input_position_t edit_pos = skb__get_sanitized_position(input, pos, SKB_SANITIZE_IGNORE_AFFINITY);
	const skb__input_paragraph_t* paragraph = &input->paragraphs[edit_pos.paragraph_idx];
	pos.offset -= paragraph->text_start_offset;
	skb_visual_caret_t caret = skb_layout_get_visual_caret_at_line(paragraph->layout, edit_pos.line_idx, pos);
	caret.y += paragraph->y;
	return caret;
}

skb_range_t skb_input_get_selection_text_offset_range(const skb_input_t* input, skb_text_selection_t selection)
{
	const skb__input_range_t range = skb__get_sanitized_range(input, selection);
	return (skb_range_t) {
		.start = range.start.text_offset,
		.end = range.end.text_offset,
	};
}

int32_t skb_input_get_selection_count(const skb_input_t* input, skb_text_selection_t selection)
{
	const skb__input_range_t range = skb__get_sanitized_range(input, selection);
	return range.end.text_offset - range.start.text_offset;
}

void skb_input_get_selection_bounds(const skb_input_t* input, skb_text_selection_t selection, skb_selection_rect_callback* callback, void* context)
{
	const skb__input_range_t range = skb__get_sanitized_range(input, selection);

	if (range.start.paragraph_idx == range.end.paragraph_idx) {
		const skb__input_paragraph_t* paragraph = &input->paragraphs[range.start.paragraph_idx];
		skb_text_selection_t line_sel = {
			.start_pos = { .offset = range.start.paragraph_offset },
			.end_pos = { .offset = range.end.paragraph_offset },
		};
		skb_layout_get_selection_bounds_with_offset(paragraph->layout, paragraph->y, line_sel, callback, context);
	} else {
		// First line
		const skb__input_paragraph_t* first_paragraph = &input->paragraphs[range.start.paragraph_idx];
		skb_text_selection_t first_line_sel = {
			.start_pos = { .offset = range.start.paragraph_offset },
			.end_pos = { .offset = first_paragraph->text_count },
		};
		skb_layout_get_selection_bounds_with_offset(first_paragraph->layout, first_paragraph->y, first_line_sel, callback, context);

		// Middle lines
		for (int32_t line_idx = range.start.paragraph_idx + 1; line_idx < range.end.paragraph_idx; line_idx++) {
			const skb__input_paragraph_t* paragraph = &input->paragraphs[line_idx];
			skb_text_selection_t line_sel = {
				.start_pos = { .offset = 0 },
				.end_pos = { .offset = first_paragraph->text_count },
			};
			skb_layout_get_selection_bounds_with_offset(paragraph->layout, paragraph->y, line_sel, callback, context);
		}
		
		// Last line
		const skb__input_paragraph_t* last_paragraph = &input->paragraphs[range.end.paragraph_idx];
		skb_text_selection_t last_line_sel = {
			.start_pos = { .offset = 0 },
			.end_pos = { .offset = range.end.paragraph_offset },
		};
		skb_layout_get_selection_bounds_with_offset(last_paragraph->layout, last_paragraph->y, last_line_sel, callback, context);
	}
}
