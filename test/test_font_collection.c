// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_font_collection.h"
#include "skb_layout.h"

static int test_init(void)
{
	skb_font_collection_t* font_collection = skb_font_collection_create();
	ENSURE(font_collection != NULL);

	skb_font_collection_destroy(font_collection);

	return 0;
}

static int test_add_remove(void)
{
	skb_font_collection_t* font_collection = skb_font_collection_create();
	ENSURE(font_collection != NULL);

	skb_font_handle_t font_handle = skb_font_collection_add_font(font_collection, "data/IBMPlexSans-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	ENSURE(font_handle);

	uint8_t script = skb_script_from_iso15924_tag(SKB_TAG_STR("Latn"));

	skb_font_handle_t font_handle2 = 0;
	int32_t count = skb_font_collection_match_fonts(font_collection, "", script, SKB_FONT_FAMILY_DEFAULT, SKB_FONT_STYLE_NORMAL, SKB_FONT_STRETCH_NORMAL, 400, &font_handle2, 1);
	ENSURE(count == 1);
	ENSURE(font_handle2);

	bool removed = skb_font_collection_remove_font(font_collection, font_handle);
	ENSURE(removed);

	// Handle should be invalid now
	skb_font_t* font_ptr = skb_font_collection_get_font(font_collection, font_handle);
	ENSURE(font_ptr == NULL);

	// Should not find a font
	skb_font_handle_t font_handle3 = 0;
	int32_t count2 = skb_font_collection_match_fonts(font_collection, "", script, SKB_FONT_FAMILY_DEFAULT, SKB_FONT_STYLE_NORMAL, SKB_FONT_STRETCH_NORMAL, 400, &font_handle3, 1);
	ENSURE(count2 == 0);
	ENSURE(font_handle3 == 0);


	skb_font_collection_destroy(font_collection);

	return 0;
}

int font_collection_tests(void)
{
	RUN_SUBTEST(test_init);
	RUN_SUBTEST(test_add_remove);
	return 0;
}
