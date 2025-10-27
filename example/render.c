// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "render.h"
#include <string.h>
#include <glad/gl.h>
#include <stdlib.h>
#include "skb_image_atlas.h"
#include "skb_rasterizer.h"
#include "skb_common.h"
#include "skb_layout.h"


typedef struct renderer__vertex_t {
	skb_vec2_t pos;
	skb_vec2_t uv;
	skb_vec2_t atlas_pos;
	skb_vec2_t atlas_size;
	skb_color_t color;
	float scale;
} render__vertex_t;

typedef struct renderer__batch_t {
	int32_t offset;
	int32_t count;
	uint32_t image_id;
	uint32_t sdf_id;
} render__batch_t;

typedef struct renderer__texture_t {
	uint32_t id;
	int32_t width;
	int32_t height;
	uint8_t bpp;
	// GL
	GLuint tex_id;
} render__texture_t;

typedef struct renderer__transform_t {
	float dx;
	float dy;
	float scale;
} render__transform_t;

enum {
	RENDER_TRANSFORM_STACK_SIZE = 10,
};

typedef struct render_context_t {
	skb_temp_alloc_t* temp_alloc;
	skb_image_atlas_t* atlas;
	skb_rasterizer_t* rasterizer;

	render__vertex_t* verts;
	int32_t verts_count;
	int32_t verts_cap;

	render__batch_t* batches;
	int32_t batches_count;
	int32_t batches_cap;

	render__texture_t* textures;
	int32_t textures_count;
	int32_t textures_cap;

	int32_t view_width;
	int32_t view_height;

	render__transform_t transform_stack[RENDER_TRANSFORM_STACK_SIZE];
	int32_t transform_stack_idx;

	// GL
	GLuint program;
	GLuint vbo;
} render_context_t;


static bool gl__init(render_context_t* rc);
static void gl__destroy(render_context_t* rc);
static void gl__flush(render_context_t* rc);
static void gl__create_texture(render__texture_t* tex, int32_t width, int32_t height, int32_t stride_bytes, const uint8_t* image, uint8_t bpp);
static void gl__update_texture(render__texture_t* tex, int32_t offset_x, int32_t offset_y, int32_t width, int32_t height, int32_t stride_bytes, const uint8_t* image);


static void renderer__on_create_texture(skb_image_atlas_t* atlas, uint8_t texture_idx, void* context)
{
	render_context_t* r = (render_context_t*)context;
	const skb_image_t* texture = skb_image_atlas_get_texture(atlas, texture_idx);
	if (texture) {
		uint32_t tex_id = render_create_texture(r, texture->width, texture->height, texture->stride_bytes, NULL, texture->bpp);
		skb_image_atlas_set_texture_user_data(atlas, texture_idx, tex_id);
	}
}

static render__texture_t* renderer__find_texture(const render_context_t* r, uint32_t id)
{
	if (id == 0)
		return NULL;
	for (int32_t i = 0; i < r->textures_count; i++) {
		if (r->textures[i].id == id) {
			return &r->textures[i];
		}
	}
	return NULL;
}

static void renderer__push_triangles(render_context_t* r, const render__vertex_t* verts, int32_t verts_count, uint32_t image_id, uint32_t sdf_id)
{
	render__batch_t* batch = NULL;

	render__batch_t* prev_batch = r->batches_count > 0 ? &r->batches[r->batches_count-1] : NULL;
	if (prev_batch && prev_batch->image_id == image_id && prev_batch->sdf_id == sdf_id) {
		batch = prev_batch;
	} else {
		SKB_ARRAY_RESERVE(r->batches, r->batches_count + 1);
		batch = &r->batches[r->batches_count++];
		batch->offset = r->verts_count;
		batch->count = 0;
		batch->image_id = image_id;
		batch->sdf_id = sdf_id;
	}

	SKB_ARRAY_RESERVE(r->verts, r->verts_count + verts_count);
	memcpy(&r->verts[r->verts_count], verts, verts_count * sizeof(render__vertex_t));

	batch->count += verts_count;
	r->verts_count += verts_count;
}

//
// API
//

render_context_t* render_create(const skb_image_atlas_config_t* config)
{
	render_context_t* r = skb_malloc(sizeof(render_context_t));
	memset(r, 0, sizeof(render_context_t));

	r->temp_alloc = skb_temp_alloc_create(512*1024);
	assert(r->temp_alloc);

	r->atlas = skb_image_atlas_create(config);
	assert(r->atlas);
	skb_image_atlas_set_create_texture_callback(r->atlas, &renderer__on_create_texture, r);

	r->rasterizer = skb_rasterizer_create(NULL);
	assert(r->rasterizer);

	const bool init_ok = gl__init(r);
	assert(init_ok);

	return r;
}

void render_destroy(render_context_t* rc)
{
	if (!rc)
		return;

	gl__destroy(rc);

	skb_image_atlas_destroy(rc->atlas);
	skb_rasterizer_destroy(rc->rasterizer);
	skb_temp_alloc_destroy(rc->temp_alloc);

	memset(rc, 0, sizeof(render_context_t));
	skb_free(rc);
}

void render_reset_atlas(render_context_t* rc, const skb_image_atlas_config_t* config)
{
	// Reset atlas
	skb_image_atlas_destroy(rc->atlas);
	rc->atlas = skb_image_atlas_create(config);
	assert(rc->atlas);
	skb_image_atlas_set_create_texture_callback(rc->atlas, &renderer__on_create_texture, rc);

	// Reset textures.
	for (int32_t i = 0; i < rc->textures_count; i++) {
		render__texture_t* tex = &rc->textures[i];
		glDeleteTextures(1, &tex->tex_id);
	}
	rc->textures_count = 0;
}

void render_begin_frame(render_context_t* rc, int32_t view_width, int32_t view_height)
{
	assert(rc);

	rc->view_width = view_width;
	rc->view_height = view_height;

	rc->transform_stack_idx = 0;
	rc->transform_stack[0].dx = 0.f;
	rc->transform_stack[0].dy = 0.f;
	rc->transform_stack[0].scale = 1.f;

	skb_image_atlas_compact(rc->atlas);
}

void render_push_transform(render_context_t* rc, float offset_x, float offset_y, float scale)
{
	assert(rc);
	if (rc->transform_stack_idx+1 < RENDER_TRANSFORM_STACK_SIZE) {
		rc->transform_stack_idx++;
		rc->transform_stack[rc->transform_stack_idx].dx = offset_x;
		rc->transform_stack[rc->transform_stack_idx].dy = offset_y;
		rc->transform_stack[rc->transform_stack_idx].scale = scale;
	}
}

void render_pop_transform(render_context_t* rc)
{
	assert(rc);
	if (rc->transform_stack_idx > 0)
		rc->transform_stack_idx--;
}

float render_get_transform_scale(render_context_t* rc)
{
	assert(rc);
	return rc->transform_stack[rc->transform_stack_idx].scale;
}

skb_vec2_t render_get_transform_offset(render_context_t* rc)
{
	assert(rc);
	return (skb_vec2_t) {
		.x = rc->transform_stack[rc->transform_stack_idx].dx,
		.y = rc->transform_stack[rc->transform_stack_idx].dy
	};
}

skb_rect2_t render_transform_rect(render_context_t* rc, const skb_rect2_t rect)
{
	const render__transform_t xform = rc->transform_stack[rc->transform_stack_idx];
	return (skb_rect2_t) {
		.x = xform.dx + rect.x * xform.scale,
		.y = xform.dy + rect.y * xform.scale,
		.width = rect.width * xform.scale,
		.height = rect.height * xform.scale,
	};
}

skb_rect2_t render_inv_transform_rect(render_context_t* rc, const skb_rect2_t rect)
{
	const render__transform_t xform = rc->transform_stack[rc->transform_stack_idx];
	return (skb_rect2_t) {
		.x = (rect.x - xform.dx) / xform.scale,
		.y = (rect.y - xform.dy) / xform.scale,
		.width = rect.width / xform.scale,
		.height = rect.height / xform.scale,
	};
}


skb_image_atlas_t* render_get_atlas(render_context_t* rc)
{
	assert(rc);
	return rc->atlas;
}

skb_rasterizer_t* render_get_rasterizer(render_context_t* rc)
{
	assert(rc);
	return rc->rasterizer;
}

skb_temp_alloc_t* render_get_temp_alloc(render_context_t* rc)
{
	assert(rc);
	return rc->temp_alloc;
}

uint32_t render_create_texture(render_context_t* rc, int32_t img_width, int32_t img_height, int32_t img_stride_bytes, const uint8_t* img_data, uint8_t bpp)
{
	assert(rc);

	SKB_ARRAY_RESERVE(rc->textures, rc->textures_count + 1);
	render__texture_t* tex = &rc->textures[rc->textures_count++];
	memset(tex, 0, sizeof(render__texture_t));
	tex->id = rc->textures_count;

	gl__create_texture(tex, img_width, img_height, img_stride_bytes, img_data, bpp);

	return tex->id;
}

void render_update_texture(render_context_t* rc, uint32_t tex_handle,
	int32_t offset_x, int32_t offset_y, int32_t width, int32_t height,
	int32_t img_width, int32_t img_height, int32_t img_stride_bytes, const uint8_t* img_data)
{
	assert(rc);

	render__texture_t* tex = renderer__find_texture(rc, tex_handle);
	if (tex) {
		if (tex->width != img_width || tex->height != img_height) {
			gl__create_texture(tex, img_width, img_height, img_stride_bytes, img_data, tex->bpp);
		} else {
			gl__update_texture(tex, offset_x, offset_y, width, height, img_stride_bytes, img_data);
		}
	}
}

void render_draw_debug_tris(render_context_t* rc, const render_vert_t* verts, int32_t verts_count)
{
	assert(rc);

	const render__transform_t xform = rc->transform_stack[rc->transform_stack_idx];

	render__vertex_t transformed_verts[16*3];
	int32_t transformed_verts_count = 0;

	for (int32_t i = 0; i < verts_count; i++) {
		const skb_vec2_t pos = verts[i].pos;
		const skb_color_t col = verts[i].col;

		transformed_verts[transformed_verts_count++] = (render__vertex_t) {
			.pos = (skb_vec2_t) {
				.x = xform.dx + pos.x * xform.scale,
				.y = xform.dy + pos.y * xform.scale
			},
			.color = col,
		};

		if (transformed_verts_count >= (16*3)) {
			renderer__push_triangles(rc, transformed_verts, transformed_verts_count, 0, 0);
			transformed_verts_count = 0;
		}
	}

	if (transformed_verts_count > 0)
		renderer__push_triangles(rc, transformed_verts, transformed_verts_count, 0, 0);
}

void render_draw_quad(render_context_t* rc, const skb_quad_t* quad)
{
	assert(rc);

	const skb_rect2_t geom = render_transform_rect(rc, quad->geom);
	const float x0 = geom.x;
	const float y0 = geom.y;
	const float x1 = geom.x + geom.width;
	const float y1 = geom.y + geom.height;
	const float u0 = quad->pattern.x;
	const float v0 = quad->pattern.y;
	const float u1 = quad->pattern.x + quad->pattern.width;
	const float v1 = quad->pattern.y + quad->pattern.height;
	const skb_vec2_t tex_pos = {quad->texture.x, quad->texture.y };
	const skb_vec2_t tex_size = {quad->texture.width, quad->texture.height };
	const float scale = quad->scale;
	const skb_color_t tint = quad->color;

	const render__vertex_t verts[] = {
		{ .pos = {x0, y0}, .uv = {u0, v0 }, .atlas_pos = tex_pos, .atlas_size = tex_size, .color = tint, .scale = scale },
		{ .pos = {x1, y0}, .uv = {u1, v0 }, .atlas_pos = tex_pos, .atlas_size = tex_size, .color = tint, .scale = scale },
		{ .pos = {x1, y1}, .uv = {u1, v1 }, .atlas_pos = tex_pos, .atlas_size = tex_size, .color = tint, .scale = scale },

		{ .pos = {x0, y0}, .uv = {u0, v0 }, .atlas_pos = tex_pos, .atlas_size = tex_size, .color = tint, .scale = scale },
		{ .pos = {x1, y1}, .uv = {u1, v1 }, .atlas_pos = tex_pos, .atlas_size = tex_size, .color = tint, .scale = scale },
		{ .pos = {x0, y1}, .uv = {u0, v1 }, .atlas_pos = tex_pos, .atlas_size = tex_size, .color = tint, .scale = scale },
	};

	const uint32_t tex_id = (uint32_t)skb_image_atlas_get_texture_user_data(rc->atlas, quad->texture_idx);

	if (quad->flags & SKB_QUAD_IS_SDF)
		renderer__push_triangles(rc, verts, SKB_COUNTOF(verts), 0, tex_id);
	else
		renderer__push_triangles(rc, verts, SKB_COUNTOF(verts), tex_id, 0);
}

void render_update_atlas(render_context_t* rc)
{
	// Update atlas and textures
	if (skb_image_atlas_rasterize_missing_items(rc->atlas, rc->temp_alloc, rc->rasterizer)) {
		for (int32_t i = 0; i < skb_image_atlas_get_texture_count(rc->atlas); i++) {
			skb_rect2i_t dirty_bounds = skb_image_atlas_get_and_reset_texture_dirty_bounds(rc->atlas, i);
			if (!skb_rect2i_is_empty(dirty_bounds)) {
				const skb_image_t* image = skb_image_atlas_get_texture(rc->atlas, i);
				assert(image);
				uint32_t tex_id = (uint32_t)skb_image_atlas_get_texture_user_data(rc->atlas, i);
				if (tex_id == 0) {
					tex_id = render_create_texture(rc, image->width, image->height, image->stride_bytes, image->buffer, image->bpp);
					assert(tex_id);
					skb_image_atlas_set_texture_user_data(rc->atlas, i, tex_id);
				} else {
					render_update_texture(rc, tex_id,
						dirty_bounds.x, dirty_bounds.y, dirty_bounds.width, dirty_bounds.height,
						image->width, image->height, image->stride_bytes, image->buffer);
				}
			}
		}
	}
}

void render_end_frame(render_context_t* rc)
{
	assert(rc);

	render_update_atlas(rc);

	if (rc->verts_count == 0 || rc->batches_count == 0) {
		rc->verts_count = 0;
		rc->batches_count = 0;
		return;
	}

	gl__flush(rc);
}

//
// High level API
//

void render_draw_glyph(render_context_t* rc,
	float offset_x, float offset_y,
	skb_font_collection_t* font_collection, skb_font_handle_t font_handle, uint32_t glyph_id, float font_size,
	skb_color_t color, skb_rasterize_alpha_mode_t alpha_mode)
{
	assert(rc);
	assert(font_collection);
	skb_image_atlas_t* atlas = render_get_atlas(rc);

	// Glyph image
	skb_quad_t quad = skb_image_atlas_get_glyph_quad(
		atlas, offset_x, offset_y, render_get_transform_scale(rc),
		font_collection, font_handle, glyph_id, font_size,
		color, alpha_mode);

	render_draw_quad(rc, &quad);
}

void render_draw_icon(render_context_t* rc,
	float offset_x, float offset_y,
	skb_icon_collection_t* icon_collection, skb_icon_handle_t icon_handle, float width, float height,
	skb_color_t color, skb_rasterize_alpha_mode_t alpha_mode)
{
	assert(rc);

	skb_image_atlas_t* atlas = render_get_atlas(rc);

	skb_quad_t quad = skb_image_atlas_get_icon_quad(
		atlas, offset_x, offset_y, render_get_transform_scale(rc),
		icon_collection, icon_handle, width, height,
		color, alpha_mode);

	render_draw_quad(rc, &quad);
}

void render_draw_decoration(render_context_t* rc,
	float offset_x, float offset_y,
	skb_decoration_style_t style, skb_decoration_position_t position, float length, float pattern_offset, float thickness,
	skb_color_t color, skb_rasterize_alpha_mode_t alpha_mode)
{
	assert(rc);

	skb_image_atlas_t* atlas = render_get_atlas(rc);

	skb_quad_t quad = skb_image_atlas_get_decoration_quad(
		atlas, offset_x, offset_y, render_get_transform_scale(rc),
		position, style, length, pattern_offset, thickness,
		color, alpha_mode);
	render_draw_quad(rc, &quad);
}

void render_draw_layout(render_context_t* rc, float offset_x, float offset_y, const skb_layout_t* layout, skb_rasterize_alpha_mode_t alpha_mode)
{
	assert(rc);
	assert(layout);

	// Draw layout
	const skb_layout_params_t* layout_params = skb_layout_get_params(layout);
	const skb_layout_run_t* layout_runs = skb_layout_get_layout_runs(layout);
	const int32_t layout_runs_count = skb_layout_get_layout_runs_count(layout);
	const skb_glyph_t* glyphs = skb_layout_get_glyphs(layout);
	const int32_t decorations_count = skb_layout_get_decorations_count(layout);
	const skb_decoration_t* decorations = skb_layout_get_decorations(layout);

	// Draw underlines
	for (int32_t i = 0; i < decorations_count; i++) {
		const skb_decoration_t* decoration = &decorations[i];
		if (decoration->position != SKB_DECORATION_THROUGHLINE) {
			render_draw_decoration(rc, offset_x + decoration->offset_x,  offset_y + decoration->offset_y,
				decoration->style, decoration->position, decoration->length, decoration->pattern_offset, decoration->thickness,
				decoration->color, alpha_mode);
		}
	}

	// Draw glyphs
	for (int32_t ri = 0; ri < layout_runs_count; ri++) {
		const skb_layout_run_t* run = &layout_runs[ri];
		const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(layout, run);
		const skb_attribute_fill_t attr_fill = skb_attributes_get_fill(run_attributes, layout_params->attribute_collection);

		if (run->type == SKB_CONTENT_RUN_OBJECT) {
			// Object
		} else if (run->type == SKB_CONTENT_RUN_ICON) {
			// Icon
			render_draw_icon(rc, offset_x + run->bounds.x, offset_y + run->bounds.y,
				layout_params->icon_collection, run->icon_handle, run->bounds.width, run->bounds.height,
				attr_fill.color, alpha_mode);
		} else {
			// Text
			for (int32_t gi = run->glyph_range.start; gi < run->glyph_range.end; gi++) {
				const skb_glyph_t* glyph = &glyphs[gi];
				render_draw_glyph(rc, offset_x + glyph->offset_x, offset_y + glyph->offset_y,
					layout_params->font_collection, run->font_handle, glyph->gid, run->font_size,
					attr_fill.color, alpha_mode);
			}
		}
	}

	// Draw through lines.
	for (int32_t i = 0; i < decorations_count; i++) {
		const skb_decoration_t* decoration = &decorations[i];
		if (decoration->position == SKB_DECORATION_THROUGHLINE) {
			render_draw_decoration(rc, offset_x + decoration->offset_x, offset_y + decoration->offset_y,
				decoration->style, decoration->position, decoration->length, decoration->pattern_offset, decoration->thickness,
				decoration->color, alpha_mode);
		}
	}
}

static void override_color(render_override_type_t type, intptr_t content_data, skb_color_t* color, render_override_slice_t color_overrides)
{
	for (int32_t i = 0; i < color_overrides.count; i++) {
		if (color_overrides.items[i].type == type && color_overrides.items[i].content_id == content_data) {
			*color = color_overrides.items[i].color;
			break;
		}
	}
}

static bool has_override(render_override_type_t type, render_override_slice_t color_overrides)
{
	for (int32_t i = 0; i < color_overrides.count; i++)
		if (color_overrides.items[i].type == type)
			return true;
	return false;
}

render_override_t render_color_override_make_fill(intptr_t content_data, skb_color_t color)
{
	return (render_override_t) {
		.type = RENDER_OVERRIDE_FILL,
		.content_id = content_data,
		.color = color,
	};
}

render_override_t render_color_override_make_decoration(intptr_t content_data, skb_color_t color)
{
	return (render_override_t) {
		.type = RENDER_OVERRIDE_DECORATION,
		.content_id = content_data,
		.color = color,
	};
}

void render_draw_layout_with_color_overrides(render_context_t* rc, float offset_x, float offset_y, const skb_layout_t* layout, skb_rasterize_alpha_mode_t alpha_mode, render_override_slice_t color_overrides)
{
	assert(rc);
	assert(layout);

	// Draw layout
	const skb_layout_params_t* layout_params = skb_layout_get_params(layout);
	const skb_layout_run_t* layout_runs = skb_layout_get_layout_runs(layout);
	const int32_t layout_runs_count = skb_layout_get_layout_runs_count(layout);
	const skb_glyph_t* glyphs = skb_layout_get_glyphs(layout);
	const int32_t decorations_count = skb_layout_get_decorations_count(layout);
	const skb_decoration_t* decorations = skb_layout_get_decorations(layout);

	const bool has_decoration_overrides = has_override(RENDER_OVERRIDE_DECORATION, color_overrides);
	const bool has_fill_overrides = has_override(RENDER_OVERRIDE_FILL, color_overrides);

	// Draw underlines
	for (int32_t i = 0; i < decorations_count; i++) {
		const skb_decoration_t* decoration = &decorations[i];
		const skb_layout_run_t* run = &layout_runs[decoration->layout_run_idx];
		if (decoration->position != SKB_DECORATION_THROUGHLINE) {
			skb_color_t color = decoration->color;
			if (has_decoration_overrides)
				override_color(RENDER_OVERRIDE_DECORATION, run->content_run_id, &color, color_overrides);
			render_draw_decoration(rc, offset_x + decoration->offset_x, offset_y + decoration->offset_y,
				decoration->style, decoration->position, decoration->length, decoration->pattern_offset, decoration->thickness,
				color, alpha_mode);
		}
	}

	// Draw glyphs
	for (int32_t ri = 0; ri < layout_runs_count; ri++) {
		const skb_layout_run_t* run = &layout_runs[ri];
		const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(layout, run);
		const skb_attribute_fill_t attr_fill = skb_attributes_get_fill(run_attributes, layout_params->attribute_collection);

		// TODO: handle color glyph/icon
		skb_color_t color = attr_fill.color;
		if (has_fill_overrides)
			override_color(RENDER_OVERRIDE_FILL, run->content_run_id, &color, color_overrides);

		if (run->type == SKB_CONTENT_RUN_OBJECT) {
			// Object
		} else if (run->type == SKB_CONTENT_RUN_ICON) {
			// Icon
			render_draw_icon(rc, offset_x + run->bounds.x, offset_y + run->bounds.y,
				layout_params->icon_collection, run->icon_handle, run->bounds.width, run->bounds.height,
				color, alpha_mode);
		} else {
			// Text
			for (int32_t gi = run->glyph_range.start; gi < run->glyph_range.end; gi++) {
				const skb_glyph_t* glyph = &glyphs[gi];
				render_draw_glyph(rc, offset_x + glyph->offset_x, offset_y + glyph->offset_y,
					layout_params->font_collection, run->font_handle, glyph->gid, run->font_size,
					color, alpha_mode);
			}
		}
	}

	// Draw through lines.
	for (int32_t i = 0; i < decorations_count; i++) {
		const skb_decoration_t* decoration = &decorations[i];
		const skb_layout_run_t* run = &layout_runs[decoration->layout_run_idx];
		if (decoration->position == SKB_DECORATION_THROUGHLINE) {
			skb_color_t color = decoration->color;
			if (has_decoration_overrides)
				override_color(RENDER_OVERRIDE_DECORATION, run->content_run_id, &color, color_overrides);
			render_draw_decoration(rc, offset_x + decoration->offset_x, offset_y + decoration->offset_y,
				decoration->style, decoration->position, decoration->length, decoration->pattern_offset, decoration->thickness,
				color, alpha_mode);
		}
	}
}

void render_draw_layout_with_culling(render_context_t* rc, const skb_rect2_t view_bounds, float offset_x, float offset_y, const skb_layout_t* layout, skb_rasterize_alpha_mode_t alpha_mode)
{
	assert(rc);
	assert(layout);

	// Draw layout
	const skb_layout_params_t* layout_params = skb_layout_get_params(layout);
	const skb_layout_line_t* layout_lines = skb_layout_get_lines(layout);
	const int32_t layout_lines_count = skb_layout_get_lines_count(layout);
	const skb_layout_run_t* layout_runs = skb_layout_get_layout_runs(layout);
	const int32_t layout_runs_count = skb_layout_get_layout_runs_count(layout);
	const skb_glyph_t* glyphs = skb_layout_get_glyphs(layout);
	const int32_t decorations_count = skb_layout_get_decorations_count(layout);
	const skb_decoration_t* decorations = skb_layout_get_decorations(layout);

	const skb_vec2_t offset = { .x = offset_x, .y = offset_y };

	for (int32_t li = 0; li < layout_lines_count; li++) {
		const skb_layout_line_t* line = &layout_lines[li];
		if (!arb_rect2_overlap(view_bounds, skb_rect2_translate(line->culling_bounds, offset)))
			continue;

		// Draw underlines
		for (int32_t i = line->decorations_range.start; i < line->decorations_range.end; i++) {
			const skb_decoration_t* decoration = &decorations[i];
			if (decoration->position != SKB_DECORATION_THROUGHLINE) {
				render_draw_decoration(rc, offset_x + decoration->offset_x, offset_x + decoration->offset_y,
					decoration->style, decoration->position, decoration->length, decoration->pattern_offset, decoration->thickness,
					decoration->color, alpha_mode);
			}
		}

		// Draw glyphs
		for (int32_t ri = 0; ri < layout_runs_count; ri++) {
			const skb_layout_run_t* run = &layout_runs[ri];
			const skb_attribute_set_t run_attributes = skb_layout_get_layout_run_attributes(layout, run);
			const skb_attribute_fill_t attr_fill = skb_attributes_get_fill(run_attributes, layout_params->attribute_collection);

			if (run->type == SKB_CONTENT_RUN_OBJECT) {
				// Object
			} else if (run->type == SKB_CONTENT_RUN_ICON) {
				// Icon
				render_draw_icon(rc, offset_x + run->bounds.x, offset_y + run->bounds.y,
					layout_params->icon_collection, run->icon_handle, run->bounds.width, run->bounds.height,
					attr_fill.color, alpha_mode);
			} else {
				// Text
				for (int32_t gi = run->glyph_range.start; gi < run->glyph_range.end; gi++) {
					const skb_glyph_t* glyph = &glyphs[gi];

					skb_rect2_t coarse_glyph_bounds = line->common_glyph_bounds;
					coarse_glyph_bounds.x += glyph->offset_x;
					coarse_glyph_bounds.y += glyph->offset_y;
					if (!arb_rect2_overlap(view_bounds, skb_rect2_translate(coarse_glyph_bounds, offset)))
						continue;

					render_draw_glyph(rc, offset_x + glyph->offset_x, offset_y + glyph->offset_y,
						layout_params->font_collection, run->font_handle, glyph->gid, run->font_size,
						attr_fill.color, alpha_mode);
				}
			}
		}

		// Draw through lines.
		for (int32_t i = 0; i < decorations_count; i++) {
			const skb_decoration_t* decoration = &decorations[i];
			if (decoration->position == SKB_DECORATION_THROUGHLINE) {
				render_draw_decoration(rc, offset_x + decoration->offset_x, offset_y + decoration->offset_y,
					decoration->style, decoration->position, decoration->length, decoration->pattern_offset, decoration->thickness,
					decoration->color, alpha_mode);
			}
		}
	}
}



//
// OpengGL specific
//

static void gl__check_error(const char* str)
{
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		const char* err_str = "";
		switch (err) {
		case GL_INVALID_ENUM: err_str = "GL_INVALID_ENUM"; break;
		case GL_INVALID_VALUE: err_str = "GL_INVALID_VALUE"; break;
		case GL_INVALID_OPERATION: err_str = "GL_INVALID_OPERATION"; break;
		case GL_OUT_OF_MEMORY: err_str = "GL_OUT_OF_MEMORY"; break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: err_str = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
		default: err_str = ""; break;
		}
		skb_debug_log("Error %08x %s after %s\n", err, err_str, str);
		return;
	}
}

static void gl__print_program_log(GLuint program)
{
	if (!glIsProgram(program)) {
		skb_debug_log("Name %d is not a program\n", program);
		return;
	}

	int32_t max_length = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &max_length);

	char* info_log = skb_malloc(max_length);

	int32_t info_log_length = 0;
	glGetProgramInfoLog(program, max_length, &info_log_length, info_log);
	skb_debug_log("%s\n", info_log);

	skb_free(info_log);
}

static void gl__print_shader_log(GLuint shader)
{
	if (!glIsShader(shader)) {
		skb_debug_log("Name %d is not a shader\n", shader);
		return;
	}

	int32_t max_length = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_length);

	char* info_log = skb_malloc(max_length);

	int32_t info_log_length = 0;
	glGetShaderInfoLog(shader, max_length, &info_log_length, info_log);
	skb_debug_log("%s\n", info_log);

	skb_free(info_log);
}

static bool gl__init(render_context_t* rc)
{
	gl__check_error("init");

	GLint success = GL_FALSE;

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	const GLchar* vs_source = "#version 140\n"
		"in vec3 a_pos;\n"
		"in vec2 a_uv;\n"
		"in vec4 a_color;\n"
		"in vec2 a_atlas_pos;\n"
		"in vec2 a_atlas_size;\n"
		"in float a_scale;\n"
		"out vec4 f_color;\n"
		"out vec2 f_uv;\n"
		"out vec2 f_atlas_pos;\n"
		"out vec2 f_atlas_size;\n"
		"out float f_scale;\n"
		"uniform vec2 u_view_size;\n"
		"uniform vec2 u_tex_size;\n"
		"void main() {\n"
		"	f_color = a_color;\n"
		"	f_scale = a_scale;\n"
		"	f_uv = a_uv;\n"
		"	f_atlas_pos = a_atlas_pos / u_tex_size;\n"
		"	f_atlas_size = a_atlas_size / u_tex_size;\n"
		"	gl_Position = vec4(2.0*a_pos.x/u_view_size.x - 1.0, 1.0 - 2.0*a_pos.y/u_view_size.y, 0, 1);\n"
		"}\n";

	glShaderSource(vs, 1, &vs_source, NULL);
	glCompileShader(vs);

	glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
	if (success != GL_TRUE) {
		skb_debug_log("Unable to compile vertex shader.\n");
		gl__print_shader_log(vs);
		return 0;
	}

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	const GLchar* fs_source =
		"#version 140\n"
		"in vec4 f_color;\n"
		"in vec2 f_uv;\n"
		"in vec2 f_atlas_pos;\n"
		"in vec2 f_atlas_size;\n"
		"in float f_scale;\n"
		"out vec4 fragment;\n"
		"uniform sampler2D u_tex;\n"
		"uniform int u_tex_type;\n"
		"\n"
		"vec4 sdf(vec4 col) {\n"
		"	float d = (col.a - (128./255.)) / (32./255.);\n"
		"	float w = 0.8 / f_scale;\n"
		"	float a = 1.f - clamp((d + 0.5*w - 0.1) / w, 0., 1.);\n"
		"	return vec4(col.rgb * a, a); // premultiply\n"
		"}\n"
		"\n"
		"void main() {\n"
		"	vec4 color = vec4(f_color.rgb * f_color.a, f_color.a); // premult\n"
		"	// Texture region wrapping\n"
		"	vec2 uv = f_atlas_pos + fract(f_uv) * f_atlas_size;\n"
		"	// Sample texture and handle RGBA vs A\n"
		"	vec4 tex_color = vec4(1.);\n"
		"	if (u_tex_type == 1) {\n"
		"		// RGBA pre-multiplied\n"
		"		tex_color = texture(u_tex, uv);\n"
		"	} else if (u_tex_type == 2) {\n"
		"		// Alpha (make pre-multiplied white)\n"
		"		tex_color = vec4(texture(u_tex, uv).x);\n"
		"	} else if (u_tex_type == 3) {\n"
		"		// RGB SDF\n"
		"		tex_color = sdf(texture(u_tex, uv));\n"
		"	} else if (u_tex_type == 4) {\n"
		"		// SDF\n"
		"		tex_color = sdf(vec4(1.,1.,1.,texture(u_tex, uv).x));\n"
		"	}\n"
		"	fragment = tex_color * color;\n"
		"}\n";

	glShaderSource(fs, 1, &fs_source, NULL);
	glCompileShader(fs);

	glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
	if (success != GL_TRUE) {
		skb_debug_log("Unable to compile fragment shader.\n");
		gl__print_shader_log(fs);
		return false;
	}

	rc->program = glCreateProgram();
	glAttachShader(rc->program, vs);
	glAttachShader(rc->program, fs);

	glBindAttribLocation(rc->program, 0, "a_pos");
	glBindAttribLocation(rc->program, 1, "a_uv");
	glBindAttribLocation(rc->program, 2, "a_atlas_pos");
	glBindAttribLocation(rc->program, 3, "a_atlas_size");
	glBindAttribLocation(rc->program, 4, "a_color");
	glBindAttribLocation(rc->program, 5, "a_scale");

	glLinkProgram(rc->program);

	glGetProgramiv(rc->program, GL_LINK_STATUS, &success);
	if (success != GL_TRUE) {
		skb_debug_log("Error linking program %d!\n", rc->program);
		gl__print_program_log(rc->program);
		return false;
	}

	glDeleteShader(vs);
	glDeleteShader(fs);
	gl__check_error("link");

	glGenBuffers(1, &rc->vbo);
	gl__check_error("gen vbo");

	return true;
}

static void gl__destroy(render_context_t* rc)
{
	glDeleteBuffers(1, &rc->vbo);
	glDeleteProgram(rc->program);
	for (int32_t i = 0; i < rc->textures_count; i++) {
		render__texture_t* tex = &rc->textures[i];
		glDeleteTextures(1, &tex->tex_id);
	}
}

static void gl__flush(render_context_t* rc)
{
	gl__check_error("flush start");

	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premult alpha
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);

	gl__check_error("flush state");

	glUseProgram(rc->program);
	glUniform2f(glGetUniformLocation(rc->program, "u_view_size"), (float)rc->view_width, (float)rc->view_height);

	gl__check_error("prog");

	GLuint vao = 0;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	gl__check_error("vao");

	glBindBuffer(GL_ARRAY_BUFFER, rc->vbo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glEnableVertexAttribArray(4);
	glEnableVertexAttribArray(5);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(render__vertex_t), (GLvoid*)(0 + offsetof(render__vertex_t, pos)));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(render__vertex_t), (GLvoid*)(0 + offsetof(render__vertex_t, uv)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(render__vertex_t), (GLvoid*)(0 + offsetof(render__vertex_t, atlas_pos)));
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(render__vertex_t), (GLvoid*)(0 + offsetof(render__vertex_t, atlas_size)));
	glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(render__vertex_t), (GLvoid*)(0 + offsetof(render__vertex_t, color)));
	glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(render__vertex_t), (GLvoid*)(0 + offsetof(render__vertex_t, scale)));

	glBufferData(GL_ARRAY_BUFFER, rc->verts_count * sizeof(render__vertex_t), rc->verts, GL_STREAM_DRAW);

	gl__check_error("vbo");

	for (int32_t i = 0; i < rc->batches_count; i++) {
		const render__batch_t* b = &rc->batches[i];
		const render__texture_t* image_tex = renderer__find_texture(rc, b->image_id);
		const render__texture_t* sdf_tex = renderer__find_texture(rc, b->sdf_id);

		if (image_tex && image_tex->tex_id) {
			glUniform1i(glGetUniformLocation(rc->program, "u_tex_type"), image_tex->bpp == 4 ? 1: 2);
			glUniform2f(glGetUniformLocation(rc->program, "u_tex_size"), image_tex->width, image_tex->height);
			glUniform1i(glGetUniformLocation(rc->program, "u_tex"), 0);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, image_tex->tex_id);
			gl__check_error("texture image");
		} else if (sdf_tex && sdf_tex->tex_id) {
			glUniform1i(glGetUniformLocation(rc->program, "u_tex_type"), sdf_tex->bpp == 4 ? 3: 4);
			glUniform2f(glGetUniformLocation(rc->program, "u_tex_size"), sdf_tex->width, sdf_tex->height);
			glUniform1i(glGetUniformLocation(rc->program, "u_tex"), 0);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, sdf_tex->tex_id);
			gl__check_error("texture sdf");
		} else {
			glUniform1i(glGetUniformLocation(rc->program, "u_tex_type"), 0);
			glUniform2f(glGetUniformLocation(rc->program, "u_tex_size"), 1, 1);
			glUniform1i(glGetUniformLocation(rc->program, "u_tex"), 0);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, 0);
			gl__check_error("texture none");
		}

		glDrawArrays(GL_TRIANGLES, b->offset, b->count);
	}

	gl__check_error("draw");

	//    glUseProgram(0);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &vao);

	gl__check_error("end");

	rc->verts_count = 0;
	rc->batches_count = 0;
}

static void gl__create_texture(render__texture_t* tex, int32_t width, int32_t height, int32_t stride_bytes, const uint8_t* image, uint8_t bpp)
{
	// Delete old of exists.
	if (tex->tex_id) {
		glDeleteTextures(1, &tex->tex_id);
		tex->tex_id = 0;
	}

	tex->width = width;
	tex->height = height;
	tex->bpp = bpp;

	// upload image
	glGenTextures(1, &tex->tex_id);
	glBindTexture(GL_TEXTURE_2D, tex->tex_id);

	glPixelStorei(GL_UNPACK_ALIGNMENT,1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, stride_bytes / bpp);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

	assert(bpp == 4 || bpp == 1);
	if (bpp == 4)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
	else if (bpp == 1)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_2D, 0);

	gl__check_error("resize texture");
}

static void gl__update_texture(render__texture_t* tex, int32_t offset_x, int32_t offset_y, int32_t width, int32_t height, int32_t stride_bytes, const uint8_t* image)
{
	// upload image
	glBindTexture(GL_TEXTURE_2D, tex->tex_id);

	glPixelStorei(GL_UNPACK_ALIGNMENT,1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, stride_bytes / tex->bpp);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, offset_x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, offset_y);

	if (tex->bpp == 4)
		glTexSubImage2D(GL_TEXTURE_2D, 0, offset_x, offset_y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image);
	else if (tex->bpp == 1)
		glTexSubImage2D(GL_TEXTURE_2D, 0, offset_x, offset_y, width, height, GL_RED, GL_UNSIGNED_BYTE, image);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

	glBindTexture(GL_TEXTURE_2D, 0);

	gl__check_error("update texture");
}
