// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_RICH_LAYOUT_H
#define SKB_RICH_LAYOUT_H

#include <stdint.h>
#include "skb_common.h"
#include "skb_layout.h"
#include "skb_text.h"
#include "skb_rich_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup rich_layout Rich Layout
 *
 * Rich layout defines layout for multiple paragraphs of text. Each paragraph has separate layout, which can be updated incrementally from Rich text.
 *
 * @{
 */


typedef struct skb_rich_layout_t skb_rich_layout_t;

skb_rich_layout_t* skb_rich_layout_create(void);
void skb_rich_layout_destroy(skb_rich_layout_t* rich_layout);
void skb_rich_layout_reset(skb_rich_layout_t* rich_layout);

int32_t skb_rich_layout_get_paragraphs_count(const skb_rich_layout_t* rich_layout);
const skb_layout_t* skb_rich_layout_get_layout(const skb_rich_layout_t* rich_layout, int32_t index);
float skb_rich_layout_get_layout_offset_y(const skb_rich_layout_t* rich_layout, int32_t index);
float skb_rich_layout_get_layout_advance_y(const skb_rich_layout_t* rich_layout, int32_t index);
skb_text_direction_t skb_rich_layout_get_direction(const skb_rich_layout_t* rich_layout, int32_t index);
const skb_layout_params_t* skb_rich_layout_get_params(const skb_rich_layout_t* rich_layout);

skb_rect2_t skb_rich_layout_get_bounds(const skb_rich_layout_t* rich_layout);

void skb_rich_layout_set_from_rich_text(
	skb_rich_layout_t* rich_layout, skb_temp_alloc_t* temp_alloc,
	const skb_layout_params_t* params, const skb_rich_text_t* rich_text,
	int32_t ime_text_offset, skb_text_t* ime_text);

void skb_rich_layout_apply_change(skb_rich_layout_t* rich_layout, skb_rich_text_change_t change);

typedef enum {
	SKB_AFFINITY_USE = 0,
	SKB_AFFINITY_IGNORE = 1,
} skb_affinity_usage_t;

skb_paragraph_position_t skb_rich_layout_text_position_to_paragraph_position(const skb_rich_layout_t* rich_layout, skb_text_position_t text_pos, skb_affinity_usage_t affinity_usage);
int32_t skb_rich_layout_text_position_to_offset(const skb_rich_layout_t* rich_layout, skb_text_position_t text_pos);
skb_range_t skb_rich_layout_text_selection_to_range(const skb_rich_layout_t* rich_layout, skb_text_selection_t selection);

skb_visual_caret_t skb_rich_layout_get_visual_caret(const skb_rich_layout_t* rich_layout, skb_text_position_t pos);
void skb_rich_layout_get_selection_bounds(const skb_rich_layout_t* rich_layout, skb_text_selection_t selection, skb_selection_rect_func_t* callback, void* context);
skb_text_position_t skb_rich_layout_hit_test(const skb_rich_layout_t* rich_layout, skb_movement_type_t type, float hit_x, float hit_y);


/** @} */

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // SKB_RICH_LAYOUT_H
