// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_render_cache.h"

static int test_init(void)
{
	skb_render_cache_t* render_cache = skb_render_cache_create(NULL);
	ENSURE(render_cache != NULL);

	skb_render_cache_destroy(render_cache);

	return 0;
}

int render_cache_tests(void)
{
	RUN_SUBTEST(test_init);
	return 0;
}
