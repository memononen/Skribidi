// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "debug_draw.h"
#include "utils.h"

#include "skb_common.h"
#include "skb_font_collection.h"
#include "skb_layout.h"
#include "skb_layout_cache.h"
#include "skb_render.h"
#include "skb_render_cache.h"


typedef struct cached_context_t {
	example_t base;

	skb_font_collection_t* font_collection;
	skb_temp_alloc_t* temp_alloc;
	skb_render_cache_t* render_cache;
	skb_renderer_t* renderer;

	skb_layout_cache_t* layout_cache;

	view_t view;
	bool drag_view;
	bool drag_text;

	bool show_glyph_bounds;
	float atlas_scale;

} cached_context_t;


#define LOAD_FONT_OR_FAIL(path, font_family) \
	if (!skb_font_collection_add_font(ctx->font_collection, path, font_family)) { \
		skb_debug_log("Failed to load " path "\n"); \
		goto error; \
	}

static void on_create_texture(skb_render_cache_t* cache, uint8_t image_idx, void* context)
{
	const skb_image_t* image = skb_render_cache_get_image(cache, image_idx);
	if (image) {
		uint32_t tex_id = draw_create_texture(image->width, image->height, image->stride_bytes, NULL, image->bpp);
		skb_render_cache_set_image_user_data(cache, image_idx, tex_id);
	}
}

void cached_destroy(void* ctx_ptr);
void cached_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods);
void cached_on_char(void* ctx_ptr, unsigned int codepoint);
void cached_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods);
void cached_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y);
void cached_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods);
void cached_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height);

void* cached_create(void)
{
	cached_context_t* ctx = skb_malloc(sizeof(cached_context_t));
	memset(ctx, 0, sizeof(cached_context_t));

	ctx->base.create = cached_create;
	ctx->base.destroy = cached_destroy;
	ctx->base.on_key = cached_on_key;
	ctx->base.on_char = cached_on_char;
	ctx->base.on_mouse_button = cached_on_mouse_button;
	ctx->base.on_mouse_move = cached_on_mouse_move;
	ctx->base.on_mouse_scroll = cached_on_mouse_scroll;
	ctx->base.on_update = cached_on_update;

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

	ctx->temp_alloc = skb_temp_alloc_create(512*1024);
	assert(ctx->temp_alloc);

	ctx->render_cache = skb_render_cache_create(NULL);
	assert(ctx->render_cache);
	skb_render_cache_set_create_texture_callback(ctx->render_cache, &on_create_texture, NULL);

	ctx->renderer = skb_renderer_create(NULL);
	assert(ctx->renderer);

	ctx->layout_cache = skb_layout_cache_create();
	assert(ctx->layout_cache);

	ctx->view = (view_t) { .cx = 400.f, .cy = 120.f, .scale = 1.f, .zoom_level = 0.f, };

	return ctx;

error:
	cached_destroy(ctx);
	return NULL;
}

void cached_destroy(void* ctx_ptr)
{
	cached_context_t* ctx = ctx_ptr;
	assert(ctx);

	skb_layout_cache_destroy(ctx->layout_cache);
	skb_render_cache_destroy(ctx->render_cache);
	skb_renderer_destroy(ctx->renderer);
	skb_font_collection_destroy(ctx->font_collection);
	skb_temp_alloc_destroy(ctx->temp_alloc);

	memset(ctx, 0, sizeof(cached_context_t));

	skb_free(ctx);
}

void cached_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods)
{
	cached_context_t* ctx = ctx_ptr;
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

void cached_on_char(void* ctx_ptr, unsigned int codepoint)
{
	cached_context_t* ctx = ctx_ptr;
	assert(ctx);
}

void cached_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods)
{
	cached_context_t* ctx = ctx_ptr;
	assert(ctx);

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

void cached_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y)
{
	cached_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (ctx->drag_view) {
		view_drag_move(&ctx->view, mouse_x, mouse_y);
	}
}

void cached_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods)
{
	cached_context_t* ctx = ctx_ptr;
	assert(ctx);

	const float zoom_speed = 0.2f;
	view_scroll_zoom(&ctx->view, mouse_x, mouse_y, delta_y * zoom_speed);
}

void render_text(cached_context_t* ctx, float x, float y, float font_size, int32_t font_weight, skb_color_t color, const char* text)
{
	skb_layout_params_t params = {
		.base_direction = SKB_DIRECTION_AUTO,
		.font_collection = ctx->font_collection,
		.align = SKB_ALIGN_START,
		.baseline = SKB_BASELINE_MIDDLE,
	};

	skb_text_attribs_t attribs = {
		.font_size = font_size,
		.font_weight = (uint16_t)font_weight,
		.line_spacing_multiplier = 1.f, //1.3f,
		.color = color,
	};

	const skb_layout_t* layout = skb_layout_cache_get_utf8(ctx->layout_cache, ctx->temp_alloc, &params, text, -1, &attribs);
	assert(layout);

	// Draw layout
	const skb_glyph_t* glyphs = skb_layout_get_glyphs(layout);
	const int32_t glyphs_count = skb_layout_get_glyphs_count(layout);
	const skb_text_attribs_span_t* attrib_spans = skb_layout_get_attribute_spans(layout);
	const skb_layout_params_t* layout_params = skb_layout_get_params(layout);

	skb_glyph_run_iterator_t glyph_iter = skb_glyph_run_iterator_make(glyphs, glyphs_count, 0, glyphs_count);
	skb_range_t glyph_range;
	skb_font_handle_t font_handle = 0;
	uint16_t span_idx = 0;
	while (skb_glyph_run_iterator_next(&glyph_iter, &glyph_range, &font_handle, &span_idx)) {
		const skb_text_attribs_span_t* span = &attrib_spans[span_idx];
		for (int32_t gi = glyph_range.start; gi < glyph_range.end; gi++) {
			const skb_glyph_t* glyph = &glyphs[gi];

			const float gx = x + glyph->offset_x;
			const float gy = y + glyph->offset_y;

			// Glyph image
			skb_render_quad_t quad = skb_render_cache_get_glyph_quad(
				ctx->render_cache,gx, gy, ctx->view.scale,
				layout_params->font_collection, glyph->font_handle, glyph->gid,
				span->attribs.font_size, SKB_RENDER_ALPHA_SDF);

			draw_image_quad_sdf(
				view_transform_rect(&ctx->view, quad.geom_bounds),
				quad.image_bounds, 1.f / quad.scale, (quad.flags & SKB_RENDER_QUAD_IS_COLOR) ? skb_rgba(255,255,255, span->attribs.color.a) : span->attribs.color,
				(uint32_t)skb_render_cache_get_image_user_data(ctx->render_cache, quad.image_idx));
		}
	}
}

void cached_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height)
{
	cached_context_t* ctx = ctx_ptr;
	assert(ctx);

	draw_line_width(1.f);

	skb_layout_cache_compact(ctx->layout_cache);
	skb_render_cache_compact(ctx->render_cache);

	{
		skb_temp_alloc_stats_t stats = skb_temp_alloc_stats(ctx->temp_alloc);
		draw_text((float)view_width - 20,20, 12, 1.f, skb_rgba(0,0,0,255), "Temp alloc  used:%.1fkB  allocated:%.1fkB", (float)stats.used / 1024.f, (float)stats.allocated / 1024.f);
	}

	// Draw visual result
	const skb_color_t ink_color_trans = skb_rgba(32,32,32,128);

	render_text(ctx, 0.f, 0.f, 15.f, 400, ink_color_trans, "Moikka");
	render_text(ctx, 0.f, 20.f, 35.f, 700, skb_rgba(255,0,0,255), "Tsuiba! 123");
	render_text(ctx, 0.f, 70.f, 15.f, 400, skb_rgba(255,0,0,255), "😬👀🚨");


	// Update atlas and textures
	if (skb_render_cache_rasterize_missing_items(ctx->render_cache, ctx->temp_alloc, ctx->renderer)) {
		for (int32_t i = 0; i < skb_render_cache_get_image_count(ctx->render_cache); i++) {
			skb_rect2i_t dirty_bounds = skb_render_cache_get_and_reset_image_dirty_bounds(ctx->render_cache, i);
			if (!skb_rect2i_is_empty(dirty_bounds)) {
				const skb_image_t* image = skb_render_cache_get_image(ctx->render_cache, i);
				assert(image);
				uint32_t tex_id = (uint32_t)skb_render_cache_get_image_user_data(ctx->render_cache, i);
				if (tex_id == 0) {
					tex_id = draw_create_texture(image->width, image->height, image->stride_bytes, image->buffer, image->bpp);
					assert(tex_id);
					skb_render_cache_set_image_user_data(ctx->render_cache, i, tex_id);
				} else {
					draw_update_texture(tex_id,
							dirty_bounds.x, dirty_bounds.y, dirty_bounds.width, dirty_bounds.height,
							image->width, image->height, image->stride_bytes, image->buffer);
				}
			}
		}
	}

	// Draw atlas
	debug_draw_atlas(ctx->render_cache, 20.f, 50.f, ctx->atlas_scale, 1);

	// Draw info
	draw_text((float)view_width - 20.f, (float)view_height - 15.f, 12.f, 1.f, skb_rgba(0,0,0,255),
		"RMB: Pan view   Wheel: Zoom View   F9: Glyph details %s   F10: Atlas %.1f%%",
		ctx->show_glyph_bounds ? "ON" : "OFF", ctx->atlas_scale * 100.f);

}
