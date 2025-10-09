// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdbool.h>

#include "skb_common.h"
#include "skb_rich_layout.h"
#include "skb_rich_layout_internal.h"
#include "skb_rich_text.h"
#include "skb_layout.h"
#include <string.h>


skb_paragraph_position_t skb_rich_layout_get_paragraph_position(const skb_rich_layout_t* rich_layout, skb_text_position_t text_pos, skb_affinity_usage_t affinity_usage)
{
	assert(rich_layout);

	skb_paragraph_position_t result = {0};

	// TODO: use lower bound

	// Find paragraph.
	const int32_t last_paragraph_idx = rich_layout->paragraphs_count - 1;
	const int32_t total_text_count = rich_layout->paragraphs[last_paragraph_idx].global_text_offset + skb_layout_get_text_count(&rich_layout->paragraphs[last_paragraph_idx].layout);
	if (text_pos.offset < 0) {
		result.paragraph_idx = 0;
	} else if (text_pos.offset >= total_text_count) {
		result.paragraph_idx = last_paragraph_idx;
	} else {
		for (int32_t i = 0; i < rich_layout->paragraphs_count; i++) {
			const int32_t end_text_offset = rich_layout->paragraphs[i].global_text_offset + skb_layout_get_text_count(&rich_layout->paragraphs[i].layout);
			if (text_pos.offset < end_text_offset) {
				result.paragraph_idx = i;
				break;
			}
		}
	}

	// Adjust text position withing the paragraph.
	result.text_offset = text_pos.offset - rich_layout->paragraphs[result.paragraph_idx].global_text_offset;
	// Align to nearest grapheme.
	result.text_offset = skb_layout_align_grapheme_offset(&rich_layout->paragraphs[result.paragraph_idx].layout, result.text_offset);

	// Adjust position based on affinity
	if (affinity_usage == SKB_AFFINITY_USE) {
		if (text_pos.affinity == SKB_AFFINITY_LEADING || text_pos.affinity == SKB_AFFINITY_EOL) {
			result.text_offset = skb_layout_next_grapheme_offset(&rich_layout->paragraphs[result.paragraph_idx].layout, result.text_offset);
			// Affinity adjustment may push the offset to next paragraph
			if (result.text_offset >= skb_layout_get_text_count(&rich_layout->paragraphs[result.paragraph_idx].layout)) {
				if ((result.paragraph_idx + 1) < rich_layout->paragraphs_count) {
					result.text_offset = 0;
					result.paragraph_idx++;
				}
			}
		}
	}

	result.global_text_offset = rich_layout->paragraphs[result.paragraph_idx].global_text_offset + result.text_offset;

	return result;
}

int32_t skb_rich_layout_text_position_to_offset(const skb_rich_layout_t* rich_layout, skb_text_position_t text_pos)
{
	skb_paragraph_position_t pos = skb_rich_layout_get_paragraph_position(rich_layout, text_pos, SKB_AFFINITY_USE);
	return pos.global_text_offset;
}

skb_range_t skb_rich_layout_text_selection_to_range(const skb_rich_layout_t* rich_layout, skb_text_selection_t selection)
{
	skb_paragraph_position_t start_pos = skb_rich_layout_get_paragraph_position(rich_layout, selection.start_pos, SKB_AFFINITY_USE);
	skb_paragraph_position_t end_pos = skb_rich_layout_get_paragraph_position(rich_layout, selection.end_pos, SKB_AFFINITY_USE);
	if (start_pos.global_text_offset > start_pos.global_text_offset)
		return (skb_range_t) { .start = end_pos.global_text_offset, .end = start_pos.global_text_offset };
	return (skb_range_t) { .start = start_pos.global_text_offset, .end = end_pos.global_text_offset };
}


static void skb__layout_paragraph_init(skb_layout_paragraph_t* layout_paragraph)
{
	memset(layout_paragraph, 0, sizeof(skb_layout_paragraph_t));
	layout_paragraph->layout = skb_layout_make_empty();
}

static void skb__layout_paragraph_clear(skb_layout_paragraph_t* layout_paragraph)
{
	skb_layout_destroy(&layout_paragraph->layout);
	memset(layout_paragraph, 0, sizeof(skb_layout_paragraph_t));
}

skb_rich_layout_t skb_rich_layout_make_empty(void)
{
	return (skb_rich_layout_t) {
		.should_free_instance = false,
	};
}

skb_rich_layout_t* skb_rich_layout_create(void)
{
	skb_rich_layout_t* layout = skb_malloc(sizeof(skb_rich_layout_t));
	memset(layout, 0, sizeof(skb_rich_layout_t));
	layout->should_free_instance = true;
	return layout;
}

void skb_rich_layout_destroy(skb_rich_layout_t* layout)
{
	if (!layout) return;
	for (int32_t i = 0; i < layout->paragraphs_count; i++)
		skb__layout_paragraph_clear(&layout->paragraphs[i]);
	skb_free(layout->paragraphs);
	memset(layout, 0, sizeof(skb_rich_layout_t));
	if (layout->should_free_instance)
		skb_free(layout);
}

void skb_rich_layout_reset(skb_rich_layout_t* layout)
{
	if (!layout) return;
	for (int32_t i = 0; i < layout->paragraphs_count; i++)
		skb__layout_paragraph_clear(&layout->paragraphs[i]);
	layout->paragraphs_count = 0;
}

int32_t skb_rich_layout_get_paragraphs_count(const skb_rich_layout_t* layout)
{
	assert(layout);
	return layout->paragraphs_count;
}

const skb_layout_paragraph_t* skb_rich_layout_get_paragraph(const skb_rich_layout_t* layout, int32_t index)
{
	assert(layout);
	assert(index >= 0 && index < layout->paragraphs_count);
	return &layout->paragraphs[index];
}

const skb_layout_t* skb_rich_layout_get_layout(const skb_rich_layout_t* layout, int32_t index)
{
	assert(layout);
	assert(index >= 0 && index < layout->paragraphs_count);
	return &layout->paragraphs[index].layout;
}

float skb_rich_layout_get_offset_y(const skb_rich_layout_t* layout, int32_t index)
{
	assert(layout);
	assert(index >= 0 && index < layout->paragraphs_count);
	return layout->paragraphs[index].offset_y;
}

skb_text_direction_t skb_rich_layout_get_direction(const skb_rich_layout_t* layout, int32_t index)
{
	assert(layout);
	assert(index >= 0 && index < layout->paragraphs_count);
	return layout->paragraphs[index].direction;
}

void skb_rich_layout_update(
	skb_rich_layout_t* rich_layout, skb_temp_alloc_t* temp_alloc,
	const skb_layout_params_t* params, const skb_rich_text_t* rich_text,
	int32_t ime_text_offset, skb_text_t* ime_text)
{
	assert(skb_rich_text_get_paragraphs_count(rich_text) == rich_layout->paragraphs_count);

	skb_layout_params_t layout_params = *params;
	layout_params.flags |= SKB_LAYOUT_PARAMS_IGNORE_MUST_LINE_BREAKS;

	skb_text_direction_t direction = SKB_DIRECTION_AUTO;
	skb_attribute_t dir_override_attribute = {0};

	if (!ime_text || skb_text_get_utf32_count(ime_text) == 0)
		ime_text_offset = SKB_INVALID_INDEX;

	float y = 0.f;

	for (int32_t i = 0; i < skb_rich_text_get_paragraphs_count(rich_text); i++) {
		skb_layout_paragraph_t* layout_paragraph = &rich_layout->paragraphs[i];

		skb_attribute_set_t paragraph_attributes = skb_rich_text_get_paragraph_attributes(rich_text, i);
		paragraph_attributes.parent_set = &params->layout_attributes;
		if (i > 0) {
			// Copy the paragraph direction from the first paragraph to all later paragraphs.
			dir_override_attribute = skb_attribute_make_text_direction(direction);
			layout_params.layout_attributes = (skb_attribute_set_t) {
				.attributes = &dir_override_attribute,
				.attributes_count = 1,
				.parent_set = &paragraph_attributes,
			};
		} else {
			layout_params.layout_attributes = paragraph_attributes;
		}

		const skb_text_t* paragraph_text = skb_rich_text_get_paragraph_text(rich_text, i);
		const int32_t paragraph_text_count = skb_text_get_utf32_count(paragraph_text);
		const uint32_t paragraph_id = skb_rich_text_get_paragraph_version(rich_text, i);
		const int32_t global_text_offset = skb_rich_text_get_paragraph_text_offset(rich_text, i);
		const int32_t local_ime_text_offset = ime_text_offset - global_text_offset;

		layout_paragraph->global_text_offset = global_text_offset;

		if (local_ime_text_offset >= 0 && local_ime_text_offset < paragraph_text_count) {
			skb_temp_alloc_mark_t mark = skb_temp_alloc_save(temp_alloc);

			// Combine IME text with the line.
			skb_text_t* combined_text = skb_text_create_temp(temp_alloc);

			// Before
			skb_text_append_range(combined_text, paragraph_text, (skb_range_t){ .start = 0, .end = local_ime_text_offset });

			// Composition
			skb_text_append(combined_text, ime_text);

			// After
			skb_text_append_range(combined_text, paragraph_text, (skb_range_t){ .start = local_ime_text_offset, .end = paragraph_text_count });

			skb_layout_set_from_text(&layout_paragraph->layout, temp_alloc, &layout_params, combined_text, (skb_attribute_set_t){0});

			skb_text_destroy(combined_text);

			skb_temp_alloc_restore(temp_alloc, mark);

			// Reset ID so that when the IME state changes the paragraph will update.
			layout_paragraph->version = 0;
		} else {
			bool rebuild = false;

			// If the paragraph direction has changed, relayout.
			if (layout_paragraph->direction != direction)
				rebuild = true;

			// If the contents has changed, rebuild.
			if (layout_paragraph->version != paragraph_id)
				rebuild = true;

			if (rebuild) {
				skb_layout_set_from_text(&layout_paragraph->layout, temp_alloc, &layout_params, paragraph_text, (skb_attribute_set_t){0});
				layout_paragraph->direction = (uint8_t)direction;
				layout_paragraph->version = paragraph_id;
			}
		}

		// Take the resolved direction from the first paragraph, and apply to the rest. This matches the behavior of a single layout.
		if (i == 0)
			direction = skb_layout_get_resolved_direction(&layout_paragraph->layout);

		layout_paragraph->offset_y = y;

		skb_rect2_t layout_bounds = skb_layout_get_bounds(&layout_paragraph->layout);
		y += layout_bounds.height;
	}
}

void skb_rich_layout_update_with_change(
	skb_rich_layout_t* rich_layout, skb_temp_alloc_t* temp_alloc,
	const skb_layout_params_t* params, const skb_rich_text_t* text, skb_rich_text_change_t change,
	int32_t ime_text_offset, skb_text_t* ime_text)
{
	// Allocate new lines or prune.
	const int32_t new_paragraphs_count = skb_maxi(0, rich_layout->paragraphs_count - change.removed_paragraph_count + change.inserted_paragraph_count);
	const int32_t old_paragraphs_count = rich_layout->paragraphs_count;
	SKB_ARRAY_RESERVE(rich_layout->paragraphs, new_paragraphs_count);
	rich_layout->paragraphs_count = new_paragraphs_count;

	// Free the paragraphs that will be removed
	for (int32_t i = change.start_paragraph_idx + change.inserted_paragraph_count; i < change.start_paragraph_idx + change.removed_paragraph_count; i++)
		skb__layout_paragraph_clear(&rich_layout->paragraphs[i]);

	// Move tail of the paragraphs to create space for the paragraphs to be inserted, accounting for the removed paragraphs.
	const int32_t old_tail_idx = change.start_paragraph_idx + change.removed_paragraph_count; // range_end is the last one to remove.
	const int32_t tail_count = old_paragraphs_count - old_tail_idx;
	const int32_t new_tail_idx = change.start_paragraph_idx + change.inserted_paragraph_count;
	if (new_tail_idx != old_tail_idx && tail_count > 0)
		memmove(rich_layout->paragraphs + new_tail_idx, rich_layout->paragraphs + old_tail_idx, tail_count * sizeof(skb_layout_paragraph_t));

	// Init the new paragraphs that were created
	for (int32_t i = change.start_paragraph_idx + change.removed_paragraph_count; i < change.start_paragraph_idx + change.inserted_paragraph_count; i++)
		skb__layout_paragraph_init(&rich_layout->paragraphs[i]);

	// call update
	skb_rich_layout_update(rich_layout, temp_alloc, params, text, ime_text_offset, ime_text);
}

skb_visual_caret_t skb_rich_layout_get_visual_caret(const skb_rich_layout_t* rich_layout, skb_text_position_t pos)
{
	assert(rich_layout);
	if (rich_layout->paragraphs_count == 0)
		return (skb_visual_caret_t) {0};

	skb_paragraph_position_t paragraph_pos = skb_rich_layout_get_paragraph_position(rich_layout, pos, SKB_AFFINITY_IGNORE);
	const skb_layout_paragraph_t* paragraph = &rich_layout->paragraphs[paragraph_pos.paragraph_idx];

	pos.offset = paragraph_pos.text_offset;

	skb_visual_caret_t caret = skb_layout_get_visual_caret_at(&paragraph->layout, pos);
	caret.y += paragraph->offset_y;

	return caret;
}

void skb_rich_layout_get_selection_bounds(const skb_rich_layout_t* rich_layout, skb_text_selection_t selection, skb_selection_rect_func_t* callback, void* context)
{
	assert(rich_layout);

	skb_paragraph_position_t start_pos = skb_rich_layout_get_paragraph_position(rich_layout, selection.start_pos, SKB_AFFINITY_USE);
	skb_paragraph_position_t end_pos = skb_rich_layout_get_paragraph_position(rich_layout, selection.end_pos, SKB_AFFINITY_USE);
	if (start_pos.global_text_offset > end_pos.global_text_offset) {
		skb_paragraph_position_t tmp = start_pos;
		start_pos = end_pos;
		end_pos = tmp;
	}

	if (start_pos.paragraph_idx == end_pos.paragraph_idx) {
		const skb_layout_paragraph_t* paragraph = &rich_layout->paragraphs[start_pos.paragraph_idx];
		skb_text_selection_t line_sel = {
			.start_pos = { .offset = start_pos.text_offset },
			.end_pos = { .offset = end_pos.text_offset },
		};
		skb_layout_get_selection_bounds_with_offset(&paragraph->layout,paragraph->offset_y, line_sel, callback, context);
		return;
	}

	// First paragraph
	const skb_layout_paragraph_t* first_paragraph = &rich_layout->paragraphs[start_pos.paragraph_idx];
	skb_text_selection_t first_paragraph_sel = {
		.start_pos = { .offset = start_pos.text_offset },
		.end_pos = { .offset = skb_layout_get_text_count(&first_paragraph->layout) },
	};
	skb_layout_get_selection_bounds_with_offset(&first_paragraph->layout, first_paragraph->offset_y, first_paragraph_sel, callback, context);

	// Middle paragraphs
	for (int32_t i = start_pos.paragraph_idx + 1; i < end_pos.paragraph_idx; i++) {
		const skb_layout_paragraph_t* paragraph = &rich_layout->paragraphs[i];
		skb_text_selection_t line_sel = {
			.start_pos = { .offset = 0 },
			.end_pos = { .offset = skb_layout_get_text_count(&paragraph->layout) },
		};
		skb_layout_get_selection_bounds_with_offset(&paragraph->layout, paragraph->offset_y, line_sel, callback, context);
	}

	// Last paragraph
	const skb_layout_paragraph_t* last_paragraph = &rich_layout->paragraphs[end_pos.paragraph_idx];
	skb_text_selection_t last_paragraph_sel = {
		.start_pos = { .offset = 0 },
		.end_pos = { .offset = end_pos.text_offset },
	};
	skb_layout_get_selection_bounds_with_offset(&last_paragraph->layout, last_paragraph->offset_y, last_paragraph_sel, callback, context);
}

skb_text_position_t skb_rich_layout_hit_test(const skb_rich_layout_t* rich_layout, skb_movement_type_t type, float hit_x, float hit_y)
{
	assert(rich_layout);
	if (rich_layout->paragraphs_count == 0)
		return (skb_text_position_t){0};

	int32_t hit_paragraph_idx = SKB_INVALID_INDEX;
	int32_t hit_line_idx = SKB_INVALID_INDEX;

	const int32_t last_paragraph_idx = rich_layout->paragraphs_count - 1;

	const skb_rect2_t first_paragraph_bounds = skb_layout_get_bounds(&rich_layout->paragraphs[0].layout);
	const skb_rect2_t last_paragraph_bounds = skb_layout_get_bounds(&rich_layout->paragraphs[last_paragraph_idx].layout);

	const float first_top_y = rich_layout->paragraphs[0].offset_y + first_paragraph_bounds.y;
	const float last_bot_y = rich_layout->paragraphs[last_paragraph_idx].offset_y + last_paragraph_bounds.y + last_paragraph_bounds.height;

	if (hit_y < first_top_y) {
		hit_paragraph_idx = 0;
		hit_line_idx = 0;
	} else if (hit_y >= last_bot_y) {
		hit_paragraph_idx = last_paragraph_idx;
		hit_line_idx = skb_layout_get_lines_count(&rich_layout->paragraphs[last_paragraph_idx].layout) - 1;
	} else {
		for (int32_t i = 0; i < rich_layout->paragraphs_count; i++) {
			const skb_layout_paragraph_t* paragraph = &rich_layout->paragraphs[i];
			const skb_layout_line_t* lines = skb_layout_get_lines(&paragraph->layout);
			const int32_t lines_count = skb_layout_get_lines_count(&paragraph->layout);
			for (int32_t j = 0; j < lines_count; j++) {
				const skb_layout_line_t* line = &lines[j];
				const float bot_y = paragraph->offset_y + line->bounds.y + -line->ascender + line->descender;
				if (hit_y < bot_y) {
					hit_line_idx = j;
					break;
				}
			}
			if (hit_line_idx != SKB_INVALID_INDEX) {
				hit_paragraph_idx = i;
				break;
			}
		}
		if (hit_line_idx == SKB_INVALID_INDEX) {
			hit_paragraph_idx = last_paragraph_idx;
			hit_line_idx = skb_layout_get_lines_count(&rich_layout->paragraphs[last_paragraph_idx].layout) - 1;
		}
	}

	assert(hit_paragraph_idx != SKB_INVALID_INDEX);

	const skb_layout_paragraph_t* hit_paragraph = &rich_layout->paragraphs[hit_paragraph_idx];
	skb_text_position_t pos = skb_layout_hit_test_at_line(&hit_paragraph->layout, type, hit_line_idx, hit_x);
	pos.offset += hit_paragraph->global_text_offset;

	return pos;
}
