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

typedef struct ui_context_t {
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

	bool allow_char;
	view_t view;
	bool drag_view;
	bool drag_text;

	skb_vec2_t mouse_pos;
	bool mouse_pressed;

	ui_context_t ui;

	GLFWcursor* hand_cursor;

} notes_context_t;



static void update_ime_rect(notes_context_t* ctx)
{
	skb_text_selection_t edit_selection = skb_editor_get_current_selection(ctx->editor);
	skb_visual_caret_t caret_pos = skb_editor_get_visual_caret(ctx->editor, edit_selection.end_pos);

	skb_rect2_t caret_rect = {
		.x = caret_pos.x - caret_pos.descender * caret_pos.slope,
		.y = caret_pos.y + caret_pos.ascender,
		.width = (-caret_pos.ascender + caret_pos.descender) * caret_pos.slope,
		.height = -caret_pos.ascender + caret_pos.descender,
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

	ctx->font_collection = skb_font_collection_create();
	assert(ctx->font_collection);

	const skb_font_create_params_t fake_italic_params = {
		.slant = SKB_DEFAULT_SLANT
	};

	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Italic.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Bold.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_PARAMS_OR_FAIL("data/IBMPlexSans-Bold.ttf", SKB_FONT_FAMILY_DEFAULT, &fake_italic_params);

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

	ctx->hand_cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

	ctx->attribute_collection = skb_attribute_collection_create();
	assert(ctx->attribute_collection);

	// Create paragraph styles.

	const skb_color_t header_color = skb_rgba(64,64,64,255);
	const skb_color_t body_color = skb_rgba(16,16,16,255);

	{
		const skb_attribute_t h1_attributes[] = {
			skb_attribute_make_font_size(32.f),
			skb_attribute_make_fill(header_color),
			skb_attribute_make_vertical_padding(20,5),
		};

		const skb_attribute_t h2_attributes[] = {
			skb_attribute_make_font_size(22.f),
			skb_attribute_make_fill(header_color),
			skb_attribute_make_vertical_padding(10,5),
		};

		const skb_attribute_t body_attributes[] = {
			skb_attribute_make_font_size(16.f),
			skb_attribute_make_fill(body_color),
			skb_attribute_make_line_height(SKB_LINE_HEIGHT_METRICS_RELATIVE, 1.3f),
			skb_attribute_make_vertical_padding(5,5),
		};

		const skb_attribute_t list_attributes[] = {
			skb_attribute_make_font_size(16.f),
			skb_attribute_make_fill(body_color),
			skb_attribute_make_vertical_padding(5,5),
			skb_attribute_make_list_marker(SKB_LIST_MARKER_CODEPOINT, 32, 5, 0x2022),
		};

		const skb_attribute_t underline_attributes[] = {
			skb_attribute_make_decoration(SKB_DECORATION_UNDERLINE, SKB_DECORATION_STYLE_SOLID, 1.f, 1.f, body_color),
		};

		const skb_attribute_t strikethrough_attributes[] = {
			skb_attribute_make_decoration(SKB_DECORATION_THROUGHLINE, SKB_DECORATION_STYLE_SOLID, 1.5f, 0.f, body_color),
		};

		const skb_attribute_t italic_attributes[] = {
			skb_attribute_make_font_style(SKB_STYLE_ITALIC),
		};

		const skb_attribute_t bold_attributes[] = {
			skb_attribute_make_font_weight(SKB_WEIGHT_BOLD),
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
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "LIST", "paragraph", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(list_attributes));

		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "align-start", "align", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(align_start));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "align-center", "align", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(align_center));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "align-end", "align", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(align_end));

		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "ltr", "text-dir", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(dir_ltr));
		skb_attribute_collection_add_set_with_group(ctx->attribute_collection, "rtl", "text-dir", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(dir_rtl));

		skb_attribute_collection_add_set(ctx->attribute_collection, "s", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(strikethrough_attributes));
		skb_attribute_collection_add_set(ctx->attribute_collection, "u", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(underline_attributes));
		skb_attribute_collection_add_set(ctx->attribute_collection, "i", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(italic_attributes));
		skb_attribute_collection_add_set(ctx->attribute_collection, "b", SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(bold_attributes));
	}

	const skb_attribute_t layout_attributes[] = {
		skb_attribute_make_text_wrap(SKB_WRAP_WORD_CHAR),
		skb_attribute_make_tab_stop_increment(16.f * 2.f),
		skb_attribute_make_indent_increment(32.f, 0.f),
	};

	skb_attribute_set_t body = skb_attribute_set_make_reference_by_name(ctx->attribute_collection, "BODY");

	const skb_attribute_t composition_attributes[] = {
		skb_attribute_make_fill(skb_rgba(0,128,192,255)),
		skb_attribute_make_decoration(SKB_DECORATION_UNDERLINE, SKB_DECORATION_STYLE_DOTTED, 0.f, 1.f, skb_rgba(0,128,192,255)),
	};

	skb_editor_params_t edit_params = {
		.font_collection = ctx->font_collection,
		.attribute_collection = ctx->attribute_collection,
		.editor_width = 1200.f,
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
	skb_editor_destroy(ctx->editor);
	skb_temp_alloc_destroy(ctx->temp_alloc);

	glfwDestroyCursor(ctx->hand_cursor);

	SKB_ZERO_STRUCT(ctx);

	ime_cancel();
	ime_set_handler(NULL, NULL);

	skb_free(ctx);
}


static bool notes__selection_has_paragraph_attribute(const skb_editor_t* editor, skb_attribute_t attribute)
{
	skb_text_selection_t edit_selection = skb_editor_get_current_selection(editor);
	skb_range_t paragraph_range = skb_editor_get_selection_paragraphs_range(editor, edit_selection);
	int32_t match_count = 0;
	for (int32_t i = paragraph_range.start; i < paragraph_range.end; i++) {
		skb_attribute_set_t paragraph_attributes = skb_editor_get_paragraph_attributes(editor, i);
		for (int32_t j = 0; j < paragraph_attributes.attributes_count; j++) {
			if (paragraph_attributes.attributes[j].kind == attribute.kind && memcmp(&paragraph_attributes.attributes[j], &attribute, sizeof(skb_attribute_t)) == 0) {
				match_count++;
				break;
			}
		}
	}
	int32_t paragraph_count = paragraph_range.end - paragraph_range.start;
	return paragraph_count == match_count;
}

static bool nodes__match_prefix_at_paragraph_start(const skb_editor_t* editor, const skb_paragraph_position_t paragraph_pos, const char* value_utf8, skb_text_selection_t* prefix_selection)
{
	const skb_text_t* paragraph_text = skb_editor_get_paragraph_text(editor, paragraph_pos.paragraph_idx);
	if (!paragraph_text)
		return false;

	const int32_t value_utf8_count = (int32_t)strlen(value_utf8);
	uint32_t value_utf32[32];
	int32_t value_utf32_count = skb_utf8_to_utf32(value_utf8, value_utf8_count, value_utf32, SKB_COUNTOF(value_utf32));

	skb_range_t range = skb_text_find_reverse_utf32(paragraph_text, (skb_range_t){ .start = 0, .end = paragraph_pos.text_offset }, value_utf32, value_utf32_count);
	if (skb_range_is_empty(range))
		return false;

	// The match must be at the start of the paragraph.
	if (range.start != 0)
		return false;
	// The match bust be just before the queried position.
	if (range.end != paragraph_pos.text_offset)
		return false;

	const int32_t prefix_global_start_offset = skb_editor_get_paragraph_global_text_offset(editor, paragraph_pos.paragraph_idx);
	prefix_selection->start_pos = (skb_text_position_t) { .offset = prefix_global_start_offset + range.start };
	prefix_selection->end_pos = (skb_text_position_t) { .offset = prefix_global_start_offset + range.end };

	return true;
}

static void notes__handle_space(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_attribute_collection_t* attribute_collection, uint32_t edit_mods)
{
	skb_attribute_t h1 = skb_attribute_make_reference_by_name(attribute_collection, "H1");
	skb_attribute_t h2 = skb_attribute_make_reference_by_name(attribute_collection, "H2");
	skb_attribute_t body = skb_attribute_make_reference_by_name(attribute_collection, "BODY");
	skb_attribute_t list = skb_attribute_make_reference_by_name(attribute_collection, "LIST");

	skb_text_selection_t selection = skb_editor_get_current_selection(editor);
	const int32_t selection_count = skb_editor_get_selection_count(editor, selection);
	const skb_paragraph_position_t paragraph_pos = skb_editor_text_position_to_paragraph_position(editor, selection.end_pos);

	if (notes__selection_has_paragraph_attribute(editor, body)) {
		if (selection_count == 0) {
			skb_text_selection_t prefix_selection;
			// Body -> H2
			if (nodes__match_prefix_at_paragraph_start(editor, paragraph_pos, "##", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				skb_editor_select(editor, prefix_selection);
				skb_editor_cut(editor, temp_alloc);
				selection = skb_editor_get_current_selection(editor);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, selection, h2);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
			// Body -> H1
			if (nodes__match_prefix_at_paragraph_start(editor, paragraph_pos, "#", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				skb_editor_select(editor, prefix_selection);
				skb_editor_cut(editor, temp_alloc);
				selection = skb_editor_get_current_selection(editor);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, selection, h1);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
			// Body -> List
			if (nodes__match_prefix_at_paragraph_start(editor, paragraph_pos, "-", &prefix_selection)) {
				int32_t transaction_id = skb_editor_undo_transaction_begin(editor);
				skb_editor_select(editor, prefix_selection);
				skb_editor_cut(editor, temp_alloc);
				selection = skb_editor_get_current_selection(editor);
				skb_editor_set_paragraph_attribute(editor, temp_alloc, selection, list);
				skb_editor_undo_transaction_end(editor, transaction_id);
				return;
			}
		}
	}

	// Insert space
	skb_editor_insert_codepoint(editor, temp_alloc, ' ');
}

static void notes__handle_tab(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_attribute_collection_t* attribute_collection, uint32_t edit_mods)
{
	skb_attribute_t list = skb_attribute_make_reference_by_name(attribute_collection, "LIST");
	skb_attribute_t body = skb_attribute_make_reference_by_name(attribute_collection, "BODY");

	skb_text_selection_t selection = skb_editor_get_current_selection(editor);
	const int32_t selection_count = skb_editor_get_selection_count(editor, selection);
	const skb_paragraph_position_t paragraph_pos = skb_editor_text_position_to_paragraph_position(editor, selection.end_pos);

	// Indent && Outdent
	if (notes__selection_has_paragraph_attribute(editor, list)) {
		skb_attribute_t indent_level_delta = skb_attribute_make_indent_level((edit_mods & SKB_MOD_SHIFT) ? -1 : 1);
		skb_editor_apply_paragraph_attribute_delta(editor, temp_alloc, selection, indent_level_delta);
		return;
	}
	if (notes__selection_has_paragraph_attribute(editor, body)) {
		if (selection_count == 0 && paragraph_pos.text_offset == 0) {
			skb_attribute_t indent_level_delta = skb_attribute_make_indent_level((edit_mods & SKB_MOD_SHIFT) ? -1 : 1);
			skb_editor_apply_paragraph_attribute_delta(editor, temp_alloc, selection, indent_level_delta);
			return;
		}
	}

	// Insert tab
	skb_editor_insert_codepoint(editor, temp_alloc, '\t');
}

static void notes__handle_backspace(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_attribute_collection_t* attribute_collection, uint32_t edit_mods)
{
	skb_attribute_t list = skb_attribute_make_reference_by_name(attribute_collection, "LIST");
	skb_attribute_t body = skb_attribute_make_reference_by_name(attribute_collection, "BODY");

	skb_text_selection_t selection = skb_editor_get_current_selection(editor);
	const int32_t selection_count = skb_editor_get_selection_count(editor, selection);
	const skb_paragraph_position_t paragraph_pos = skb_editor_text_position_to_paragraph_position(editor, selection.end_pos);

	// List outdent
	if (notes__selection_has_paragraph_attribute(editor, list)) {
		if (selection_count == 0 && paragraph_pos.text_offset == 0) {
			skb_attribute_set_t paragraph_attributes = skb_editor_get_paragraph_attributes(editor, paragraph_pos.paragraph_idx);
			const int32_t indent_level = skb_attributes_get_indent_level(paragraph_attributes, attribute_collection);
			if (indent_level == 0) {
				// Convert to body when no indent left.
				skb_editor_set_paragraph_attribute(editor, temp_alloc, selection, body);
			} else {
				// Outdent
				skb_attribute_t indent_level_delta = skb_attribute_make_indent_level(-1);
				skb_editor_apply_paragraph_attribute_delta(editor, temp_alloc, selection, indent_level_delta);
			}
			return;
		}
	}

	// Body outdent
	if (notes__selection_has_paragraph_attribute(editor, body)) {
		if (selection_count == 0 && paragraph_pos.text_offset == 0) {
			skb_attribute_set_t paragraph_attributes = skb_editor_get_paragraph_attributes(editor, paragraph_pos.paragraph_idx);
			const int32_t indent_level = skb_attributes_get_indent_level(paragraph_attributes, attribute_collection);
			if (indent_level > 0) {
				// Outdent
				skb_attribute_t indent_level_delta = skb_attribute_make_indent_level(-1);
				skb_editor_apply_paragraph_attribute_delta(editor, temp_alloc, selection, indent_level_delta);
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
	skb_attribute_t list = skb_attribute_make_reference_by_name(attribute_collection, "LIST");

	skb_text_selection_t selection = skb_editor_get_current_selection(editor);
	const int32_t selection_count = skb_editor_get_selection_count(editor, selection);
	const skb_paragraph_position_t paragraph_pos = skb_editor_text_position_to_paragraph_position(editor, selection.end_pos);
	int32_t paragraph_text_count = skb_editor_get_paragraph_text_count(editor, paragraph_pos.paragraph_idx);

	// List undent
	if (notes__selection_has_paragraph_attribute(editor, list)) {
		if (selection_count == 0) {
			if (paragraph_text_count <= 1) { // Empty paragraph
				skb_editor_set_paragraph_attribute(editor, temp_alloc, selection, body);
				return;
			}
		}
	}

	// H1,H2 -> body
	if (notes__selection_has_paragraph_attribute(editor, h1) || notes__selection_has_paragraph_attribute(editor, h2)) {
		if (selection_count == 0) {
			if (paragraph_pos.text_offset >= (paragraph_text_count - 1)) {
				skb_editor_insert_paragraph(editor, temp_alloc, body);
				return;
			}
		}
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
			skb_editor_paste_utf8(ctx->editor, ctx->temp_alloc, clipboard_text, -1);
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

		if (key == GLFW_KEY_1 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
			// H1
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			skb_attribute_t h1 = skb_attribute_make_reference_by_name(ctx->attribute_collection, "H1");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, selection, h1);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_2 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
			// H2
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			skb_attribute_t h2 = skb_attribute_make_reference_by_name(ctx->attribute_collection, "H2");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, selection, h2);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_0 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
			// Body
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			skb_attribute_t body = skb_attribute_make_reference_by_name(ctx->attribute_collection, "BODY");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, selection, body);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_8 && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT)) {
			// List
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "LIST");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, selection, list);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_L && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-start");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, selection, list);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_T && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-center");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, selection, list);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_R && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "align-end");
			skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, selection, list);
			ctx->allow_char = false;
		}

		if (key == GLFW_KEY_A && (mods & GLFW_MOD_CONTROL)) {
			// Select all
			skb_editor_select_all(ctx->editor);
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_B && (mods & GLFW_MOD_CONTROL)) {
			// Bold
			skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, skb_attribute_make_font_weight(SKB_WEIGHT_BOLD));
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_I && (mods & GLFW_MOD_CONTROL)) {
			// Italic
			skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, skb_attribute_make_font_style(SKB_STYLE_ITALIC));
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_U && (mods & GLFW_MOD_CONTROL)) {
			// Underline
			skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, skb_attribute_make_reference_by_name(ctx->attribute_collection, "u"));
			ctx->allow_char = false;
		}
		if (key == GLFW_KEY_ESCAPE) {
			// Clear selection
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			if (skb_editor_get_selection_text_utf32_count(ctx->editor, selection) > 0)
				skb_editor_select_none(ctx->editor);
			else
				glfwSetWindowShouldClose(window, GL_TRUE);
		}
		if (key == GLFW_KEY_X) {
			if ((mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT)) {
				// Strike through
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, skb_attribute_make_reference_by_name(ctx->attribute_collection, "s"));
				ctx->allow_char = false;
			} else if (mods & GLFW_MOD_CONTROL) {
				// Cut
				skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
				int32_t text_len = skb_editor_get_selection_text_utf8(ctx->editor, selection, NULL, -1);
				char* text = SKB_TEMP_ALLOC(ctx->temp_alloc, char, text_len + 1);
				text_len = skb_editor_get_selection_text_utf8(ctx->editor, selection, text, text_len);
				text[text_len] = '\0';
				glfwSetClipboardString(window, text);
				SKB_TEMP_FREE(ctx->temp_alloc, text);
				skb_editor_cut(ctx->editor, ctx->temp_alloc);
				ctx->allow_char = false;
			}
		}
		if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
			// Copy
			skb_text_selection_t selection = skb_editor_get_current_selection(ctx->editor);
			int32_t text_len = skb_editor_get_selection_text_utf8_count(ctx->editor, selection);
			char* text = SKB_TEMP_ALLOC(ctx->temp_alloc, char, text_len + 1);
			text_len = skb_editor_get_selection_text_utf8(ctx->editor, selection, text, text_len);
			text[text_len] = '\0';
			glfwSetClipboardString(window, text);
			SKB_TEMP_FREE(ctx->temp_alloc, text);
			ctx->allow_char = false;
		}

		update_ime_rect(ctx);

/*		if (key == GLFW_KEY_F6) {
			ctx->show_run_details = !ctx->show_run_details;
		}
		if (key == GLFW_KEY_F7) {
			ctx->show_baseline_details = !ctx->show_baseline_details;
		}
		if (key == GLFW_KEY_F8) {
			ctx->show_caret_details = !ctx->show_caret_details;
		}
		if (key == GLFW_KEY_F9) {
			ctx->show_glyph_details = !ctx->show_glyph_details;
		}
		if (key == GLFW_KEY_F10) {
			ctx->atlas_scale += 0.25f;
			if (ctx->atlas_scale > 1.01f)
				ctx->atlas_scale = 0.0f;
		}*/
	}
}

void notes_on_char(void* ctx_ptr, unsigned int codepoint)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (ctx->allow_char)
		skb_editor_insert_codepoint(ctx->editor, ctx->temp_alloc, codepoint);
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


static void ui_frame_begin(ui_context_t* ui)
{
	ui->id_gen = 1;
	ui->hover = ui->next_hover;
	ui->next_hover = 0;
	ui->went_active = 0;
}

static void ui_frame_end(ui_context_t* ui)
{
	ui->mouse_pressed = false;
	ui->mouse_released = false;
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

	// process down
	if (ui->active == 0) {
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

static bool ui_button(notes_context_t* ctx, skb_rect2_t rect, const char* text, bool selected)
{
	int32_t id = ui_make_id(&ctx->ui);
	bool over = skb_rect2_pt_inside(rect, ctx->ui.mouse_pos);
	bool res = ui_button_logic(&ctx->ui, id, over);

	if (res) {
		skb_debug_log("click!!\n");
	}

	skb_color_t bg_col = skb_rgba(255,255,255,128);
	skb_color_t text_col = skb_rgba(0,0,0,220);
	if (selected) {
		bg_col = skb_rgba(0,192,220,192);
		text_col = skb_rgba(255,255,255,220);
	}
	if (ctx->ui.active == id) {
		bg_col.a = 255;
		text_col.a = 255;
	}
	if (ctx->ui.hover == id)
		bg_col.a = 192;

	debug_render_filled_rect(ctx->rc, rect.x, rect.y, rect.width, rect.height, bg_col);
	debug_render_text(ctx->rc, rect.x + rect.width * 0.5f+1,rect.y + rect.height*0.5f + 6.f, 17, RENDER_ALIGN_CENTER, text_col, text);

	return res;
}

static bool notes__selection_has_attribute(const skb_editor_t* editor, skb_attribute_t attribute)
{
	skb_text_selection_t edit_selection = skb_editor_get_current_selection(editor);
	const int32_t selection_count = skb_editor_get_selection_count(editor, edit_selection);

	if (selection_count > 0)
		return skb_editor_get_attribute_count(editor, edit_selection, attribute.kind) == selection_count;

	const int32_t active_attributes_count = skb_editor_get_active_attributes_count(editor);
	const skb_attribute_t* active_attributes = skb_editor_get_active_attributes(editor);

	for (int32_t i = 0; i < active_attributes_count; i++) {
		if (active_attributes[i].kind == attribute.kind && memcmp(&active_attributes[i], &attribute, sizeof(skb_attribute_t)) == 0)
			return true;
	}

	return false;
}

void notes_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height)
{
	notes_context_t* ctx = ctx_ptr;
	assert(ctx);

	ui_frame_begin(&ctx->ui);

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

		skb_rect2_t editor_bounds = {
			.x = 0.f,
			.y = 0.f,
			.width = editor_params->editor_width,
			.height = 0.f,
		};
		const int32_t paragraph_count = skb_editor_get_paragraph_count(ctx->editor);
		if (paragraph_count > 0) {
			const float offset_y = skb_editor_get_paragraph_offset_y(ctx->editor, paragraph_count - 1);
			const float advance_y = skb_editor_get_paragraph_advance_y(ctx->editor, paragraph_count - 1);
			editor_bounds.height = offset_y + advance_y;
		}

		debug_render_stroked_rect(ctx->rc, editor_bounds.x - 5, editor_bounds.y - 5, editor_bounds.width + 10, editor_bounds.height + 10, skb_rgba(0,0,0,128), 1.f);

		skb_text_selection_t edit_selection = skb_editor_get_current_selection(ctx->editor);
		if (skb_editor_get_selection_count(ctx->editor, edit_selection) > 0) {
			draw_selection_context_t sel_ctx = { .x = 0, .y = 0, .color = sel_color, .renderer = ctx->rc };
			skb_editor_get_selection_bounds(ctx->editor, edit_selection, draw_selection_rect, &sel_ctx);
		}

		for (int32_t pi = 0; pi < skb_editor_get_paragraph_count(ctx->editor); pi++) {
			const skb_layout_t* edit_layout = skb_editor_get_paragraph_layout(ctx->editor, pi);
			const float edit_layout_y = skb_editor_get_paragraph_offset_y(ctx->editor, pi);
			const skb_layout_line_t* lines = skb_layout_get_lines(edit_layout);
			const int32_t lines_count = skb_layout_get_lines_count(edit_layout);
			const skb_layout_run_t* layout_runs = skb_layout_get_layout_runs(edit_layout);
			const skb_glyph_t* glyphs = skb_layout_get_glyphs(edit_layout);
			const skb_layout_params_t* layout_params = skb_layout_get_params(edit_layout);
			const int32_t decorations_count = skb_layout_get_decorations_count(edit_layout);
			const skb_decoration_t* decorations = skb_layout_get_decorations(edit_layout);

			// Draw underlines
			for (int32_t i = 0; i < decorations_count; i++) {
				const skb_decoration_t* decoration = &decorations[i];
				if (decoration->position != SKB_DECORATION_THROUGHLINE) {
					render_draw_decoration(ctx->rc, decoration->offset_x, edit_layout_y + decoration->offset_y,
						decoration->style, decoration->position, decoration->length, decoration->pattern_offset, decoration->thickness,
						decoration->color, SKB_RASTERIZE_ALPHA_SDF);
				}
			}

			for (int li = 0; li < lines_count; li++) {
				const skb_layout_line_t* line = &lines[li];
				for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
					const skb_layout_run_t* layout_run = &layout_runs[ri];
					const skb_attribute_fill_t attr_fill = skb_attributes_get_fill(layout_run->attributes, layout_params->attribute_collection);
					const float font_size = layout_run->font_size;
					if (layout_run->type == SKB_CONTENT_RUN_UTF8 || layout_run->type == SKB_CONTENT_RUN_UTF32) {
						for (int32_t gi = layout_run->glyph_range.start; gi < layout_run->glyph_range.end; gi++) {
							const skb_glyph_t* glyph = &glyphs[gi];
							render_draw_glyph(ctx->rc, glyph->offset_x, edit_layout_y + glyph->offset_y,
								layout_params->font_collection, layout_run->font_handle, glyph->gid, font_size,
								attr_fill.color, SKB_RASTERIZE_ALPHA_SDF);
						}
					}
				}
			}

			// Draw through lines
			for (int32_t i = 0; i < decorations_count; i++) {
				const skb_decoration_t* decoration = &decorations[i];
				if (decoration->position == SKB_DECORATION_THROUGHLINE) {
					render_draw_decoration(ctx->rc, decoration->offset_x, edit_layout_y + decoration->offset_y,
						decoration->style, decoration->position, decoration->length, decoration->pattern_offset, decoration->thickness,
						decoration->color, SKB_RASTERIZE_ALPHA_SDF);
				}
			}

		}

		// Caret is generally drawn only when there is no selection.
		if (skb_editor_get_selection_count(ctx->editor, edit_selection) == 0) {

			// Visual caret
			skb_visual_caret_t caret_pos = skb_editor_get_visual_caret(ctx->editor, edit_selection.end_pos);

			float caret_line_width = 2.f;

			float caret_slope = caret_pos.slope;
			float caret_top_x = caret_pos.x + (caret_pos.ascender + caret_line_width*0.5f) * caret_slope;
			float caret_top_y = caret_pos.y + caret_pos.ascender + caret_line_width*0.5f;
			float caret_bot_x = caret_pos.x + (caret_pos.descender - caret_line_width*0.5f) * caret_slope;
			float caret_bot_y = caret_pos.y + (caret_pos.descender - caret_line_width*0.5f);

			debug_render_line(ctx->rc, caret_top_x, caret_top_y, caret_bot_x, caret_bot_y, caret_color, caret_line_width);

			float as = skb_absf(caret_bot_y - caret_top_y) / 10.f;
			float dx = skb_is_rtl(caret_pos.direction) ? -as : as;
			float tri_top_x = caret_pos.x + caret_pos.ascender * caret_slope;
			float tri_top_y = caret_pos.y + caret_pos.ascender;
			float tri_bot_x = tri_top_x - as * caret_slope;
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
		skb_text_selection_t edit_selection = skb_editor_get_current_selection(ctx->editor);

		// Caret location
		const int32_t insert_idx = skb_editor_text_position_to_text_offset(ctx->editor, edit_selection.end_pos);
		skb_text_position_t insert_pos = {
			.offset = insert_idx,
			.affinity = SKB_AFFINITY_TRAILING,
		};
		const int32_t line_idx = skb_editor_get_line_index_at(ctx->editor, insert_pos);
		const int32_t col_idx = skb_editor_get_column_index_at(ctx->editor, insert_pos);

		float cx = 30.f;

		skb_color_t col = skb_rgba(0,0,0,220);

		cx = debug_render_text(ctx->rc, cx,view_height - 50, 13, RENDER_ALIGN_START, col, "Ln %d, Col %d", line_idx+1, col_idx+1);

		// Selection count
		const int32_t selection_count = skb_editor_get_selection_count(ctx->editor, edit_selection);
		if (selection_count > 0) {
			cx = debug_render_text(ctx->rc, cx + 20,view_height - 50, 13, RENDER_ALIGN_START, col, "(%d chars)", selection_count);
		}

		const float but_size = 30.f;
		const float but_spacing = 5.f;
		const float spacer = 15.f;

		float tx = 100.f;
		float ty = 50.f;

		const int32_t active_attributes_count = skb_editor_get_active_attributes_count(ctx->editor);
		const skb_attribute_t* active_attributes = skb_editor_get_active_attributes(ctx->editor);

		{
			// Bold
//			skb_attribute_t bold = skb_attribute_make_reference_by_name(ctx->attribute_collection, "b");
			skb_attribute_t bold = skb_attribute_make_font_weight(SKB_WEIGHT_BOLD);
			bool bold_sel = notes__selection_has_attribute(ctx->editor, bold);
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "B", bold_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, bold);
			}
			tx += but_size + but_spacing;
		}

		{
			// Italic
//			skb_attribute_t italic = skb_attribute_make_reference_by_name(ctx->attribute_collection, "i");
			skb_attribute_t italic = skb_attribute_make_font_style(SKB_STYLE_ITALIC);
			bool italic_sel = notes__selection_has_attribute(ctx->editor, italic);

			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "I", italic_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, italic);
			}
			tx += but_size + but_spacing;
		}

		{
			// Underline
			skb_attribute_t underline = skb_attribute_make_reference_by_name(ctx->attribute_collection, "u");
			bool underline_sel = notes__selection_has_attribute(ctx->editor, underline);

			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "U", underline_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, underline);
			}
			tx += but_size + but_spacing;
		}

		{
			// Striketrough
			skb_attribute_t strikethrough = skb_attribute_make_reference_by_name(ctx->attribute_collection, "s");
			bool strikethrough_sel = notes__selection_has_attribute(ctx->editor, strikethrough);

			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "S", strikethrough_sel)) {
				skb_editor_toggle_attribute(ctx->editor, ctx->temp_alloc, strikethrough);
			}
			tx += but_size + but_spacing;
		}

		tx += spacer;

		{
			// H1
			skb_attribute_t h1 = skb_attribute_make_reference_by_name(ctx->attribute_collection, "H1");
			bool h1_sel = notes__selection_has_paragraph_attribute(ctx->editor, h1);
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "H1", h1_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, h1);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// H2
			skb_attribute_t h2 = skb_attribute_make_reference_by_name(ctx->attribute_collection, "H2");
			bool h2_sel = notes__selection_has_paragraph_attribute(ctx->editor, h2);
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "H2", h2_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, h2);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// Body
			skb_attribute_t body = skb_attribute_make_reference_by_name(ctx->attribute_collection, "BODY");
			bool body_sel = notes__selection_has_paragraph_attribute(ctx->editor, body);
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "Body", body_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, body);
			}
			tx += but_size*2 + but_spacing;
		}

		{
			// List
			skb_attribute_t list = skb_attribute_make_reference_by_name(ctx->attribute_collection, "LIST");
			bool list_sel = notes__selection_has_paragraph_attribute(ctx->editor, list);
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size*2, .height = but_size }, "List", list_sel)) {
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, list);
			}
			tx += but_size*2 + but_spacing;
		}

		tx += spacer;

		{
			// Indent+
			skb_attribute_t indent_level_delta = skb_attribute_make_indent_level(1);
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, ">|", false)) {
				skb_editor_apply_paragraph_attribute_delta(ctx->editor, ctx->temp_alloc, edit_selection, indent_level_delta);
			}
			tx += but_size + but_spacing;
		}

		{
			// Indent-
			skb_attribute_t indent_level_delta = skb_attribute_make_indent_level(-1);
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "<|", false)) {
				skb_editor_apply_paragraph_attribute_delta(ctx->editor, ctx->temp_alloc, edit_selection, indent_level_delta);
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

			bool is_ltr = notes__selection_has_paragraph_attribute(ctx->editor, ltr);
			bool is_rtl = notes__selection_has_paragraph_attribute(ctx->editor, rtl);
			if (!is_rtl)
				is_ltr = true;

			bool is_align_start = notes__selection_has_paragraph_attribute(ctx->editor, align_start);
			bool is_align_center = notes__selection_has_paragraph_attribute(ctx->editor, align_center);
			bool is_align_end = notes__selection_has_paragraph_attribute(ctx->editor, align_end);
			if (!is_align_center && !is_align_end)
				is_align_start = true;

			// Align
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "S", is_rtl ? is_align_end : is_align_start))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, is_rtl ? align_end : align_start);
			tx += but_size + but_spacing;

			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "C", is_align_center))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, align_center);
			tx += but_size + but_spacing;

			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "E", is_rtl ? is_align_start : is_align_end))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, is_rtl ? align_start : align_end);
			tx += but_size + but_spacing;

			tx += spacer;

			// Text direction
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "L>", is_ltr))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, ltr);
			tx += but_size + but_spacing;
			if (ui_button(ctx, (skb_rect2_t){ .x = tx, .y = ty, .width = but_size, .height = but_size }, "<R", is_rtl))
				skb_editor_set_paragraph_attribute(ctx->editor, ctx->temp_alloc, edit_selection, rtl);
			tx += but_size + but_spacing;
		}


		/*
			- Color
			- Bold
			- Italic
			- Font
			- Size
			- Strike

			- Link
			- List

			- Align (left/center/right)

		*/


		// Active attributes
/*		const int32_t active_attributes_count = skb_editor_get_active_attributes_count(ctx->editor);
		const skb_attribute_t* active_attributes = skb_editor_get_active_attributes(ctx->editor);
		cx = debug_render_text(ctx->rc, cx + 20,layout_height + 30, 13, RENDER_ALIGN_START, ink_color, "Active attributes (%d):", active_attributes_count);
		for (int32_t i = 0; i < active_attributes_count; i++)
			cx = debug_render_text(ctx->rc, cx + 5,layout_height + 30, 13, RENDER_ALIGN_START, ink_color, "%c%c%c%c", SKB_UNTAG(active_attributes[i].kind));*/
	}


	// Draw atlas
	render_update_atlas(ctx->rc);
//	debug_render_atlas_overlay(ctx->rc, 20.f, 50.f, ctx->atlas_scale, 1);

	// Draw info
/*	debug_render_text(ctx->rc, (float)view_width - 20.f, (float)view_height - 15.f, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,255),
		"F9: Glyph details %s   F10: Atlas %.1f%%",
		ctx->show_glyph_bounds ? "ON" : "OFF", ctx->atlas_scale * 100.f);*/

	ui_frame_end(&ctx->ui);

}
