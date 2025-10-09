// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_RICH_LAYOUT_INTERNAL_H
#define SKB_RICH_LAYOUT_INTERNAL_H

#include <stdint.h>

#include "skb_common.h"
#include "skb_layout_internal.h"

typedef struct skb_layout_paragraph_t {
	skb_layout_t layout;				// Layout for the paragraph, may contain multiple lines.
	skb_text_direction_t direction;		// The reading direction the paragraph layout was done with.
	int32_t global_text_offset;
	float offset_y;						// Y offset of the layout.
	uint32_t version;					// Version of the paragraph, if different from rich text paragraph, needs update.
} skb_layout_paragraph_t;

typedef struct skb_rich_layout_t {
	skb_layout_paragraph_t* paragraphs;
	int32_t paragraphs_count;
	int32_t paragraphs_cap;
	uint8_t should_free_instance;
} skb_rich_layout_t;

skb_rich_layout_t skb_rich_layout_make_empty(void);

#endif // SKB_RICH_LAYOUT_INTERNAL_H
