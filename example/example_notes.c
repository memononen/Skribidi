// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "debug_render.h"
#include "ime.h"
#include "render.h"
#include "utils.h"

#include "skb_common.h"
#include "skb_font_collection.h"
#include "skb_attribute_collection.h"
#include "skb_layout.h"
#include "skb_rasterizer.h"
#include "skb_image_atlas.h"
#include "skb_editor.h"
#include "skb_rich_text.h"

typedef struct ui_context_t {
	render_context_t* rc;
	skb_vec2_t mouse_pos;
	bool mouse_pressed;
	bool mouse_released;
	int32_t id_gen;
	int32_t next_hover;
	int32_t hover;
	int32_t active;
	int32_t went_active;
} ui_context_t;

typedef struct notes_context_t {
	example_t base;

	skb_font_collection_t* font_collection;
	skb_attribute_collection_t* attribute_collection;

	skb_temp_alloc_t* temp_alloc;
	GLFWwindow* window;
	render_context_t* rc;

	skb_editor_t* editor;

	skb_rich_text_t* rich_text_clipboard;
	uint64_t rich_text_clipboard_hash;

	bool allow_char;
	view_t view;
	bool drag_view;
	bool drag_text;

	skb_vec2_t mouse_pos;
	bool mouse_pressed;

	bool show_caret_details;
	bool show_run_details;

	ui_context_t ui;

	GLFWcursor* hand_cursor;

} notes_context_t;



static void update_ime_rect(notes_context_t* ctx)
{
	skb_caret_info_t caret_info = skb_editor_get_caret_info_at(ctx->editor, SKB_CURRENT_SELECTION_END);
	skb_vec2_t view_offset = skb_editor_get_view_offset(ctx->editor);
	caret_info.x += view_offset.x;
	caret_info.y += view_offset.y;

	skb_rect2_t caret_rect = {
		.x = caret_info.x - caret_info.descender * caret_info.slope,
		.y = caret_info.y + caret_info.ascender,
		.width = (-caret_info.ascender + caret_info.descender) * caret_info.slope,
		.height = -caret_info.ascender + caret_info.descender,
	};

	skb_rect2i_t input_rect = {
		.x = (int32_t)(ctx->view.cx + caret_rect.x * ctx->view.scale),
		.y = (int32_t)(ctx->view.cy + caret_rect.y * ctx->view.scale),
		.width = (int32_t)(caret_rect.width * ctx->view.scale),
		.height = (int32_t)(caret_rect.height * ctx->view.scale),
	};
	ime_set_input_rect(input_rect);
}

static void ime_handler(ime_event_t event, const uint32_t* text, int32_t text_length, int32_t cursor, void* context)
{
	notes_context_t* ctx = context;

	if (event == IME_EVENT_COMPOSITION)
		skb_editor_set_composition_utf32(ctx->editor, ctx->temp_alloc, text, text_length, cursor);
	else if (event == IME_EVENT_COMMIT)
		skb_editor_commit_composition_utf32(ctx->editor, ctx->temp_alloc, text, text_length);
	else if (event == IME_EVENT_CANCEL)
		skb_editor_clear_composition(ctx->editor, ctx->temp_alloc);

	update_ime_rect(ctx);
}

void notes_destroy(void* ctx_ptr);
void notes_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods);
void notes_on_char(void* ctx_ptr, unsigned int codepoint);
void notes_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods);
void notes_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y);
void notes_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods);
void notes_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height);

void* notes_create(GLFWwindow* window, render_context_t* rc)
{
	assert(rc);

	notes_context_t* ctx = skb_malloc(sizeof(notes_context_t));
	SKB_ZERO_STRUCT(ctx);

	ctx->base.create = notes_create;
	ctx->base.destroy = notes_destroy;
	ctx->base.on_key = notes_on_key;
	ctx->base.on_char = notes_on_char;
	ctx->base.on_mouse_button = notes_on_mouse_button;
	ctx->base.on_mouse_move = notes_on_mouse_move;
	ctx->base.on_mouse_scroll = notes_on_mouse_scroll;
	ctx->base.on_update = notes_on_update;

	ctx->window = window;
	ctx->rc = rc;
	render_reset_atlas(rc, NULL);

	ctx->rich_text_clipboard = skb_rich_text_create();
	ctx->rich_text_clipboard_hash = 0;

	ctx->font_collection = skb_font_collection_create();
	assert(ctx->font_collection);

	const skb_font_create_params_t fake_italic_params = {
		.slant = SKB_DEFAULT_SLANT
	};

	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Italic.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Bold.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_PARAMS_OR_FAIL("data/IBMPlexSans-Bold.ttf", SKB_FONT_FAMILY_DEFAULT, &fake_italic_params);

	LOAD_FONT_OR_FAIL("data/IBMPlexMono-Regular.ttf", SKB_FONT_FAMILY_MONOSPACE);
	LOAD_FONT_OR_FAIL("data/IBMPlexMono-Italic.ttf", SKB_FONT_FAMILY_MONOSPACE);
	LOAD_FONT_OR_FAIL("data/IBMPlexMono-Bold.ttf", SKB_FONT_FAMILY_MONOSPACE);
	LOAD_FONT_OR_FAIL("data/IBMPlexMono-BoldItalic.ttf", SKB_FONT_FAMILY_MONOSPACE);

	LOAD_FONT_OR_FAIL("data/IBMPlexSansArabic-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSansJP-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSansKR-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSansDevanagari-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSansBrahmi-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSerifBalinese-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSansTamil-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSansBengali-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoSansThai-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/NotoColorEmoji-Regular.ttf", SKB_FONT_FAMILY_EMOJI);

	ctx->temp_alloc = skb_temp_alloc_create(512*1024);
	assert(ctx->temp_alloc);

	ctx->view = (view_t) { .cx = 400.f, .cy = 120.f, .scale = 1.f, .zoom_level = 0.f, };

	ctx->show_caret_details = false;
	ctx->show_run_details = false;

	ctx->hand_cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

	ctx->attribute_collection = skb_attribute_collection_create();
	assert(ctx->attribute_collection);

	// Create paragraph styles.

	const skb_color_t header_color = skb_rgba(64,64,64,255);
	const skb_color_t body_color = skb_rgba(16,16,16,255);
	const skb_color_t quote_color = skb_rgba(16,16,16,192);
	const skb_color_t code_color = skb_rgba(64,50,128,255);
	const skb_color_t code_bg_color = skb_rgba(64,50,128,32);
	const skb_color_t link_color = skb_rgba(64,0,255,255);

	{
		const skb_attribute_t h1_attributes[] = {
			skb_attribute_make_font_size(32.f),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, header_color),
			skb_attribute_make_paragraph_padding(0,0,20,5),
		};

		const skb_attribute_t h2_attributes[] = {
			skb_attribute_make_font_size(22.f),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, header_color),
			skb_attribute_make_paragraph_padding(0,0,10,5),
		};

		const skb_attribute_t body_attributes[] = {
			skb_attribute_make_font_size(16.f),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, body_color),
			skb_attribute_make_line_height(SKB_LINE_HEIGHT_METRICS_RELATIVE, 1.3f),
			skb_attribute_make_paragraph_padding(0,0,5,5),
		};

		const skb_attribute_t quoteblock_attributes[] = {
			skb_attribute_make_font_size(16.f),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, quote_color),
			skb_attribute_make_line_height(SKB_LINE_HEIGHT_METRICS_RELATIVE, 1.3f),
			skb_attribute_make_paragraph_padding(16,16,5,5),
			skb_attribute_make_indent_increment(16.f, 0.f),
			skb_attribute_make_group_tag(SKB_TAG_STR("quote")),
			skb_attribute_make_paint_color(SKB_PAINT_INDENT_DECORATION, SKB_PAINT_STATE_DEFAULT, skb_rgba(0,0,0,64)),
			skb_attribute_make_indent_decoration(0, -1, 13, 4),
		};

		const skb_attribute_t list_attributes[] = {
			skb_attribute_make_font_size(16.f),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, body_color),
			skb_attribute_make_paragraph_padding(0,0,5,5),
//			skb_attribute_make_list_marker(SKB_LIST_MARKER_CODEPOINT, 32, 5, 0x2013), // en-dash
			skb_attribute_make_list_marker(SKB_LIST_MARKER_CODEPOINT, 32, 5, 0x2022), // bullet
		};

		const skb_attribute_t codeblock_attributes[] = {
			skb_attribute_make_font_family(SKB_FONT_FAMILY_MONOSPACE),
			skb_attribute_make_font_size(16.f),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, code_color),
			skb_attribute_make_paragraph_padding(20,20,10,10),
			skb_attribute_make_paint_color(SKB_PAINT_PARAGRAPH_BACKGROUND, SKB_PAINT_STATE_DEFAULT, code_bg_color),
			skb_attribute_make_group_tag(SKB_TAG_STR("code"))
		};

		const skb_attribute_t ordered_list_attributes[] = {
			skb_attribute_make_font_size(16.f),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, body_color),
			skb_attribute_make_paragraph_padding(0,0,5,5),
			skb_attribute_make_list_marker(SKB_LIST_MARKER_COUNTER_LOWER_LATIN, 32, 5, 0),
			skb_attribute_make_list_marker(SKB_LIST_MARKER_COUNTER_DECIMAL, 32, 5, 0), // Most prominent attrib is the last one, this will be picked first.
		};

		const skb_attribute_t underline_attributes[] = {
			skb_attribute_make_decoration(SKB_DECORATION_UNDERLINE, SKB_DECORATION_STYLE_SOLID, 1.f, 1.f, SKB_PAINT_TEXT),
		};

		const skb_attribute_t strikethrough_attributes[] = {
			skb_attribute_make_decoration(SKB_DECORATION_THROUGHLINE, SKB_DECORATION_STYLE_SOLID, 1.5f, 0.f, SKB_PAINT_TEXT),
		};

		const skb_attribute_t italic_attributes[] = {
			skb_attribute_make_font_style(SKB_STYLE_ITALIC),
		};

		const skb_attribute_t bold_attributes[] = {
			skb_attribute_make_font_weight(SKB_WEIGHT_BOLD),
		};

		const skb_attribute_t code_attributes[] = {
			skb_attribute_make_font_family(SKB_FONT_FAMILY_MONOSPACE),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, code_color),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT_BACKGROUND, SKB_PAINT_STATE_DEFAULT, code_bg_color),
			skb_attribute_make_inline_padding(4,4,0,0),
		};

		const skb_attribute_t superscript_attributes[] = {
			skb_attribute_make_font_size_scaling(SKB_FONT_SIZE_SCALING_SUPERSCRIPT, 0.f),
			skb_attribute_make_baseline_shift(SKB_BASELINE_SHIFT_SUPERSCRIPT, 0.f),
		};

		const skb_attribute_t subscript_attributes[] = {
			skb_attribute_make_font_size_scaling(SKB_FONT_SIZE_SCALING_SUBSCRIPT, 0.f),
			skb_attribute_make_baseline_shift(SKB_BASELINE_SHIFT_SUBSCRIPT, 0.f),
		};

		const skb_attribute_t link_attributes[] = {
			skb_attribute_make_decoration(SKB_DECORATION_UNDERLINE, SKB_DECORATION_STYLE_SOLID, 1.f, 1.f, SKB_PAINT_TEXT),
			skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, link_color),
		};

		const skb_attribute_t align_start[] = {
			skb_attribute_make_horizontal_align(SKB_ALIGN_START),
		};

		const skb_attribute_t align_center[] = {
			skb_attribute_make_horizontal_align(SKB_ALIGN_CENTER),
		};

		const skb_attribute_t align_end[] = {
			skb_attribute_make_horizontal_align(SKB_ALIGN_END),
		};

		const skb_attribute_t dir_ltr[] = {
			skb_attribute_make_text_base_direction(SKB_DIRECTION_LTR),
		};
		const skb_attribute_t dir_rtl[] = {
			skb_attribute_make_text_base_direction(SKB_DIRECTION_RTL),
		};

		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "H1", "paragraph", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(h1_attributes));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "H2", "paragraph", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(h2_attributes));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "BODY", "paragraph", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(body_attributes));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "LI", "paragraph", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(list_attributes));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "OL", "paragraph", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(ordered_list_attributes));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "CODE", "paragraph", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(codeblock_attributes));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "QUOTE", "paragraph", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(quoteblock_attributes));

		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "align-start", "align", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(align_start));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "align-center", "align", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(align_center));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "align-end", "align", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(align_end));

		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "ltr", "text-dir", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(dir_ltr));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "rtl", "text-dir", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(dir_rtl));

		skb_attribute_collection_add_set(ctx->attribute_collection, "s", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(strikethrough_attributes));
		skb_attribute_collection_add_set(ctx->attribute_collection, "u", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(underline_attributes));
		skb_attribute_collection_add_set(ctx->attribute_collection, "i", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(italic_attributes));
		skb_attribute_collection_add_set(ctx->attribute_collection, "b", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(bold_attributes));
		skb_attribute_collection_add_set(ctx->attribute_collection, "code", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(code_attributes));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "sup", "baseline-shift", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(superscript_attributes));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "sub", "baseline-shift", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(subscript_attributes));
		skb_attribute_collection_add_set(ctx->attribute_collection, "link", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(link_attributes));
	}

	const skb_attribute_t layout_attributes[] = {
		skb_attribute_make_text_wrap(SKB_WRAP_WORD_CHAR),
		skb_attribute_make_text_overflow(SKB_OVERFLOW_SCROLL),
		skb_attribute_make_tab_stop_increment(16.f * 2.f),
		skb_attribute_make_indent_increment(32.f, 0.f),
		skb_attribute_make_caret_padding(25, 25),
	};

	skb_attribute_set_t body = skb_attribute_set_make_reference_by_name(ctx->attribute_collection, "BODY");

	const skb_attribute_t composition_attributes[] = {
		skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT, skb_rgba(0,128,192,255)),
		skb_attribute_make_decoration(SKB_DECORATION_UNDERLINE, SKB_DECORATION_STYLE_DOTTED, 0.f, 1.f, SKB_PAINT_TEXT),
	};

	skb_editor_params_t edit_params = {
		.font_collection = ctx->font_collection,
		.attribute_collection = ctx->attribute_collection,
		.editor_width = 300.f, //SKB_AUTO_SIZE,
		.editor_height = 400.f, //SKB_AUTO_SIZE,
		.layout_attributes = SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(layout_attributes),
		.paragraph_attributes = body,
		.composition_attributes = SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(composition_attributes),
	};

	ctx->editor = skb_editor_create(&edit_params);
	assert(ctx->editor);
	skb_editor_set_text_utf8(ctx->editor, ctx->temp_alloc, "Edit...", -1);

	ime_set_handler(ime_handler, ctx);

	update_ime_rect(ctx);

	return ctx;

error:
	notes_destroy(ctx);
	return NULL;
}

void notes_destroy(void* ctx_ptr)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	skb_font_collection_destroy(ctx->font_collection);
	skb_attribute_collection_destroy(ctx->attribute_collection);
	skb_rich_text_destroy(ctx->rich_text_clipboard);
	skb_editor_destroy(ctx->editor);
	skb_temp_alloc_destroy(ctx->temp_alloc);

	glfwDestroyCursor(ctx->hand_cursor);

	SKB_ZERO_STRUCT(ctx);

	ime_cancel();
	ime_set_handler(NULL, NULL);

	skb_free(ctx);
}

static bool nodes__match_prefix_at_paragraph_start(const skb_editor_t* editor, const skb_paragraph_position_t paragraph_pos, const char* value_utf8, skb_text_range_t* prefix_selection)
{
	const skb_text_t* paragraph_text = skb_editor_get_paragraph_text(editor, paragraph_pos.paragraph_idx);
	if (!paragraph_text)
		return false;

	const int32_t paragraph_utf32_count = skb_text_get_utf32_count(paragraph_text);
	const uint32_t* paragraph_utf32 = skb_text_get_utf32(paragraph_text);

	const int32_t value_utf8_count = (int32_t)strlen(value_utf8);
	uint32_t value_utf32[32];
	int32_t value_utf32_count = skb_utf8_to_utf32(value_utf8, value_utf8_count, value_utf32, SKB_COUNTOF(value_utf32));

	// If the text is shorter than the prefix, no match.
	if (value_utf32_count > paragraph_utf32_count)
		return false;

	// Expect that the cursor is richt after the prefix.
	if (paragraph_pos.text_offset != value_utf32_count)
		return false;

	// Check that the prefix matches
	int32_t match_count = 0;
	while (match_count < value_utf32_count && value_utf32[match_count] == paragraph_utf32[match_count])
		match_count++;

	if (match_count != value_utf32_count)
		return false;

	// Found match
	const int32_t prefix_global_start_offset = skb_editor_get_paragraph_global_text_offset(editor, paragraph_pos.paragraph_idx);
	prefix_selection->start = (skb_text_position_t) { .offset = prefix_global_start_offset + 0 };
	prefix_selection->end = (skb_text_position_t) { .offset = prefix_global_start_offset + value_utf32_count };

	return true;
}

static void notes__handle_space(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_attribute_collection_t* attribute_collection, uint32_t edit_mods)
{
	skb_attribute_t h1 = skb_attribute_make_reference_by_name(attribute_collection, "H1");
	skb_attribute_t h2 = skb_attribute_make_reference_by_name(attribute_collection, "H2");
	skb_attribute_t body = skb_attribute_make_reference_by_name(attribute_collection, "BODY");
	skb_attribute_t quoteblock = skb_attribute_make_reference_by_name(attribute_collection, "QUOTE");
	skb_attribute_t list = skb_attribute_make_reference_by_name(attribute_collection, "LI");
	skb_attribute_t ordered_list = skb_attribute_make_reference_by_name(attribute_collection, "OL");
	skb_attribute_t codeblock = skb_attribute_make_reference_by_name(attribute_collection, "CODE");

	const int32_t selection_count = skb_editor_get_text_range_count(editor, SKB_CURRENT_SELECTION);
	const skb_paragraph_position_t caret_paragraph_pos = skb_editor_get_paragraph_position_from_text_position(editor, SKB_CURRENT_SELECTION_END);

	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, body)) {
		if (selection_count == 0) {
			skb_text_range_t prefix_selection;
			// Body -> H2
			if (nodes__match_prefix_at_paragraph_start(editor, caret_paragraph_pos, "##", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				// Remove prefix
				skb_editor_insert_text_utf32(editor, temp_alloc, prefix_selection, NULL, 0);
				// Apply style to paragraph
				const skb_text_range_t paragraph_range = skb_editor_get_paragraph_text_range(editor, caret_paragraph_pos.paragraph_idx);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, paragraph_range, h2);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
			// Body -> H1
			if (nodes__match_prefix_at_paragraph_start(editor, caret_paragraph_pos, "#", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				// Remove prefix
				skb_editor_insert_text_utf32(editor, temp_alloc, prefix_selection, NULL, 0);
				// Apply style to paragraph
				const skb_text_range_t paragraph_range = skb_editor_get_paragraph_text_range(editor, caret_paragraph_pos.paragraph_idx);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, paragraph_range, h1);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
			// Body -> List
			if (nodes__match_prefix_at_paragraph_start(editor, caret_paragraph_pos, "-", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				// Remove prefix
				skb_editor_insert_text_utf32(editor, temp_alloc, prefix_selection, NULL, 0);
				// Apply style to paragraph
				const skb_text_range_t paragraph_range = skb_editor_get_paragraph_text_range(editor, caret_paragraph_pos.paragraph_idx);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, paragraph_range, list);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
			// Body -> Ordered List
			if (nodes__match_prefix_at_paragraph_start(editor, caret_paragraph_pos, ".", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				// Remove prefix
				skb_editor_insert_text_utf32(editor, temp_alloc, prefix_selection, NULL, 0);
				// Apply style to paragraph
				const skb_text_range_t paragraph_range = skb_editor_get_paragraph_text_range(editor, caret_paragraph_pos.paragraph_idx);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, paragraph_range, ordered_list);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
			// Body -> Quote
			if (nodes__match_prefix_at_paragraph_start(editor, caret_paragraph_pos, "|", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				// Remove prefix
				skb_editor_insert_text_utf32(editor, temp_alloc, prefix_selection, NULL, 0);
				// Apply style to paragraph
				const skb_text_range_t paragraph_range = skb_editor_get_paragraph_text_range(editor, caret_paragraph_pos.paragraph_idx);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, paragraph_range, quoteblock);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
			// Body -> codeblock
			if (nodes__match_prefix_at_paragraph_start(editor, caret_paragraph_pos, "```", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				// Remove prefix
				skb_editor_insert_text_utf32(editor, temp_alloc, prefix_selection, NULL, 0);
				// Apply style to paragraph
				const skb_text_range_t paragraph_range = skb_editor_get_paragraph_text_range(editor, caret_paragraph_pos.paragraph_idx);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, paragraph_range, codeblock);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
		}
	}

	// Insert space
	skb_editor_insert_codepoint(editor, temp_alloc, SKB_CURRENT_SELECTION, ' ');
}

static skb_text_range_t notes__make_paragraph_selection(skb_editor_t* editor, int32_t paragraph_idx, skb_range_t text_range)
{
	const int32_t global_offset = skb_editor_get_paragraph_global_text_offset(editor, paragraph_idx);
	return (skb_text_range_t) {
		.start = (skb_text_position_t) { .offset = global_offset + text_range.start },
		.end = (skb_text_position_t) { .offset = global_offset + text_range.end },
	};
}

static int32_t notes__get_paragraph_start_tab_count(const skb_editor_t* editor, int32_t paragraph_idx)
{
	int32_t tab_count = 0;
	const skb_text_t* text = skb_editor_get_paragraph_text(editor, paragraph_idx);
	if (text) {
		const uint32_t* utf32 = skb_text_get_utf32(text);
		const int32_t utf32_count = skb_text_get_utf32_count(text);
		while (tab_count < utf32_count && utf32[tab_count] == '\t')
			tab_count++;
	}
	return tab_count;
}

static void notes__handle_tab(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_attribute_collection_t* attribute_collection, uint32_t edit_mods)
{
	skb_attribute_t list = skb_attribute_make_reference_by_name(attribute_collection, "LI");
	skb_attribute_t ordered_list = skb_attribute_make_reference_by_name(attribute_collection, "OL");
	skb_attribute_t body = skb_attribute_make_reference_by_name(attribute_collection, "BODY");
	skb_attribute_t quote = skb_attribute_make_reference_by_name(attribute_collection, "QUOTE");
	skb_attribute_t codeblock = skb_attribute_make_reference_by_name(attribute_collection, "CODE");

	const int32_t selection_count = skb_editor_get_text_range_count(editor, SKB_CURRENT_SELECTION);
	const skb_paragraph_position_t caret_paragraph_pos = skb_editor_get_paragraph_position_from_text_position(editor, SKB_CURRENT_SELECTION_END);

	// Indent && Outdent
	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, list)
		|| skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, ordered_list)) {
		skb_attribute_t indent_level_delta = skb_attribute_make_indent_level((edit_mods & SKB_MOD_SHIFT) ? -1 : 1);
		skb_editor_set_paragraph_attribute_delta(editor, temp_alloc, SKB_CURRENT_SELECTION, indent_level_delta);
		return;
	}
	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, body)
		|| skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, quote)) {
		if (selection_count == 0 && caret_paragraph_pos.text_offset == 0) {
			skb_attribute_t indent_level_delta = skb_attribute_make_indent_level((edit_mods & SKB_MOD_SHIFT) ? -1 : 1);
			skb_editor_set_paragraph_attribute_delta(editor, temp_alloc, SKB_CURRENT_SELECTION, indent_level_delta);
			return;
		}
	}
	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, codeblock)) {
		if (edit_mods & SKB_MOD_SHIFT) {
			// Remove tabs from all paragraphs?
			const skb_range_t paragraph_range = skb_editor_get_paragraphs_range_from_text_range(editor, SKB_CURRENT_SELECTION);

			int32_t transaction_id = skb_editor_undo_transaction_begin(editor);

			for (int32_t pi = paragraph_range.start; pi < paragraph_range.end; pi++) {
				const int32_t tab_count = notes__get_paragraph_start_tab_count(editor, pi);
				if (tab_count > 0) {
					skb_text_range_t remove_range = notes__make_paragraph_selection(editor, pi, (skb_range_t){ .start = 0, .end = 1});
					skb_editor_insert_text_utf32(editor, temp_alloc, remove_range, NULL, 0);
				}
			}

			skb_editor_undo_transaction_end(editor, transaction_id);
			return;

		} else {
			if (selection_count > 0) {
				// Add tabs to all paragraphs
				const skb_range_t paragraph_range = skb_editor_get_paragraphs_range_from_text_range(editor, SKB_CURRENT_SELECTION);

				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);

				for (int32_t pi = paragraph_range.start; pi < paragraph_range.end; pi++) {
					skb_text_range_t insert_pos = notes__make_paragraph_selection(editor, pi, (skb_range_t){ .start = 0, .end = 0});
					const uint32_t tab[] = { '\t' };
					skb_editor_insert_text_utf32(editor, temp_alloc, insert_pos, tab, 1);
				}

				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
		}
	}

	// Insert tab
	skb_editor_insert_codepoint(editor, temp_alloc, SKB_CURRENT_SELECTION, '\t');
}

static void notes__handle_backspace(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_attribute_collection_t* attribute_collection, uint32_t edit_mods)
{
	skb_attribute_t body = skb_attribute_make_reference_by_name(attribute_collection, "BODY");
	skb_attribute_t quote = skb_attribute_make_reference_by_name(attribute_collection, "QUOTE");
	skb_attribute_t list = skb_attribute_make_reference_by_name(attribute_collection, "LI");
	skb_attribute_t ordered_list = skb_attribute_make_reference_by_name(attribute_collection, "OL");
	skb_attribute_t codeblock = skb_attribute_make_reference_by_name(attribute_collection, "CODE");

	const int32_t selection_count = skb_editor_get_text_range_count(editor, SKB_CURRENT_SELECTION);
	const skb_paragraph_position_t paragraph_pos = skb_editor_get_paragraph_position_from_text_position(editor, SKB_CURRENT_SELECTION_END);

	// List outdent
	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, list)
		|| skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, ordered_list)
		|| skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, quote)) {
		if (selection_count == 0 && paragraph_pos.text_offset == 0) {
			skb_attribute_set_t paragraph_attributes = skb_editor_get_paragraph_attributes(editor, paragraph_pos.paragraph_idx);
			const int32_t indent_level = skb_attributes_get_indent_level(paragraph_attributes, attribute_collection);
			if (indent_level == 0) {
				// Convert to body when no indent left.
				skb_editor_set_paragraph_attribute(editor, temp_alloc, SKB_CURRENT_SELECTION, body);
			} else {
				// Outdent
				skb_attribute_t indent_level_delta = skb_attribute_make_indent_level(-1);
				skb_editor_set_paragraph_attribute_delta(editor, temp_alloc, SKB_CURRENT_SELECTION, indent_level_delta);
			}
			return;
		}
	}

	// Body outdent
	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, body)) {
		if (selection_count == 0 && paragraph_pos.text_offset == 0) {
			skb_attribute_set_t paragraph_attributes = skb_editor_get_paragraph_attributes(editor, paragraph_pos.paragraph_idx);
			const int32_t indent_level = skb_attributes_get_indent_level(paragraph_attributes, attribute_collection);
			if (indent_level > 0) {
				// Outdent
				skb_attribute_t indent_level_delta = skb_attribute_make_indent_level(-1);
				skb_editor_set_paragraph_attribute_delta(editor, temp_alloc, SKB_CURRENT_SELECTION, indent_level_delta);
				return;
			}
		}
	}

	// Process backspace
	skb_editor_process_key_pressed(editor, temp_alloc, SKB_KEY_BACKSPACE, edit_mods);
}

static void notes__handle_enter(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_attribute_collection_t* attribute_collection, uint32_t edit_mods)
{
	skb_attribute_t h1 = skb_attribute_make_reference_by_name(attribute_collection, "H1");
	skb_attribute_t h2 = skb_attribute_make_reference_by_name(attribute_collection, "H2");
	skb_attribute_t body = skb_attribute_make_reference_by_name(attribute_collection, "BODY");
	skb_attribute_t quote = skb_attribute_make_reference_by_name(attribute_collection, "QUOTE");
	skb_attribute_t list = skb_attribute_make_reference_by_name(attribute_collection, "LI");
	skb_attribute_t ordered_list = skb_attribute_make_reference_by_name(attribute_collection, "OL");
	skb_attribute_t codeblock = skb_attribute_make_reference_by_name(attribute_collection, "CODE");

	const int32_t selection_count = skb_editor_get_text_range_count(editor, SKB_CURRENT_SELECTION);
	const skb_paragraph_position_t caret_paragraph_pos = skb_editor_get_paragraph_position_from_text_position(editor, SKB_CURRENT_SELECTION_END);
	const int32_t paragraph_text_count = skb_editor_get_paragraph_text_count(editor, caret_paragraph_pos.paragraph_idx);
	const int32_t paragraph_text_count_no_linebreak = skb_editor_get_paragraph_text_content_count(editor, caret_paragraph_pos.paragraph_idx);

	// List -> body
	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, list)
		|| skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, ordered_list)
		|| skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, quote)) {
		if (selection_count == 0) {
			if (paragraph_text_count_no_linebreak == 0) { // Empty paragraph
				skb_editor_set_paragraph_attribute(editor, temp_alloc, SKB_CURRENT_SELECTION, body);
				return;
			}
		}
	}

	// H1,H2 -> body
	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, h1)
		|| skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, h2)) {
		if (selection_count == 0) {
			if (caret_paragraph_pos.text_offset >= (paragraph_text_count - 1)) {
				skb_editor_insert_paragraph(editor, temp_alloc, SKB_CURRENT_SELECTION, body);
				return;
			}
		}
	}

	// Match tabs on code
	if (skb_editor_has_paragraph_attribute(editor, SKB_CURRENT_SELECTION, codeblock)) {

		int32_t tab_count = notes__get_paragraph_start_tab_count(editor, caret_paragraph_pos.paragraph_idx);
		if (tab_count == paragraph_text_count_no_linebreak) {
			const int32_t prev_paragraph_text_count_no_linebreaks = skb_editor_get_paragraph_text_content_count(editor, caret_paragraph_pos.paragraph_idx - 1);
			const int32_t prev_tab_count = notes__get_paragraph_start_tab_count(editor, caret_paragraph_pos.paragraph_idx - 1);
			if (prev_tab_count == prev_paragraph_text_count_no_linebreaks) {
				// Two empty lines in a row, will end code block
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				// Remove first empty line, and the contents of the second (sans linebreak)
				skb_text_range_t paragraph_range = {
					.start = skb_editor_get_paragraph_content_start_pos(editor, caret_paragraph_pos.paragraph_idx - 1),
					.end = skb_editor_get_paragraph_content_end_pos(editor, caret_paragraph_pos.paragraph_idx),
				};
				skb_editor_insert_text_utf32(editor, temp_alloc, paragraph_range, NULL, 0);
				// Set the remaining empty line to body style
				skb_text_position_t paragraph_start = skb_editor_get_paragraph_content_start_pos(editor, caret_paragraph_pos.paragraph_idx - 1);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, (skb_text_range_t){ .start = paragraph_start, .end = paragraph_start }, body);

				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
		}

		// Match paragraph tabs
		int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
		// Enter
		skb_editor_insert_paragraph(editor, temp_alloc, SKB_CURRENT_SELECTION, (skb_attribute_t){0});
		// Match tabs on new line.
		if (tab_count > 0) {
			const uint32_t tabs[] = { '\t', '\t', '\t', '\t', '\t', '\t', '\t', '\t' };
			skb_editor_insert_text_utf32(editor, temp_alloc, SKB_CURRENT_SELECTION, tabs, skb_mini(tab_count, SKB_COUNTOF(tabs)));
		}
		skb_editor_undo_transaction_end(editor, transaction_id);
		return;
	}

	// Process enter
	skb_editor_process_key_pressed(editor, temp_alloc, SKB_KEY_ENTER, edit_mods);
}


void notes_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	uint32_t edit_mods = 0;
	if (mods & GLFW_MOD_SHIFT)
		edit_mods |= SKB_MOD_SHIFT;
	if (mods & GLFW_MOD_CONTROL)
		edit_mods |= SKB_MOD_CONTROL;

	if (action == GLFW_PRESS || action == GLFW_REPEAT) {
		ctx->allow_char = true;
		if (key == GLFW_KEY_V && (mods & GLFW_MOD_CONTROL)) {
			// Paste
			const char* clipboard_text = glfwGetClipboardString(window);
			const uint64_t clipboard_hash = skb_hash64_append_str(skb_hash64_empty(), clipboard_text);
			if (clipboard_hash == ctx->rich_text_clipboard_hash && (mods & GLFW_MOD_SHIFT) == 0) {
				// The text matches what we copied, paste the rich text version instead.
				skb_editor_insert_rich_text(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, ctx->rich_text_clipboard);
			} else {
				// Paste plain text from clipboard.
				skb_editor_insert_text_utf8(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, clipboard_text, -1);
			}
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_Z && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT) == 0)
			skb_editor_undo(ctx->editor, ctx->temp_alloc);
		if (key == GLFW_KEY_Z && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT))
			skb_editor_redo(ctx->editor, ctx->temp_alloc);
		if (key == GLFW_KEY_LEFT)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_LEFT, edit_mods);
		if (key == GLFW_KEY_RIGHT)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_RIGHT, edit_mods);
		if (key == GLFW_KEY_UP)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_UP, edit_mods);
		if (key == GLFW_KEY_DOWN)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_DOWN, edit_mods);
		if (key == GLFW_KEY_HOME)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_HOME, edit_mods);
		if (key == GLFW_KEY_END)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_END, edit_mods);
		if (key == GLFW_KEY_DELETE)
			skb_editor_process_key_pressed(ctx->editor, ctx->temp_alloc, SKB_KEY_DELETE, edit_mods);
		if (key == GLFW_KEY_BACKSPACE)
			notes__handle_backspace(ctx->editor, ctx->temp_alloc, ctx->attribute_collection, edit_mods);
		if (key == GLFW_KEY_ENTER)
			notes__handle_enter(ctx->editor, ctx->temp_alloc, ctx->attribute_collection, edit_mods);
		if (key == GLFW_KEY_TAB)
			notes__handle_tab(ctx->editor, ctx->temp_alloc, ctx->attribute_collection, edit_mods);
		if (key == GLFW_KEY_SPACE) {
			notes__handle_space(ctx->editor, ctx->temp_alloc, ctx->attribute_collection, edit_mods);
			ctx->allow_char = false;
		}

		update_ime_rect(ctx);
	}
	if (action == GLFW_PRESS) {

		if (key == GLFW_KEY_0 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT) == 0) {
			// Body
			skb_attribute_t body = skb_attribute_make_reference_by_name(ctx->attribute_collection, "BODY");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, body);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_1 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT) == 0) {
			// H1
			skb_attribute_t h1 = skb_attribute_make_reference_by_name(ctx->attribute_collection, "H1");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, h1);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_2 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT) == 0) {
			// H2
			skb_attribute_t h2 = skb_attribute_make_reference_by_name(ctx->attribute_collection, "H2");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, h2);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_6 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT) == 0) {
			// H2
			skb_attribute_t quote = skb_attribute_make_reference_by_name(ctx->attribute_collection, "QUOTE");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, quote);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_7 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT) == 0) {
			// Codeblock
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "CODE");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, list);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_8 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT) == 0) {
			// List
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "LI");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, list);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_9 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT) == 0) {
			// Ordered list
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "OL");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, list);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_L && (mods & GLFW_MOD_CONTROL)) {
			// Align left
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-start");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, list);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_T && (mods & GLFW_MOD_CONTROL)) {
			// Align Center
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-center");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, list);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL)) {
			// Align end
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-end");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, list);
			ctx->allow_char = false;
		}

		if (key == GLFW_KEY_A && (mods & GLFW_MOD_CONTROL)) {
			// Select all
			skb_editor_select_all(ctx->editor);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_B && (mods & GLFW_MOD_CONTROL)) {
			// Bold
			skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, skb_attribute_make_font_weight(SKB_WEIGHT_BOLD));
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_I && (mods & GLFW_MOD_CONTROL)) {
			// Italic
			skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, skb_attribute_make_font_style(SKB_STYLE_ITALIC));
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_U && (mods & GLFW_MOD_CONTROL)) {
			// Underline
			skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, skb_attribute_make_reference_by_name(ctx->attribute_collection, "u"));
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_E && (mods & GLFW_MOD_CONTROL)) {
			// Inline Code
			skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, skb_attribute_make_reference_by_name(ctx->attribute_collection, "code"));
			ctx->allow_char = false;
		}

		if (key == GLFW_KEY_ESCAPE) {
			// Clear selection
			if (skb_editor_get_text_range_count(ctx->editor, SKB_CURRENT_SELECTION) > 0)
				skb_editor_select_none(ctx->editor);
			else
				glfwSetWindowShouldClose(window, GL_TRUE);
		}
		if (key == GLFW_KEY_X) {
			if ((mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT)) {
				// Strike through
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, skb_attribute_make_reference_by_name(ctx->attribute_collection, "s"));
				ctx->allow_char = false;
			} else if (mods & GLFW_MOD_CONTROL) {
				// Cut
				int32_t text_len = skb_editor_get_text_utf8_in_range(ctx->editor, SKB_CURRENT_SELECTION, NULL, -1);
				char* text = SKB_TEMP_ALLOC(ctx->temp_alloc, char, text_len + 1);
				text_len = skb_editor_get_text_utf8_in_range(ctx->editor, SKB_CURRENT_SELECTION, text, text_len);
				text[text_len] = '\0';
				glfwSetClipboardString(window, text);

				// Keep copy of the selection as rich text, so that we can paste as rich text.
				skb_editor_get_rich_text_in_range(ctx->editor, SKB_CURRENT_SELECTION, ctx->rich_text_clipboard);
				ctx->rich_text_clipboard_hash = skb_hash64_append_str(skb_hash64_empty(), text);

				SKB_TEMP_FREE(ctx->temp_alloc, text);
				skb_editor_insert_text_utf8(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, NULL, 0);
				ctx->allow_char = false;
			}
		}
		if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
			// Copy
			int32_t text_len = skb_editor_get_text_utf8_count_in_range(ctx->editor, SKB_CURRENT_SELECTION);
			char* text = SKB_TEMP_ALLOC(ctx->temp_alloc, char, text_len + 1);
			text_len = skb_editor_get_text_utf8_in_range(ctx->editor, SKB_CURRENT_SELECTION, text, text_len);
			text[text_len] = '\0';
			glfwSetClipboardString(window, text);

			// Keep copy of the selection as rich text, so that we can paste as rich text.
			skb_editor_get_rich_text_in_range(ctx->editor, SKB_CURRENT_SELECTION, ctx->rich_text_clipboard);
			ctx->rich_text_clipboard_hash = skb_hash64_append_str(skb_hash64_empty(), text);

			SKB_TEMP_FREE(ctx->temp_alloc, text);
			ctx->allow_char = false;		}

		update_ime_rect(ctx);

		if (key == GLFW_KEY_F8) {
			ctx->show_caret_details = !ctx->show_caret_details;
		}
		if (key == GLFW_KEY_F9) {
			ctx->show_run_details = !ctx->show_run_details;
		}
	}
}

void notes_on_char(void* ctx_ptr, unsigned int codepoint)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (ctx->allow_char)
		skb_editor_insert_codepoint(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, codepoint);
}

static skb_vec2_t transform_mouse_pos(notes_context_t* ctx, float mouse_x, float mouse_y)
{
	return (skb_vec2_t) {
		.x = (mouse_x - ctx->view.cx) / ctx->view.scale,
		.y = (mouse_y - ctx->view.cy) / ctx->view.scale,
	};
}

void notes_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	int32_t mouse_mods = 0;
	if (mods & GLFW_MOD_SHIFT)
		mouse_mods |= SKB_MOD_SHIFT;
	if (mods & GLFW_MOD_CONTROL)
		mouse_mods |= SKB_MOD_CONTROL;

	if (button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (action == GLFW_PRESS) {
			if (!ctx->drag_view) {
				view_drag_start(&ctx->view, mouse_x, mouse_y);
				ctx->drag_view = true;
			}
		}
		if (action == GLFW_RELEASE) {
			if (ctx->drag_view) {
				ctx->drag_view = false;
			}
		}
	}

	if (button == GLFW_MOUSE_BUTTON_LEFT) {

		if (ctx->ui.hover || ctx->ui.active) {
			if (action == GLFW_PRESS) {
				ime_cancel();
				ctx->ui.mouse_pressed = true;
			}
			if (action == GLFW_RELEASE) {
				ctx->ui.mouse_released = true;
			}
			ctx->ui.mouse_pos.x = mouse_x;
			ctx->ui.mouse_pos.y = mouse_y;
		} else {
			// caret hit testing
			if (action == GLFW_PRESS) {
				if (!ctx->drag_text) {
					ime_cancel();
					ctx->drag_text = true;
					skb_vec2_t pos = transform_mouse_pos(ctx, mouse_x, mouse_y);
					skb_vec2_t view_offset = skb_editor_get_view_offset(ctx->editor);
					pos = skb_vec2_sub(pos, view_offset);
					skb_editor_process_mouse_click(ctx->editor, pos.x, pos.y, mouse_mods, glfwGetTime());
				}
			}

			if (action == GLFW_RELEASE) {
				if (ctx->drag_text) {
					ctx->drag_text = false;
				}
				ctx->ui.mouse_released = true;
			}

		}


	}

	update_ime_rect(ctx);}

void notes_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (ctx->drag_view) {
		view_drag_move(&ctx->view, mouse_x, mouse_y);
		update_ime_rect(ctx);
	}

	if (ctx->drag_text) {
		skb_vec2_t pos = transform_mouse_pos(ctx, mouse_x, mouse_y);
		skb_vec2_t view_offset = skb_editor_get_view_offset(ctx->editor);
		pos = skb_vec2_sub(pos, view_offset);
		skb_editor_process_mouse_drag(ctx->editor, pos.x, pos.y);
		update_ime_rect(ctx);
	} else {
		ctx->ui.mouse_pos.x = mouse_x;
		ctx->ui.mouse_pos.y = mouse_y;
	}
}

void notes_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	const float zoom_speed = 0.2f;
	view_scroll_zoom(&ctx->view, mouse_x, mouse_y, delta_y * zoom_speed);
}

typedef struct draw_selection_context_t {
	float x;
	float y;
	skb_color_t color;
	render_context_t* renderer;
} draw_selection_context_t;

static void draw_selection_rect(skb_rect2_t rect, void* context)
{
	draw_selection_context_t* ctx = (draw_selection_context_t*)context;
	debug_render_filled_rect(ctx->renderer, ctx->x + rect.x, ctx->y + rect.y, rect.width, rect.height, ctx->color);
}


static void ui_frame_begin(ui_context_t* ui, render_context_t* rc)
{
	ui->rc = rc;
	ui->id_gen = 1;
	ui->hover = ui->next_hover;
	ui->next_hover = 0;
	ui->went_active = 0;
}

static void ui_frame_end(ui_context_t* ui)
{
	ui->rc = NULL;
	ui->mouse_pressed = false;
	ui->mouse_released = false;
}

static skb_vec2_t ui_get_mouse_pos(const ui_context_t* ui)
{
	return render_inv_transform_point(ui->rc, (skb_vec2_t){ui->mouse_pos.x, ui->mouse_pos.y});
}

static int32_t ui_make_id(ui_context_t* ui)
{
	return ui->id_gen++;
}

static bool ui_button_logic(ui_context_t* ui, int32_t id, bool over)
{
	bool res = false;

	if (over)
		ui->next_hover = id;

	if (ui->active == 0) {
		// process down
		if (ui->hover == id && ui->mouse_pressed) {
			ui->active = id;
			ui->went_active = id;
//			ui->focus = id;
		}
	}

	// if button is active, then react on left up
	if (ui->active == id) {
		if (ui->mouse_released) {
			if (ui->hover == id)
				res = true;
			ui->active = 0;
		}
	}

	return res;
}

static bool ui_button(ui_context_t* ui, skb_rect2_t rect, const char* text, bool selected)
{
	const int32_t id = ui_make_id(ui);
	const bool over = skb_rect2_pt_inside(rect, ui_get_mouse_pos(ui));
	const bool res = ui_button_logic(ui, id, over);

	skb_color_t bg_col = skb_rgba(255,255,255,128);
	skb_color_t text_col = skb_rgba(0,0,0,220);
	if (selected) {
		bg_col = skb_rgba(0,192,220,192);
		text_col = skb_rgba(255,255,255,220);
	}
	if (ui->active == id) {
		bg_col.a = 255;
		text_col.a = 255;
	} else if (ui->hover == id) {
		bg_col.a = 192;
	}

	debug_render_filled_rect(ui->rc, rect.x, rect.y, rect.width, rect.height, bg_col);
	debug_render_text(ui->rc, rect.x + rect.width * 0.5f+1,rect.y + rect.height*0.5f + 6.f, 17, RENDER_ALIGN_CENTER, text_col, text);

	return res;
}
typedef enum {
	UI_SCROLLBAR_HORIZONTAL,
	UI_SCROLLBAR_VERTICAL,
} ui_scrollbar_dir_t;

static skb_rect2_t ui__make_handle_rect(skb_rect2_t rect, ui_scrollbar_dir_t dir, float content_ratio, float offset_ratio)
{
	if (dir == UI_SCROLLBAR_VERTICAL) {
		return (skb_rect2_t) {
			.x = rect.x,
			.y = rect.y + offset_ratio * rect.height,
			.width = rect.width,
			.height = content_ratio * rect.height,
		};
	}
	return (skb_rect2_t) {
		.x = rect.x + offset_ratio * rect.width,
		.y = rect.y,
		.width = content_ratio * rect.width,
		.height = rect.height,
	};
}

static bool ui_scrollbar(ui_context_t* ui, skb_rect2_t rect, ui_scrollbar_dir_t dir, float view_size, float content_size, float* content_offset)
{
	static skb_vec2_t start_mouse_pos = {0};
	static float start_offset = 0.f;
	static int32_t drag_id = 0;

	float content_ratio = content_size > 0.f ? skb_clampf(view_size / content_size, 0.f, 1.f) : 0.f;
	float offset_ratio = content_size > 0.f ? skb_clampf(*content_offset / content_size, 0.f, 1.f - content_ratio) : 0.f;

	skb_rect2_t handle_rect = ui__make_handle_rect(rect, dir, content_ratio, offset_ratio);

	const skb_vec2_t mouse_pos = ui_get_mouse_pos(ui);

	int32_t bg_id = ui_make_id(ui);
	bool bg_over = skb_rect2_pt_inside(rect, mouse_pos);
	bool bg_res = ui_button_logic(ui, bg_id, bg_over);

	int32_t handle_id = ui_make_id(ui);
	bool handle_over = skb_rect2_pt_inside(handle_rect, mouse_pos);
	bool handle_res = ui_button_logic(ui, handle_id, handle_over);

	skb_color_t bg_col = skb_rgba(0,0,0,32);
	skb_color_t handle_col = skb_rgba(0,0,0,64);
/*	if (ctx->ui.active == bg_id)
		bg_col.a = 255;
	if (ctx->ui.hover == bg_id)
		bg_col.a = 192;*/

	if (ui->active == handle_id)
		handle_col.a = 128;
	else if (ui->hover == handle_id)
		handle_col.a = 96;

	bool changed = false;
	if (ui->went_active == handle_id) {
		// Start drag
		start_mouse_pos = mouse_pos;
		start_offset = *content_offset;
		drag_id = handle_id;
	}
	if (drag_id == handle_id) {
		// Drag
		const float delta = dir == UI_SCROLLBAR_VERTICAL ? (mouse_pos.y - start_mouse_pos.y) / rect.height : (mouse_pos.x - start_mouse_pos.x) / rect.width;
		const float max_offset = skb_maxf(0.f, content_size - view_size);
		const float offset = skb_clampf(start_offset + delta * content_size, 0.f, max_offset);

		if (skb_absf(*content_offset - offset) > 1e-6f) {
			*content_offset = offset;
			// Update handle, so that we get immediate feedback.
			offset_ratio = content_size > 0.f ? skb_clampf(*content_offset / content_size, 0.f, 1.f - content_ratio) : 0.f;
			handle_rect = ui__make_handle_rect(rect, dir, content_ratio, offset_ratio);
			changed = true;
		}
	}
	if (drag_id == handle_id && ui->active != handle_id) {
		// End drag
		drag_id = 0;
	}

	debug_render_stroked_rect(ui->rc, rect.x, rect.y, rect.width, rect.height, bg_col, 1.f);
	debug_render_filled_rect(ui->rc, handle_rect.x + 2, handle_rect.y + 2, handle_rect.width - 4, handle_rect.height - 4, handle_col);

	return changed;
}


void notes_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	ui_frame_begin(&ctx->ui, ctx->rc);

	{
		skb_temp_alloc_stats_t stats = skb_temp_alloc_stats(ctx->temp_alloc);
		debug_render_text(ctx->rc, (float)view_width - 20,20, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,220), "Temp alloc  used:%.1fkB  allocated:%.1fkB", (float)stats.used / 1024.f, (float)stats.allocated / 1024.f);
		skb_temp_alloc_stats_t render_stats = skb_temp_alloc_stats(render_get_temp_alloc(ctx->rc));
		debug_render_text(ctx->rc, (float)view_width - 20,40, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,220), "Render Temp alloc  used:%.1fkB  allocated:%.1fkB", (float)render_stats.used / 1024.f, (float)render_stats.allocated / 1024.f);
	}

	render_push_transform(ctx->rc, ctx->view.cx, ctx->view.cy, ctx->view.scale);

	// Draw visual result

	{
		skb_color_t sel_color = skb_rgba(255,192,192,255);
		skb_color_t caret_color = skb_rgba(255,128,128,255);

		const skb_editor_params_t* editor_params = skb_editor_get_params(ctx->editor);
		const skb_text_overflow_t text_overflow = skb_attributes_get_text_overflow(editor_params->layout_attributes, editor_params->attribute_collection);

		skb_vec2_t view_offset = skb_editor_get_view_offset(ctx->editor);
		skb_rect2_t editor_content_bounds = skb_editor_get_layout_bounds(ctx->editor);
		const skb_rect2_t editor_view_bounds = skb_editor_get_view_bounds(ctx->editor);

		if (ctx->show_run_details) {
			debug_render_stroked_rect(ctx->rc, view_offset.x + editor_content_bounds.x, view_offset.y + editor_content_bounds.y, editor_content_bounds.width, editor_content_bounds.height, skb_rgba(255,100,128,128), 1.f);
		}

		debug_render_stroked_rect(ctx->rc, editor_view_bounds.x - 5, editor_view_bounds.y - 5, editor_view_bounds.width + 10, editor_view_bounds.height + 10, skb_rgba(0,0,0,128), 1.f);

		// Scrollbar
		if (text_overflow == SKB_OVERFLOW_SCROLL) {

			if (editor_content_bounds.height > editor_view_bounds.height) {
				const skb_rect2_t vert_scrollbar_rect = {
					.x = editor_view_bounds.x + editor_view_bounds.width + 5,
					.y = editor_view_bounds.y - 5,
					.width = 15,
					.height = editor_view_bounds.height+10,
				};
				float offset_vert = -view_offset.y;
				if (ui_scrollbar(&ctx->ui, vert_scrollbar_rect, UI_SCROLLBAR_VERTICAL, editor_view_bounds.height, editor_content_bounds.height, &offset_vert)) {
					skb_debug_log("y changed %f -> %f\n", view_offset.y, -offset_vert);
					view_offset.y = -offset_vert;
					skb_editor_set_view_offset(ctx->editor, view_offset);
				}
			}

			if (editor_content_bounds.width > editor_view_bounds.width) {
				const skb_rect2_t horiz_scrollbar_rect = {
					.x = editor_view_bounds.x - 5,
					.y = editor_view_bounds.y + editor_view_bounds.height + 5,
					.width = editor_view_bounds.width + 10,
					.height = 15,
				};
				float offset_horiz = -view_offset.x;
				if (ui_scrollbar(&ctx->ui, horiz_scrollbar_rect, UI_SCROLLBAR_HORIZONTAL, editor_view_bounds.width, editor_content_bounds.width, &offset_horiz)) {
					skb_debug_log("x changed %f -> %f\n", view_offset.x, -offset_horiz);
					view_offset.x = -offset_horiz;
					skb_editor_set_view_offset(ctx->editor, view_offset);
				}
			}
		}


		skb_text_range_t edit_selection = skb_editor_get_current_selection(ctx->editor);

		if (ctx->show_caret_details) {
			const char* affinity_str[] = { "-", "TR", "LD", "SOL", "EOL" };
			// Selection text
			float x = editor_view_bounds.x;
			float y = editor_view_bounds.y - 25;
			debug_render_text(ctx->rc, x, y, 10, RENDER_ALIGN_START, skb_rgba(0,0,0,128),
				"Selection   Start=%d/%s   End=%d/%s",
				edit_selection.start.offset, affinity_str[edit_selection.start.affinity],
				edit_selection.end.offset, affinity_str[edit_selection.end.affinity]);
		}

		if (text_overflow == SKB_OVERFLOW_SCROLL) {
			render_push_scissor(ctx->rc, editor_view_bounds.x, editor_view_bounds.y, editor_view_bounds.width, editor_view_bounds.height);
		}

		if (skb_editor_get_text_range_count(ctx->editor, edit_selection) > 0) {
			draw_selection_context_t sel_ctx = { .x = view_offset.x, .y = view_offset.y, .color = sel_color, .renderer = ctx->rc };
			skb_editor_iterate_text_range_bounds(ctx->editor, edit_selection, draw_selection_rect, &sel_ctx);
		}

		// Draw the layout
		const skb_rich_layout_t* edit_rich_layout = skb_editor_get_rich_layout(ctx->editor);
		render_draw_rich_layout(ctx->rc, NULL, view_offset.x, view_offset.y, edit_rich_layout, SKB_RASTERIZE_ALPHA_SDF);

		if (text_overflow == SKB_OVERFLOW_SCROLL) {
			render_pop_scissor(ctx->rc);
		}

		// Debug draw
		if (ctx->show_caret_details ||ctx->show_run_details) {
			for (int32_t pi = 0; pi < skb_editor_get_paragraph_count(ctx->editor); pi++) {
				const skb_layout_t* edit_layout = skb_editor_get_paragraph_layout(ctx->editor, pi);
				const skb_vec2_t edit_layout_offset = skb_editor_get_paragraph_offset(ctx->editor, pi);

				// Tick at paragraph start
				if (ctx->show_caret_details) {
					float x = view_offset.x + edit_layout_offset.x + editor_view_bounds.x + editor_view_bounds.width + 5;
					float y = view_offset.y + edit_layout_offset.y;
					debug_render_line(ctx->rc, x, y, x + 15, y, skb_rgba(0,0,0,128), 1.f);

					const int32_t text_count = skb_editor_get_paragraph_text_count(ctx->editor, pi);
					const int32_t content_count = skb_editor_get_paragraph_text_content_count(ctx->editor, pi);

					debug_render_text(ctx->rc, x + 5, y + 15, 10, RENDER_ALIGN_START, skb_rgba(0,0,0,192), "[%d] @%d %d %c",
						pi, skb_editor_get_paragraph_global_text_offset(ctx->editor, pi),text_count, text_count != content_count ? 'N' : ' ');

					// Draw spans, attribute types and payload
					const skb_text_t* text = skb_editor_get_paragraph_text(ctx->editor, pi);
					const skb_attribute_span_t* spans = skb_text_get_attribute_spans(text);
					const int32_t spans_count = skb_text_get_attribute_spans_count(text);
					for (int32_t si = 0; si < spans_count; si++) {
						const skb_attribute_span_t* span = &spans[si];
						x = debug_render_text(ctx->rc, x + 5, y + 30, 10, RENDER_ALIGN_START, skb_rgba(0,0,0,192), "%c%c%c%c:[%d-%d) ",
							SKB_UNTAG(span->attribute.kind), span->text_range.start, span->text_range.end);
						if (span->payload) {
							uint32_t payload_type = skb_data_blob_get_type(span->payload);
							x = debug_render_text(ctx->rc, x, y + 30, 10, RENDER_ALIGN_START, skb_rgba(128,0,0,128), "<%c%c%c%c>",
								SKB_UNTAG(span->attribute.kind), span->text_range.start, span->text_range.end, SKB_UNTAG(payload_type));
							if (payload_type == SKB_DATA_BLOB_UTF8)
								x = debug_render_text(ctx->rc, x, y + 30, 10, RENDER_ALIGN_START, skb_rgba(128,0,0,192), "\"%s\"",
									skb_data_blob_get_utf8(span->payload, NULL));
						}
					}
				}

				if (ctx->show_run_details) {
					debug_render_layout(ctx->rc, view_offset.x + edit_layout_offset.x, view_offset.y + edit_layout_offset.y, edit_layout);
					debug_render_layout_lines(ctx->rc, view_offset.x + edit_layout_offset.x, view_offset.y + edit_layout_offset.y, edit_layout);
					debug_render_layout_runs(ctx->rc, view_offset.x + edit_layout_offset.x, view_offset.y + edit_layout_offset.y, edit_layout);
					//debug_render_layout_glyphs(ctx->rc, 0.f, edit_layout_y, edit_layout);
				}
			}
		}

		// Caret is generally drawn only when there is no selection.
		if (skb_editor_get_text_range_count(ctx->editor, edit_selection) == 0) {

			// Visual caret
			skb_caret_info_t caret_info = skb_editor_get_caret_info_at(ctx->editor, SKB_CURRENT_SELECTION_END);

			caret_info.x += view_offset.x;
			caret_info.y += view_offset.y;

			float caret_line_width = 2.f;

			float caret_slope = caret_info.slope;
			float caret_top_x = caret_info.x + (caret_info.ascender + caret_line_width*0.5f) * caret_slope;
			float caret_top_y = caret_info.y + caret_info.ascender + caret_line_width*0.5f;
			float caret_bot_x = caret_info.x + (caret_info.descender - caret_line_width*0.5f) * caret_slope;
			float caret_bot_y = caret_info.y + (caret_info.descender - caret_line_width*0.5f);

			debug_render_line(ctx->rc, caret_top_x, caret_top_y, caret_bot_x, caret_bot_y, caret_color, caret_line_width);

			float as = skb_absf(caret_bot_y - caret_top_y) / 5.f;
			float dx = skb_is_rtl(caret_info.direction) ? -as : as;
			float tri_top_x = caret_info.x + caret_info.ascender * caret_slope;
			float tri_top_y = caret_info.y + caret_info.ascender;
			float tri_bot_x = tri_top_x + as * caret_slope;
			float tri_bot_y = tri_top_y + as;
			debug_render_tri(ctx->rc, tri_top_x, tri_top_y,
				tri_top_x + dx, tri_top_y,
				tri_bot_x, tri_bot_y,
				caret_color);
		}
	}

	render_pop_transform(ctx->rc);

	// Draw UI

	// Caret & selection info
	{
		// Caret location
		const int32_t line_idx = skb_editor_get_line_index_at(ctx->editor, SKB_CURRENT_SELECTION_END);
		const int32_t col_idx = skb_editor_get_column_index_at(ctx->editor, SKB_CURRENT_SELECTION_END);

		float cx = 30.f;

		skb_color_t col = skb_rgba(0,0,0,220);

		cx = debug_render_text(ctx->rc, cx,view_height - 50, 13, RENDER_ALIGN_START, col, "Ln %d, Col %d", line_idx+1, col_idx+1);

		// Selection count
		const int32_t selection_count = skb_editor_get_text_range_count(ctx->editor, SKB_CURRENT_SELECTION);
		if (selection_count > 0) {
			cx = debug_render_text(ctx->rc, cx + 20,view_height - 50, 13, RENDER_ALIGN_START, col, "(%d chars)", selection_count);
		}

		const float but_size = 30.f;
		const float but_spacing = 5.f;
		const float spacer = 15.f;

		float tx = 100.f;
		float ty = 50.f;

		{
			// Bold
//			skb_attribute_t bold = skb_attribute_make_reference_by_name(ctx->attribute_collection, "b");
			skb_attribute_t bold = skb_attribute_make_font_weight(SKB_WEIGHT_BOLD);
			bool bold_sel = skb_editor_has_active_attribute(ctx->editor, SKB_CURRENT_SELECTION, bold);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "B", bold_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, bold);
			}
			tx += but_size + but_spacing;
		}

		{
			// Italic
//			skb_attribute_t italic = skb_attribute_make_reference_by_name(ctx->attribute_collection, "i");
			skb_attribute_t italic = skb_attribute_make_font_style(SKB_STYLE_ITALIC);
			bool italic_sel = skb_editor_has_active_attribute(ctx->editor, SKB_CURRENT_SELECTION, italic);

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "I", italic_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, italic);
			}
			tx += but_size + but_spacing;
		}

		{
			// Underline
			skb_attribute_t underline = skb_attribute_make_reference_by_name(ctx->attribute_collection, "u");
			bool underline_sel = skb_editor_has_active_attribute(ctx->editor, SKB_CURRENT_SELECTION, underline);

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "U", underline_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, underline);
			}
			tx += but_size + but_spacing;
		}

		{
			// Inline Code
			skb_attribute_t code = skb_attribute_make_reference_by_name(ctx->attribute_collection, "code");
			bool code_sel = skb_editor_has_active_attribute(ctx->editor, SKB_CURRENT_SELECTION, code);

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "[]", code_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, code);
			}
			tx += but_size + but_spacing;
		}

		{
			// Striketrough
			skb_attribute_t strikethrough = skb_attribute_make_reference_by_name(ctx->attribute_collection, "s");
			bool strikethrough_sel = skb_editor_has_active_attribute(ctx->editor, SKB_CURRENT_SELECTION, strikethrough);

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "S", strikethrough_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, strikethrough);
			}
			tx += but_size + but_spacing;
		}

		{
			// Superscript
			skb_attribute_t superscript = skb_attribute_make_reference_by_name(ctx->attribute_collection, "sup");
			bool superscript_sel = skb_editor_has_active_attribute(ctx->editor, SKB_CURRENT_SELECTION, superscript);

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "^", superscript_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, superscript);
			}
			tx += but_size + but_spacing;
		}

		{
			// Subscript
			skb_attribute_t subscript = skb_attribute_make_reference_by_name(ctx->attribute_collection, "sub");
			bool subscript_sel = skb_editor_has_active_attribute(ctx->editor, SKB_CURRENT_SELECTION, subscript);

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "_", subscript_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, subscript);
			}
			tx += but_size + but_spacing;
		}

		{
			// Link
			skb_attribute_t link = skb_attribute_make_reference_by_name(ctx->attribute_collection, "link");

			bool link_sel = skb_editor_has_attribute(ctx->editor, SKB_CURRENT_SELECTION, link);

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "#", link_sel)) {
				if (!link_sel) {
					skb_data_blob_t* url = skb_data_blob_create_temp(ctx->temp_alloc);
					skb_data_blob_set_utf8(url, "http://ihankiva.com", -1);
					skb_editor_set_attribute_with_payload(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, link, SKB_ATTRIBUTE_SPAN_END_EXCLUSIVE, url);
					skb_data_blob_destroy(url);
				} else {
					// Remove whole link.
					skb_text_range_t link_range = skb_editor_get_attribute_text_range(ctx->editor, SKB_CURRENT_SELECTION, link);
					skb_editor_clear_attribute(ctx->editor, ctx->temp_alloc, link_range, link);

				}
			}
			tx += but_size + but_spacing;
		}

		tx += spacer;

		{
			// H1
			skb_attribute_t h1 = skb_attribute_make_reference_by_name(ctx->attribute_collection, "H1");
			bool h1_sel = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, h1);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "H1", h1_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, h1);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// H2
			skb_attribute_t h2 = skb_attribute_make_reference_by_name(ctx->attribute_collection, "H2");
			bool h2_sel = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, h2);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "H2", h2_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, h2);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// Body
			skb_attribute_t body = skb_attribute_make_reference_by_name(ctx->attribute_collection, "BODY");
			bool body_sel = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, body);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "Body", body_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, body);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// List
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "LI");
			bool list_sel = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, list);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "LI", list_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, list);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// Ordered List
			skb_attribute_t ordered_list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "OL");
			bool ordered_list_sel = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, ordered_list);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "OL", ordered_list_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, ordered_list);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// Quoteblock
			skb_attribute_t quoteblock = skb_attribute_make_reference_by_name(ctx->attribute_collection, "QUOTE");
			bool quoteblock_sel = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, quoteblock);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "\"\"", quoteblock_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, quoteblock);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// Codeblock
			skb_attribute_t codeblock = skb_attribute_make_reference_by_name(ctx->attribute_collection, "CODE");
			bool codeblock_sel = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, codeblock);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "{}", codeblock_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, codeblock);
			}
			tx += but_size*2 + but_spacing;
		}

		tx += spacer;

		{
			// Indent+
			skb_attribute_t indent_level_delta = skb_attribute_make_indent_level(1);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, ">|", false)) {
				skb_editor_set_paragraph_attribute_delta(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, indent_level_delta);
			}
			tx += but_size + but_spacing;
		}

		{
			// Indent-
			skb_attribute_t indent_level_delta = skb_attribute_make_indent_level(-1);
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "<|", false)) {
				skb_editor_set_paragraph_attribute_delta(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, indent_level_delta);
			}
			tx += but_size + but_spacing;
		}

		tx += spacer;

		// Align
		{
			skb_attribute_t ltr = skb_attribute_make_reference_by_name(ctx->attribute_collection, "ltr");
			skb_attribute_t rtl = skb_attribute_make_reference_by_name(ctx->attribute_collection, "rtl");

			skb_attribute_t align_start = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-start");
			skb_attribute_t align_center = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-center");
			skb_attribute_t align_end = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-end");

			bool is_ltr = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, ltr);
			bool is_rtl = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, rtl);
			if (!is_rtl)
				is_ltr = true;

			bool is_align_start = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, align_start);
			bool is_align_center = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, align_center);
			bool is_align_end = skb_editor_has_paragraph_attribute(ctx->editor, SKB_CURRENT_SELECTION, align_end);
			if (!is_align_center && !is_align_end)
				is_align_start = true;

			// Align
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "S", is_rtl ? is_align_end : is_align_start))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, is_rtl ? align_end : align_start);
			tx += but_size + but_spacing;

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "C", is_align_center))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, align_center);
			tx += but_size + but_spacing;

			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "E", is_rtl ? is_align_start : is_align_end))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, is_rtl ? align_start : align_end);
			tx += but_size + but_spacing;

			tx += spacer;

			// Text direction
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "L>", is_ltr))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, ltr);
			tx += but_size + but_spacing;
			if (ui_button(&ctx->ui, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "<R", is_rtl))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, SKB_CURRENT_SELECTION, rtl);
			tx += but_size + but_spacing;
		}
	}


	// Draw atlas
	render_update_atlas(ctx->rc);
//	debug_render_atlas_overlay(ctx->rc, 20.f, 50.f, ctx->atlas_scale, 1);

	// Draw info
	debug_render_text(ctx->rc, (float)view_width - 20.f, (float)view_height - 15.f, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,255),
		"F8: Caret details %s   F9: Run details %s",
		ctx->show_caret_details ? "ON" : "OFF", ctx->show_run_details ? "ON" : "OFF");

	ui_frame_end(&ctx->ui);

}
