// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_input.h"

static int test0(void)
{
	skb_input_params_t params = {
		 .layout_params = {
		 	.font_collection = NULL,
		 },
		. text_attribs = {
		 	.font_size = 15.f,
		 	.font_weight = 400
		},
		.base_direction = SKB_DIR_LTR,
		.caret_mode = SKB_CARET_MODE_SKRIBIDI,
	};

	skb_input_t* input = skb_input_create(&params);
	ENSURE(input != NULL);

	skb_input_destroy(input);
	
	return 0;
}

int input_tests(void)
{
	RUN_SUBTEST(test0);
	return 0;	
}
