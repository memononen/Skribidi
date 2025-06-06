// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_render.h"

static int test_init(void)
{
	skb_renderer_t* renderer = skb_renderer_create(NULL);
	ENSURE(renderer != NULL);

	skb_renderer_destroy(renderer);

	return 0;
}

int render_tests(void)
{
	RUN_SUBTEST(test_init);
	return 0;
}
