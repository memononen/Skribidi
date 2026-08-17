// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

// This test is currently set up just to test the that the headers compile when included from C++.
// The test can be elaborated if C++ related issues arise.
#include "test_macros.h"
#include "skribidi/skb_canvas.h"
#include "skribidi/skb_common.h"
#include "skribidi/skb_editor.h"
#include "skribidi/skb_font_collection.h"
#include "skribidi/skb_icon_collection.h"
#include "skribidi/skb_layout.h"
#include "skribidi/skb_layout_cache.h"
#include "skribidi/skb_rasterizer.h"
#include "skribidi/skb_image_atlas.h"

extern "C" int cpp_tests(void)
{
	return 0;
}
