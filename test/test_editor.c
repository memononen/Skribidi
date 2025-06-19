// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_editor.h"

static int test_init(void)
{
	skb_attribute_t attributes[] = {
		skb_attribute_make_font(SKB_FONT_FAMILY_DEFAULT, 15.f, SKB_WEIGHT_NORMAL, SKB_STYLE_NORMAL, SKB_STRETCH_NORMAL),
	};

	skb_editor_params_t params = {
		 .layout_params = {
		 	.font_collection = NULL,
		 },
		.text_attributes = attributes,
		.text_attributes_count = SKB_COUNTOF(attributes),
		.base_direction = SKB_DIRECTION_LTR,
		.caret_mode = SKB_CARET_MODE_SKRIBIDI,
	};

	skb_editor_t* editor = skb_editor_create(&params);
	ENSURE(editor != NULL);

	skb_editor_destroy(editor);

	return 0;
}

int editor_tests(void)
{
	RUN_SUBTEST(test_init);
	return 0;
}
