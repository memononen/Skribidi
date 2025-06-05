// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_font_collection.h"

static int test_init(void)
{
	skb_font_collection_t* font_collection = skb_font_collection_create();
	ENSURE(font_collection != NULL);

	skb_font_collection_destroy(font_collection);
	
	return 0;
}

int font_collection_tests(void)
{
	RUN_SUBTEST(test_init);
	return 0;	
}
