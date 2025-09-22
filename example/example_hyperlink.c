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
#include "render.h"
#include "utils.h"

#include "skb_common.h"
#include "skb_font_collection.h"
#include "skb_layout.h"
#include "skb_layout_cache.h"
#include "skb_rasterizer.h"
#include "skb_image_atlas.h"


typedef struct hyperlink_context_t {
	example_t base;

	skb_font_collection_t* font_collection;
	skb_icon_collection_t* icon_collection;
	skb_temp_alloc_t* temp_alloc;
	GLFWwindow* window;
	render_context_t* rc;

	skb_layout_cache_t* layout_cache;

	view_t view;
	bool drag_view;
	bool drag_text;

	skb_vec2_t mouse_pos;
	bool mouse_pressed;
	intptr_t hover_item;

	bool show_glyph_bounds;
	float atlas_scale;

	GLFWcursor* hand_cursor;

} hyperlink_context_t;


#define LOAD_FONT_OR_FAIL(path, font_family) \
	if (!skb_font_collection_add_font(ctx->font_collection, path, font_family, NULL)) { \
		skb_debug_log("Failed to load " path "\n"); \
		goto error; \
	}

void hyperlink_destroy(void* ctx_ptr);
void hyperlink_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods);
void hyperlink_on_char(void* ctx_ptr, unsigned int codepoint);
void hyperlink_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods);
void hyperlink_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y);
void hyperlink_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods);
void hyperlink_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height);

void* hyperlink_create(GLFWwindow* window, render_context_t* rc)
{
	assert(rc);

	hyperlink_context_t* ctx = skb_malloc(sizeof(hyperlink_context_t));
	memset(ctx, 0, sizeof(hyperlink_context_t));

	ctx->base.create = hyperlink_create;
	ctx->base.destroy = hyperlink_destroy;
	ctx->base.on_key = hyperlink_on_key;
	ctx->base.on_char = hyperlink_on_char;
	ctx->base.on_mouse_button = hyperlink_on_mouse_button;
	ctx->base.on_mouse_move = hyperlink_on_mouse_move;
	ctx->base.on_mouse_scroll = hyperlink_on_mouse_scroll;
	ctx->base.on_update = hyperlink_on_update;

	ctx->window = window;
	ctx->rc = rc;
	render_reset_atlas(rc, NULL);

	ctx->hover_item = 0;
	ctx->atlas_scale = 0.0f;

	ctx->font_collection = skb_font_collection_create();
	assert(ctx->font_collection);

	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Regular.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Italic.ttf", SKB_FONT_FAMILY_DEFAULT);
	LOAD_FONT_OR_FAIL("data/IBMPlexSans-Bold.ttf", SKB_FONT_FAMILY_DEFAULT);
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

	ctx->icon_collection = skb_icon_collection_create();
	assert(ctx->icon_collection);

	skb_icon_handle_t icon_pen = skb_icon_collection_add_picosvg_icon(ctx->icon_collection, "pen", "data/pen_pico.svg");
	if (!icon_pen) {
		skb_debug_log("Failed to load icon3\n");
		goto error;
	}
	skb_icon_collection_set_is_color(ctx->icon_collection, icon_pen, false); // render as alpha.

	ctx->temp_alloc = skb_temp_alloc_create(512*1024);
	assert(ctx->temp_alloc);

	ctx->layout_cache = skb_layout_cache_create();
	assert(ctx->layout_cache);

	ctx->view = (view_t) { .cx = 400.f, .cy = 120.f, .scale = 1.f, .zoom_level = 0.f, };

	ctx->hand_cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

	return ctx;

error:
	hyperlink_destroy(ctx);
	return NULL;
}

void hyperlink_destroy(void* ctx_ptr)
{
	hyperlink_context_t* ctx = ctx_ptr;
	assert(ctx);

	skb_layout_cache_destroy(ctx->layout_cache);
	skb_font_collection_destroy(ctx->font_collection);
	skb_icon_collection_destroy(ctx->icon_collection);
	skb_temp_alloc_destroy(ctx->temp_alloc);

	glfwDestroyCursor(ctx->hand_cursor);

	memset(ctx, 0, sizeof(hyperlink_context_t));

	skb_free(ctx);
}

void hyperlink_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods)
{
	hyperlink_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_F9) {
			ctx->show_glyph_bounds = !ctx->show_glyph_bounds;
		}
		if (key == GLFW_KEY_F10) {
			ctx->atlas_scale += 0.25f;
			if (ctx->atlas_scale > 1.01f)
				ctx->atlas_scale = 0.0f;
		}
	}

	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_ESCAPE) {
			glfwSetWindowShouldClose(window, GL_TRUE);
		}
	}
}

void hyperlink_on_char(void* ctx_ptr, unsigned int codepoint)
{
	hyperlink_context_t* ctx = ctx_ptr;
	assert(ctx);
}

void hyperlink_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods)
{
	hyperlink_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS)
			ctx->mouse_pressed = true;
		if (action == GLFW_RELEASE)
			ctx->mouse_pressed = false;
	}

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
}

void hyperlink_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y)
{
	hyperlink_context_t* ctx = ctx_ptr;
	assert(ctx);

	ctx->mouse_pos = (skb_vec2_t){0};

	if (ctx->drag_view) {
		view_drag_move(&ctx->view, mouse_x, mouse_y);
	} else {
		ctx->mouse_pos = (skb_vec2_t) {
			.x = (mouse_x - ctx->view.cx) / ctx->view.scale,
			.y = (mouse_y - ctx->view.cy) / ctx->view.scale,
		};
	}
}

void hyperlink_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods)
{
	hyperlink_context_t* ctx = ctx_ptr;
	assert(ctx);

	const float zoom_speed = 0.2f;
	view_scroll_zoom(&ctx->view, mouse_x, mouse_y, delta_y * zoom_speed);
}

typedef struct draw_content_context_t {
	render_context_t* rc;
	const skb_layout_t* layout;
	skb_color_t color;
} draw_content_context_t;

void draw_content_bounds(skb_rect2_t rect, int32_t layout_run_idx, int32_t line_idx, void* context)
{
	draw_content_context_t* ctx = context;
	const skb_layout_run_t* layout_runs = skb_layout_get_layout_runs(ctx->layout);
	const  skb_layout_run_t* run = &layout_runs[layout_run_idx];
	if (run->type == SKB_CONTENT_RUN_ICON || run->type == SKB_CONTENT_RUN_OBJECT) {
		debug_render_filled_rect(ctx->rc, rect.x-3, rect.y-3, rect.width+6, rect.height+6, ctx->color);
	} else {
		debug_render_filled_rect(ctx->rc, rect.x-3, rect.y, rect.width+6, rect.height, ctx->color);
	}
}

void hyperlink_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height)
{
	hyperlink_context_t* ctx = ctx_ptr;
	assert(ctx);

	skb_layout_cache_compact(ctx->layout_cache);

	{
		skb_temp_alloc_stats_t stats = skb_temp_alloc_stats(ctx->temp_alloc);
		debug_render_text(ctx->rc, (float)view_width - 20,20, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,220), "Temp alloc  used:%.1fkB  allocated:%.1fkB", (float)stats.used / 1024.f, (float)stats.allocated / 1024.f);
		skb_temp_alloc_stats_t render_stats = skb_temp_alloc_stats(render_get_temp_alloc(ctx->rc));
		debug_render_text(ctx->rc, (float)view_width - 20,40, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,220), "Render Temp alloc  used:%.1fkB  allocated:%.1fkB", (float)render_stats.used / 1024.f, (float)render_stats.allocated / 1024.f);
	}

	render_push_transform(ctx->rc, ctx->view.cx, ctx->view.cy, ctx->view.scale);

	// Draw visual result

	{
		const skb_color_t text_color = skb_rgba(32,32,32,255);
		const skb_color_t link_color = skb_rgba(32,32,255,255);
		const skb_color_t active_link_color = skb_rgba(220,32,255,255);
		const skb_color_t link_color_trans = skb_rgba(32,32,255,32);
		const skb_color_t active_link_color_trans = skb_rgba(220,32,255,32);

		const skb_attribute_t layout_attributes[] = {
			skb_attribute_make_text_wrap(SKB_WRAP_WORD_CHAR),
			skb_attribute_make_baseline_align(SKB_BASELINE_CENTRAL),
		};

		skb_layout_params_t params = {
			.font_collection = ctx->font_collection,
			.icon_collection = ctx->icon_collection,
			.layout_width = 300.f,
			.layout_attributes = SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(layout_attributes),
		};

		const skb_attribute_t text_attributes[] = {
			skb_attribute_make_font_size(24.f),
			skb_attribute_make_fill(text_color),
		};

		const skb_attribute_t link_attributes[] = {
			skb_attribute_make_font_size(24.f),
			skb_attribute_make_fill(link_color),
			skb_attribute_make_decoration(SKB_DECORATION_UNDERLINE, SKB_DECORATION_STYLE_DOTTED, 3.f, 2.f, skb_rgba(0,0,0,0)),
		};

		const skb_attribute_t icon_attributes[] = {
			skb_attribute_make_object_align(0.5f, SKB_OBJECT_ALIGN_TEXT_AFTER_OR_BEFORE, SKB_BASELINE_CENTRAL),
			skb_attribute_make_fill(link_color),
		};

		skb_content_run_t runs[] = {
			skb_content_run_make_utf8("You could potentially click over ", -1, SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(text_attributes), 0),
			skb_content_run_make_utf8("here", -1, SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(link_attributes), 1),
			skb_content_run_make_utf8(" or maybe ", -1, SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(text_attributes), 0),
			skb_content_run_make_icon(skb_icon_collection_find_icon(ctx->icon_collection, "pen"), SKB_SIZE_AUTO, 24.f, SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(icon_attributes), 2),
			skb_content_run_make_utf8(" or eventually try ", -1, SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(text_attributes), 0),
			skb_content_run_make_utf8("this other one", -1, SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(link_attributes), 3),
			skb_content_run_make_utf8(".", -1, SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(text_attributes), 0),
		};

		const skb_layout_t* layout = skb_layout_cache_get_from_runs(ctx->layout_cache, ctx->temp_alloc, &params, runs, SKB_COUNTOF(runs));

		skb_layout_content_hit_t hit = skb_layout_hit_test_content(layout, ctx->mouse_pos.x, ctx->mouse_pos.y);
		ctx->hover_item = hit.run_id;

		const render_override_t hover_color_overrides[] = {
			render_color_override_make_fill(ctx->hover_item, link_color),
			render_color_override_make_decoration(ctx->hover_item, link_color),
		};

		const render_override_t active_color_overrides[] = {
			render_color_override_make_fill(ctx->hover_item, active_link_color),
			render_color_override_make_decoration(ctx->hover_item, active_link_color),
		};

		render_override_slice_t color_overrides = { 0 };
		if (ctx->hover_item != 0) {

			draw_content_context_t content_ctx = {
				.rc = ctx->rc,
				.layout = layout,
				.color = ctx->mouse_pressed ? active_link_color_trans : link_color_trans,
			};
			skb_layout_get_content_bounds(layout, ctx->hover_item, draw_content_bounds, &content_ctx);

			glfwSetCursor(ctx->window, ctx->hand_cursor);

			if (ctx->mouse_pressed)
				color_overrides = RENDER_OVERRIDE_SLICE_FROM_ARRAY(active_color_overrides);
			else
				color_overrides = RENDER_OVERRIDE_SLICE_FROM_ARRAY(hover_color_overrides);
		} else {
			glfwSetCursor(ctx->window, NULL);
		}

		render_draw_layout_with_color_overrides(ctx->rc, 0, 0, layout, SKB_RASTERIZE_ALPHA_SDF, color_overrides);

		/*
		{
			const skb_layout_line_t* layout_lines = skb_layout_get_lines(layout);
			const int32_t layout_lines_count = skb_layout_get_lines_count(layout);
			const skb_layout_run_t* layout_runs = skb_layout_get_layout_runs(layout);
			const int32_t layout_runs_count = skb_layout_get_layout_runs_count(layout);

			for (int32_t i = 0; i < layout_lines_count; i++) {
				const skb_layout_line_t* line = &layout_lines[i];

				debug_render_stroked_rect(ctx->rc, line->bounds.x, line->bounds.y, line->bounds.width, line->bounds.height, skb_rgba(0,0,255,128), -2.f);

				for (int32_t ri = line->layout_run_range.start; ri < line->layout_run_range.end; ri++) {
					const skb_layout_run_t* run = &layout_runs[ri];

					debug_render_dashed_rect(ctx->rc, run->offset_x, run->offset_y, run->content_width, run->content_height, -5.f, skb_rgba(255,0,0,128), -1.f);
				}

			}
		}
		*/
/*
		{
			const skb_layout_line_t* layout_lines = skb_layout_get_lines(layout);
			const int32_t layout_lines_count = skb_layout_get_lines_count(layout);

			for (int32_t i = 0; i < layout_lines_count; i++) {
				const skb_layout_line_t* line = &layout_lines[i];
				skb_caret_iterator_t caret_iter = skb_caret_iterator_make(layout, i);

				float x = 0.f;
				float advance = 0.f;
				skb_caret_iterator_result_t left = {0};
				skb_caret_iterator_result_t right = {0};

				while (skb_caret_iterator_next(&caret_iter, &x, &advance, &left, &right)) {

					debug_render_stroked_rect(ctx->rc, x, line->bounds.y, advance, line->bounds.height, skb_rgba(255,0,0,255), -2.f);

					debug_render_text(ctx->rc, x-0.1f, line->bounds.y+line->bounds.height+3, 3, RENDER_ALIGN_END, skb_rgba(0,220,0,255), "%d", left.text_position.offset);
					debug_render_text(ctx->rc, x-0.1f, line->bounds.y+line->bounds.height+6, 3, RENDER_ALIGN_END, skb_rgba(0,220,128,255), "%d", left.glyph_idx);

					debug_render_text(ctx->rc, x+0.1f, line->bounds.y+line->bounds.height+3, 3, RENDER_ALIGN_START, skb_rgba(220,0,0,255), "%d", right.text_position.offset);
					debug_render_text(ctx->rc, x+0.1f, line->bounds.y+line->bounds.height+6, 3, RENDER_ALIGN_START, skb_rgba(220,0,128,255), "%d", right.glyph_idx);
				}
			}
		}
*/
	}

	render_pop_transform(ctx->rc);

	// Draw atlas
	render_update_atlas(ctx->rc);
	debug_render_atlas_overlay(ctx->rc, 20.f, 50.f, ctx->atlas_scale, 1);

	// Draw info
	debug_render_text(ctx->rc, (float)view_width - 20.f, (float)view_height - 15.f, 13, RENDER_ALIGN_END, skb_rgba(0,0,0,255),
		"F9: Glyph details %s   F10: Atlas %.1f%%",
		ctx->show_glyph_bounds ? "ON" : "OFF", ctx->atlas_scale * 100.f);
}
