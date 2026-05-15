// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_render_cache.h"
#include "skb_common.h"

static int test_init(void)
{
	skb_render_cache_t* render_cache = skb_render_cache_create(NULL);
	ENSURE(render_cache != NULL);

	skb_render_cache_destroy(render_cache);

	return 0;
}

static int test_ceilf_rounding(void)
{
	// Integer font sizes must remain unchanged
	ENSURE_SMALL(skb_ceilf(48.0f) - 48.0f, 1e-6f);
	ENSURE_SMALL(skb_ceilf(33.0f) - 33.0f, 1e-6f);
	ENSURE_SMALL(skb_ceilf(100.0f) - 100.0f, 1e-6f);
	ENSURE_SMALL(skb_ceilf(256.0f) - 256.0f, 1e-6f);
	ENSURE_SMALL(skb_ceilf(1.0f) - 1.0f, 1e-6f);

	// Non-integer font sizes must round up
	ENSURE_SMALL(skb_ceilf(48.42f) - 49.0f, 1e-6f);
	ENSURE_SMALL(skb_ceilf(33.001f) - 34.0f, 1e-6f);
	ENSURE_SMALL(skb_ceilf(0.5f) - 1.0f, 1e-6f);

	// Very small values
	ENSURE_SMALL(skb_ceilf(0.001f) - 1.0f, 1e-6f);

	// Negative values (ceil rounds toward positive infinity)
	ENSURE_SMALL(skb_ceilf(-1.0f) - (-1.0f), 1e-6f);
	ENSURE_SMALL(skb_ceilf(-1.5f) - (-1.0f), 1e-6f);
	ENSURE_SMALL(skb_ceilf(-10.75f) - (-10.0f), 1e-6f);

	return 0;
}

int render_cache_tests(void)
{
	RUN_SUBTEST(test_init);
	RUN_SUBTEST(test_ceilf_rounding);
	return 0;
}
