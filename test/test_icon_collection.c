// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "test_macros.h"
#include "skb_icon_collection.h"
#include "skb_layout.h"

static int test_init(void)
{
	skb_icon_collection_t* icon_collection = skb_icon_collection_create();
	ENSURE(icon_collection != NULL);

	skb_icon_collection_destroy(icon_collection);

	return 0;
}

int icon_collection_tests(void)
{
	RUN_SUBTEST(test_init);
	return 0;
}
