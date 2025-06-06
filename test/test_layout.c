// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_layout.h"

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

int layout_tests(void)
{
	RUN_SUBTEST(test_init);
	return 0;
}
