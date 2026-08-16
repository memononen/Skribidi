// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_layout.h"
#include "skb_font_collection.h"

static int test_init(void)
{
	skb_layout_params_t layout_params = {
		.font_collection = NULL,
	};

	skb_layout_t* layout = skb_layout_create(&layout_params);
	ENSURE(layout != NULL);

	skb_layout_destroy(layout);

	return 0;
}

static int test_missing_script(void)
{
	skb_temp_alloc_t* temp_alloc = skb_temp_alloc_create(1024);
	ENSURE(temp_alloc != NULL);

	skb_font_collection_t* font_collection = skb_font_collection_create();
	skb_font_handle_t font_handle = skb_font_collection_add_font(font_collection, "data/IBMPlexSans-Regular.ttf", SKB_FONT_FAMILY_DEFAULT, NULL);
	ENSURE(font_handle);

	skb_layout_params_t layout_params = {
		.font_collection = font_collection,
	};
	skb_attribute_t attributes[] = {
		skb_attribute_make_font_size(15.f),
	};

	// The loaded font should not support the script of the text. We should still get a valid layout, but with invalid glyphs.
	skb_layout_t* layout = skb_layout_create_utf8(temp_alloc, &layout_params, "今天天气晴朗", -1, SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(attributes));
	ENSURE(layout != NULL);
	ENSURE(skb_layout_get_glyphs_count(layout) > 0);
	const skb_glyph_t* glyphs = skb_layout_get_glyphs(layout);
	ENSURE(glyphs[0].gid == 0);

	skb_layout_destroy(layout);
	skb_font_collection_destroy(font_collection);
	skb_temp_alloc_destroy(temp_alloc);

	return 0;
}

static int test_caret_pos(void)
{
	skb_temp_alloc_t* temp_alloc = skb_temp_alloc_create(1024);
	ENSURE(temp_alloc != NULL);

	skb_font_collection_t* font_collection = skb_font_collection_create();
	skb_font_handle_t font_handle = skb_font_collection_add_font(font_collection, "data/IBMPlexSans-Regular.ttf", SKB_FONT_FAMILY_DEFAULT, NULL);
	ENSURE(font_handle);

	{
		skb_attribute_t attributes[] = {
			skb_attribute_make_font_size(15.f),
			skb_attribute_make_text_overflow(SKB_OVERFLOW_ELLIPSIS),
		};

		skb_layout_params_t layout_params = {
			.font_collection = font_collection,
			.layout_width = 0.f,
			.layout_height = 0.f,
			.layout_attributes = SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(attributes),
		};

		// No lines will be created, because the text is clipped to 0 x 0 rect.
		skb_layout_t* layout = skb_layout_create_utf8(temp_alloc, &layout_params, "moikka\nmoi", -1, (skb_attribute_set_t){0});
		ENSURE(layout != NULL);

		const int32_t line_idx = skb_layout_get_line_index(layout, (skb_text_position_t){ .offset = 6 });
		ENSURE(line_idx == 0);

		skb_layout_destroy(layout);
	}

	{
		skb_attribute_t attributes[] = {
			skb_attribute_make_font_size(15.f),
			skb_attribute_make_text_overflow(SKB_OVERFLOW_ELLIPSIS),
		};

		skb_layout_params_t layout_params = {
			.font_collection = font_collection,
			.layout_width = 45.f,
			.layout_height = 100.f,
			.layout_attributes = SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(attributes),
		};

		// No lines will be created, because the text is clipped to 0 x 0 rect.
		skb_layout_t* layout = skb_layout_create_utf8(temp_alloc, &layout_params, "moikka\nmoikka\nmoi", -1, (skb_attribute_set_t){0});
		ENSURE(layout != NULL);

		// Should get line 1 even if it is truncated at end.
		const int32_t line_idx = skb_layout_get_line_index(layout, (skb_text_position_t){ .offset = 13 });
		ENSURE(line_idx == 1);

		// Should get cared position on text that is truncated.
		skb_caret_info_t caret_info = skb_layout_get_caret_info_at(layout, (skb_text_position_t){ .offset = 13 });
		ENSURE(caret_info.x > 0.f);

		// Should get cared position on text that is truncated.
		skb_caret_info_t caret_info2 = skb_layout_get_caret_info_at(layout, (skb_text_position_t){ .offset = 100 });
		ENSURE(caret_info2.x > 0.f);

		// Should get cared position on text that is truncated.
		skb_caret_info_t caret_info3 = skb_layout_get_caret_info_at(layout, (skb_text_position_t){ .offset = -100 });
		ENSURE(caret_info3.x == 0.f);

		skb_layout_destroy(layout);
	}


	skb_font_collection_destroy(font_collection);
	skb_temp_alloc_destroy(temp_alloc);

	return 0;
}

int layout_tests(void)
{
	RUN_SUBTEST(test_init);
	RUN_SUBTEST(test_missing_script);
	RUN_SUBTEST(test_caret_pos);
	return 0;
}
