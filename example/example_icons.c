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
#include "skb_icon_collection.h"
#include "skb_render.h"
#include "skb_render_cache.h"


typedef struct icons_context_t {
	example_t base;

	skb_icon_collection_t* icon_collection;
	skb_temp_alloc_t* temp_alloc;
	skb_render_cache_t* render_cache;
	skb_renderer_t* renderer;

	view_t view;
	bool drag_view;

	bool use_view_scale;
	bool show_icon_bounds;
	float atlas_scale;

} icons_context_t;


static void on_create_texture(skb_render_cache_t* cache, uint8_t image_idx, void* context)
{
	const skb_image_t* image = skb_render_cache_get_image(cache, image_idx);
	if (image) {
		uint32_t tex_id = draw_create_texture(image->width, image->height, image->stride_bytes, NULL, image->bpp);
		skb_render_cache_set_image_user_data(cache, image_idx, tex_id);
	}
}

void icons_destroy(void* ctx_ptr);
void icons_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods);
void icons_on_char(void* ctx_ptr, unsigned int codepoint);
void icons_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods);
void icons_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y);
void icons_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods);
void icons_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height);

void* icons_create(void)
{
	icons_context_t* ctx = skb_malloc(sizeof(icons_context_t));
	memset(ctx, 0, sizeof(icons_context_t));

	ctx->base.create = icons_create;
	ctx->base.destroy = icons_destroy;
	ctx->base.on_key = icons_on_key;
	ctx->base.on_char = icons_on_char;
	ctx->base.on_mouse_button = icons_on_mouse_button;
	ctx->base.on_mouse_move = icons_on_mouse_move;
	ctx->base.on_mouse_scroll = icons_on_mouse_scroll;
	ctx->base.on_update = icons_on_update;

	ctx->atlas_scale = 0.0f;
	ctx->use_view_scale = true;
	ctx->show_icon_bounds = true;

	ctx->icon_collection = skb_icon_collection_create();
	assert(ctx->icon_collection);

	skb_icon_handle_t icon1 = skb_icon_collection_add_picosvg_icon(ctx->icon_collection, "icon", "data/grad_pico.svg");
	if (!icon1) {
		skb_debug_log("Failed to load icon1\n");
		goto error;
	}
	skb_icon_handle_t icon2 = skb_icon_collection_add_picosvg_icon(ctx->icon_collection, "astro", "data/astronaut_pico.svg");
	if (!icon2) {
		skb_debug_log("Failed to load icon2\n");
		goto error;
	}

	// Procedural icon
	{
		skb_icon_handle_t icon3 = skb_icon_collection_add_icon(ctx->icon_collection, "arrow", 20,20);
		if (!icon3) {
			skb_debug_log("Failed to make icon3\n");
			goto error;
		}

		skb_icon_builder_t builder = skb_icon_builder_make(ctx->icon_collection, icon3);

		skb_icon_builder_begin_shape(&builder);

		skb_icon_builder_move_to(&builder, (skb_vec2_t){18,10});
		skb_icon_builder_line_to(&builder, (skb_vec2_t){4,16});
		skb_icon_builder_quad_to(&builder, (skb_vec2_t){8,10}, (skb_vec2_t){4,4});
		skb_icon_builder_close_path(&builder);
		skb_color_stop_t stops[] = {
			{0.1f, skb_rgba(255,198,176,255) },
			{0.6f, skb_rgba(255,102,0,255) },
			{1.f, skb_rgba(163,53,53,255) },
		};
		skb_icon_builder_fill_linear_gradient(&builder, (skb_vec2_t){8,4}, (skb_vec2_t){12,16}, skb_mat2_make_identity(), SKB_SPREAD_PAD, stops, SKB_COUNTOF(stops));

		skb_icon_builder_end_shape(&builder);
	}

	// Make simiar icons with different spread modes.
	for (int32_t i = 0; i < 3; i++) {
		char name[32];
		snprintf(name, 32, "grad_%d", i);
		skb_icon_handle_t icon = skb_icon_collection_add_icon(ctx->icon_collection, name, 20,100);
		if (!icon) {
			skb_debug_log("Failed to make %s\n", name);
			goto error;
		}

		skb_gradient_spread_t spread = SKB_SPREAD_PAD;
		if (i == 1) spread = SKB_SPREAD_REPEAT;
		if (i == 2) spread = SKB_SPREAD_REFLECT;

		skb_icon_builder_t builder = skb_icon_builder_make(ctx->icon_collection, icon);

		skb_icon_builder_begin_shape(&builder);

		skb_icon_builder_move_to(&builder, (skb_vec2_t){2,2});
		skb_icon_builder_line_to(&builder, (skb_vec2_t){18,2});
		skb_icon_builder_line_to(&builder, (skb_vec2_t){18,98});
		skb_icon_builder_line_to(&builder, (skb_vec2_t){2,98});
		skb_icon_builder_close_path(&builder);
		skb_color_stop_t stops[] = {
			{0.0f, skb_rgba(255,102,0,255) },
			{0.5f, skb_rgba(238,242,33,255) },
			{1.f, skb_rgba(49,109,237,255) },
		};
		skb_icon_builder_fill_linear_gradient(&builder, (skb_vec2_t){2,25}, (skb_vec2_t){2,50}, skb_mat2_make_identity(), spread, stops, SKB_COUNTOF(stops));

		skb_icon_builder_end_shape(&builder);
	}

	ctx->temp_alloc = skb_temp_alloc_create(512*1024);
	assert(ctx->temp_alloc);


	skb_render_cache_config_t render_cache_config = skb_render_cache_get_default_config();
	render_cache_config.atlas_init_width = 512;
	render_cache_config.atlas_init_height = 1024;
	render_cache_config.atlas_max_width = 1024;
	render_cache_config.atlas_max_height = 4096;

	ctx->render_cache = skb_render_cache_create(&render_cache_config);
	assert(ctx->render_cache);
	skb_render_cache_set_create_texture_callback(ctx->render_cache, &on_create_texture, NULL);

	skb_renderer_config_t renderer_config = skb_renderer_get_default_config();
	ctx->renderer = skb_renderer_create(&renderer_config);
	assert(ctx->renderer);

	ctx->view = (view_t) { .cx = 400.f, .cy = 120.f, .scale = 1.f, .zoom_level = 0.f, };

	return ctx;

error:
	icons_destroy(ctx);
	return NULL;
}

void icons_destroy(void* ctx_ptr)
{
	icons_context_t* ctx = ctx_ptr;
	assert(ctx);

	skb_icon_collection_destroy(ctx->icon_collection);

	skb_render_cache_destroy(ctx->render_cache);
	skb_renderer_destroy(ctx->renderer);
	skb_temp_alloc_destroy(ctx->temp_alloc);

	memset(ctx, 0, sizeof(icons_context_t));

	skb_free(ctx);
}

void icons_on_key(void* ctx_ptr, GLFWwindow* window, int key, int action, int mods)
{
	icons_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_F8) {
			ctx->use_view_scale = !ctx->use_view_scale;
		}
		if (key == GLFW_KEY_F9) {
			ctx->show_icon_bounds = !ctx->show_icon_bounds;
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

void icons_on_char(void* ctx_ptr, unsigned int codepoint)
{
	icons_context_t* ctx = ctx_ptr;
	assert(ctx);
}

void icons_on_mouse_button(void* ctx_ptr, float mouse_x, float mouse_y, int button, int action, int mods)
{
	icons_context_t* ctx = ctx_ptr;
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

void icons_on_mouse_move(void* ctx_ptr, float mouse_x, float mouse_y)
{
	icons_context_t* ctx = ctx_ptr;
	assert(ctx);

	if (ctx->drag_view) {
		view_drag_move(&ctx->view, mouse_x, mouse_y);
	}
}

void icons_on_mouse_scroll(void* ctx_ptr, float mouse_x, float mouse_y, float delta_x, float delta_y, int mods)
{
	icons_context_t* ctx = ctx_ptr;
	assert(ctx);

	const float zoom_speed = 0.2f;
	view_scroll_zoom(&ctx->view, mouse_x, mouse_y, delta_y * zoom_speed);
}

static float draw_icon(icons_context_t* ctx, skb_icon_handle_t icon_handle, float ox, float oy, float icon_size, int32_t alpha_mode, bool use_view_scale)
{
	if (!icon_handle) return 0.f;

	skb_vec2_t icon_scale = skb_icon_collection_calc_proportional_scale(ctx->icon_collection, icon_handle, -1.f, (float)icon_size);
	skb_vec2_t icon_base_size = skb_icon_collection_get_icon_size(ctx->icon_collection, icon_handle);

	skb_rect2_t icon_rect = {
		ox, oy, icon_base_size.x * icon_scale.x, icon_base_size.y * icon_scale.y,
	};
	icon_rect = view_transform_rect(&ctx->view, icon_rect);

	if (ctx->show_icon_bounds)
		draw_rect(icon_rect.x, icon_rect.y, icon_rect.width, icon_rect.height, skb_rgba(0,0,0,64));

	float view_scale = use_view_scale ? ctx->view.scale : 1.f;

	skb_render_quad_t quad = skb_render_cache_get_icon_quad(
		ctx->render_cache,ox, oy, view_scale,
		ctx->icon_collection, icon_handle, icon_scale, alpha_mode);

	float render_scale = use_view_scale ? quad.scale : quad.scale * ctx->view.scale;

	if (alpha_mode == SKB_RENDER_ALPHA_SDF) {
		draw_image_quad_sdf(
			view_transform_rect(&ctx->view, quad.geom_bounds),
			quad.image_bounds, 1.f / render_scale, skb_rgba(255,255,255,255),
			(uint32_t)skb_render_cache_get_image_user_data(ctx->render_cache, quad.image_idx));
	} else {
		draw_image_quad(
			view_transform_rect(&ctx->view, quad.geom_bounds),
			quad.image_bounds, skb_rgba(255,255,255,255),
			(uint32_t)skb_render_cache_get_image_user_data(ctx->render_cache, quad.image_idx));
	}

	return icon_base_size.x * icon_scale.x + 10.f;
}

void icons_on_update(void* ctx_ptr, int32_t view_width, int32_t view_height)
{
	icons_context_t* ctx = ctx_ptr;
	assert(ctx);

	draw_line_width(1.f);

	skb_render_cache_compact(ctx->render_cache);

	{
		skb_temp_alloc_stats_t stats = skb_temp_alloc_stats(ctx->temp_alloc);
		draw_text((float)view_width - 20,20, 12, 1.f, skb_rgba(0,0,0,255), "Temp alloc  used:%.1fkB  allocated:%.1fkB", (float)stats.used / 1024.f, (float)stats.allocated / 1024.f);
	}


	float ox = 20.f;
	float oy = 20.f;

	{
		ox = 20.f;
		draw_text(view_transform_x(&ctx->view, ox), view_transform_y(&ctx->view, oy) - 10.f, 12, 0.f, skb_rgba(0,0,0,255), "SDF");
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "icon"), ox, oy, 128.f, SKB_RENDER_ALPHA_SDF, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "astro"), ox, oy, 128.f, SKB_RENDER_ALPHA_SDF, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "arrow"), ox, oy, 40.f, SKB_RENDER_ALPHA_SDF, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "grad_0"), ox, oy, 100.f, SKB_RENDER_ALPHA_SDF, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "grad_1"), ox, oy, 100.f, SKB_RENDER_ALPHA_SDF, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "grad_2"), ox, oy, 100.f, SKB_RENDER_ALPHA_SDF, ctx->use_view_scale);

	}
	oy += 180.f;

	{
		ox = 20.f;
		draw_text(view_transform_x(&ctx->view, ox), view_transform_y(&ctx->view, oy) - 10.f, 12, 0.f, skb_rgba(0,0,0,255), "Alpha");
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "icon"), ox, oy, 128.f, SKB_RENDER_ALPHA_MASK, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "astro"), ox, oy, 128.f, SKB_RENDER_ALPHA_MASK, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "arrow"), ox, oy, 40.f, SKB_RENDER_ALPHA_MASK, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "grad_0"), ox, oy, 100.f, SKB_RENDER_ALPHA_MASK, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "grad_1"), ox, oy, 100.f, SKB_RENDER_ALPHA_MASK, ctx->use_view_scale);
		ox += draw_icon(ctx, skb_icon_collection_find_icon(ctx->icon_collection, "grad_2"), ox, oy, 100.f, SKB_RENDER_ALPHA_MASK, ctx->use_view_scale);
	}

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
		"RMB: Pan view   Wheel: Zoom View   F8: Use view scale %s   F9: Icon details %s   F10: Atlas %.1f%%",
		ctx->use_view_scale ? "ON" : "OFF", ctx->show_icon_bounds ? "ON" : "OFF", ctx->atlas_scale * 100.f);

}
