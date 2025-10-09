// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "skb_rich_text.h"
#include "test_macros.h"

static int test_rich_text_create(void)
{
	skb_rich_text_t* rich_text = skb_rich_text_create();

	ENSURE(rich_text);

	ENSURE(skb_rich_text_get_paragraphs_count(rich_text) == 0);

	skb_rich_text_destroy(rich_text);

	return 0;
}

static int test_rich_text_replace(void)
{
	skb_temp_alloc_t* temp_alloc = skb_temp_alloc_create(1024);
	ENSURE(temp_alloc != NULL);

	int32_t text_count = 0;

	skb_rich_text_t* rich_text = skb_rich_text_create();

	ENSURE(rich_text);

	ENSURE(skb_rich_text_get_paragraphs_count(rich_text) == 0);

	skb_rich_text_t* ins_rich_text = skb_rich_text_create();
	skb_rich_text_append_utf8(ins_rich_text, temp_alloc, "Foo\nbar", -1, (skb_attribute_set_t){0});
	skb_rich_text_append_utf8(ins_rich_text, temp_alloc, "baz", -1, (skb_attribute_set_t){0});
	text_count = skb_rich_text_get_utf32_count(ins_rich_text);
	ENSURE(text_count == 10);
	ENSURE(skb_rich_text_get_paragraphs_count(ins_rich_text) == 2); // Foo\n | bar

	// Insert front
	skb_rich_text_replace(rich_text, (skb_range_t){0}, ins_rich_text);
	text_count = skb_rich_text_get_utf32_count(rich_text);
	ENSURE(text_count == 10);
	ENSURE(skb_rich_text_get_paragraphs_count(rich_text) == 2); // Foo\n | barbaz

	// Insert back
	skb_rich_text_replace(rich_text, (skb_range_t){.start = text_count,.end = text_count}, ins_rich_text);
	text_count = skb_rich_text_get_utf32_count(rich_text);
	ENSURE(text_count == 20);
	ENSURE(skb_rich_text_get_paragraphs_count(rich_text) == 3); // Foo\n | barbazFoo\n | barbaz

	// Insert middle
	skb_rich_text_replace(rich_text, (skb_range_t){.start = 3,.end = 14}, ins_rich_text);
	text_count = skb_rich_text_get_utf32_count(rich_text);
	ENSURE(text_count == 19);
	ENSURE(skb_rich_text_get_paragraphs_count(rich_text) == 2); // FooFoo\n | barbazbarbaz

	skb_rich_text_destroy(rich_text);
	skb_rich_text_destroy(ins_rich_text);

	skb_temp_alloc_destroy(temp_alloc);

	return 0;
}

static int test_rich_text_append(void)
{
	skb_temp_alloc_t* temp_alloc = skb_temp_alloc_create(1024);
	ENSURE(temp_alloc != NULL);

	skb_rich_text_t* rich_text = skb_rich_text_create();
	skb_rich_text_append_utf8(rich_text, temp_alloc, "123456", -1, (skb_attribute_set_t){0});

	skb_rich_text_t* rich_text2 = skb_rich_text_create();
	skb_rich_text_append_range(rich_text2, rich_text, (skb_range_t){ .start = 2, .end = 5 });
	ENSURE(skb_rich_text_get_utf32_count(rich_text2) == 3);

	skb_rich_text_t* rich_text3 = skb_rich_text_create();
	skb_rich_text_append_utf8(rich_text3, temp_alloc, "123\n456\n789", -1, (skb_attribute_set_t){0});

	skb_rich_text_t* rich_text4 = skb_rich_text_create();
	skb_rich_text_append_utf8(rich_text4, temp_alloc, "abc", -1, (skb_attribute_set_t){0});
	skb_rich_text_append_range(rich_text4, rich_text3, (skb_range_t){ .start = 4, .end = 10 });
	ENSURE(skb_rich_text_get_utf32_count(rich_text4) == 9);
	ENSURE(skb_rich_text_get_paragraphs_count(rich_text4) == 2); // abc456\n | 78

	skb_rich_text_destroy(rich_text);
	skb_rich_text_destroy(rich_text2);
	skb_rich_text_destroy(rich_text3);
	skb_rich_text_destroy(rich_text4);

	skb_temp_alloc_destroy(temp_alloc);

	return 0;
}

int rich_text_tests(void)
{
	RUN_SUBTEST(test_rich_text_create);
	RUN_SUBTEST(test_rich_text_replace);
	RUN_SUBTEST(test_rich_text_append);
	return 0;
}
