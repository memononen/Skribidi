// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>
#include "skb_common.h"
#include "skb_image_atlas.h"

typedef struct render_context_t render_context_t;
typedef struct skb_image_atlas_t skb_image_atlas_t;
typedef struct skb_rasterizer_t skb_rasterizer_t;
typedef struct skb_layout_t skb_layout_t;
typedef struct skb_editor_t skb_editor_t;

/** Vertex used by render_draw_tris. */
typedef struct render_vert_t {
	skb_vec2_t pos;
	skb_color_t col;
} render_vert_t;

/**
 * Creates new render context.
 * @param config config for image atlas.
 * @return new render context.
 */
render_context_t* render_create(const skb_image_atlas_config_t* config);

/**
 * Destroys render context and frees resources.
 * @param rc render context.
 */
void render_destroy(render_context_t* rc);

/** @returns image atlas associated with the render context. */
skb_image_atlas_t* render_get_atlas(render_context_t* rc);

/** @returns rasterizer associated with the render context. */
skb_rasterizer_t* render_get_rasterizer(render_context_t* rc);

/** @returns temp allocator associated with the render context. */
skb_temp_alloc_t* render_get_temp_alloc(render_context_t* rc);

/**
 * Initializes the render context to draw a new frame. Compacts image atlas.
 * @param rc render context
 * @param view_width width of the view to render to in pixels
 * @param view_height height of the view to render to in pixels
 */
void render_begin_frame(render_context_t* rc, int32_t view_width, int32_t view_height);

/**
 * Ends frame, and renders all the submitted geometry. Updates image atlas to textures, if not already called.
 * @param rc render context.
 */
void render_end_frame(render_context_t* rc);

/**
 * Clears the image atlas and associated textures.
 * @param rc render context
 * @param config
 */
void render_reset_atlas(render_context_t* rc, const skb_image_atlas_config_t* config);

/**
 * Updates image atlas, rasterizes missing items and copies the data to GPU.
 * If no called during frame, render_end_frame() will call this.
 * @param rc render atlas.
 */
void render_update_atlas(render_context_t* rc);

/**
 * Pushes rendering transform.
 * @param rc render context
 * @param offset_x offset x
 * @param offset_y offset y
 * @param scale view scale.
 */
void render_push_transform(render_context_t* rc, float offset_x, float offset_y, float scale);

/**
 * Pops rendering transform.
 * @param rc render context.
 */
void render_pop_transform(render_context_t* rc);

/** @returns scale of the current trasnform. */
float render_get_transform_scale(render_context_t* rc);

/** @returns offset of the current trasnform. */
skb_vec2_t render_get_transform_offset(render_context_t* rc);

/** @returns rectangle transformed by the current transform. */
skb_rect2_t render_transform_rect(render_context_t* rc, const skb_rect2_t rect);

/** @returns rectangle transformed by the inverse of the current transform. */
skb_rect2_t render_inv_transform_rect(render_context_t* rc, const skb_rect2_t rect);

/**
 * Creates new texture and returns handle to it.
 * @param rc render context.
 * @param img_width texture width
 * @param img_height texture height
 * @param img_stride_bytes image_data stride in bytes.
 * @param img_data pointer to image data (optional)
 * @param bpp bits per pixel, 4 (RGBA) or 1 (ALPHA)
 * @return texture handle, 0 if failed.
 */
uint32_t render_create_texture(render_context_t* rc,
                               int32_t img_width, int32_t img_height, int32_t img_stride_bytes, const uint8_t* img_data, uint8_t bpp);

/**
 * Updates existing texture.
 * If image data size does not match the new data, new GPU texture is created, but the texture handle will remain.
 * @param rc render context
 * @param tex_handle handle to the texture to update
 * @param offset_x offset x of subregion of image to update
 * @param offset_y offset y of subregion of image to update
 * @param width width of subregion of image to update
 * @param height height of subregion of image to update
 * @param img_width image data width
 * @param img_height image data height
 * @param img_stride_bytes image data stride in bytes
 * @param img_data pointer to image data
 */
void render_update_texture(render_context_t* rc,
                           uint32_t tex_handle, int32_t offset_x, int32_t offset_y, int32_t width, int32_t height,
                           int32_t img_width, int32_t img_height, int32_t img_stride_bytes, const uint8_t* img_data);

/**
 * Draws solid color debug triangles.
 * @param rc render context
 * @param verts pointer to array of vertices to draw, 3 consequtive vertices form a triangle.
 * @param verts_count number of vertices to draw.
 */
void render_draw_debug_tris(render_context_t* rc, const render_vert_t* verts, int32_t verts_count);

/**
 * Draws a textured quad.
 * Note: The quad should be created using the image atlas of the render context.
 * @param rc render context.
 * @param quad pointer to quad to draw.
 */
void render_draw_quad(render_context_t* rc, const skb_quad_t* quad);

/**
 * Draws a glyph.
 * @param rc render context
 * @param offset_x x offset of the glyph to render
 * @param offset_y y offset of the glyph to render
 * @param font_collection font colletion to use
 * @param font_handle handle to the font in font collection
 * @param glyph_id glyph id in the font
 * @param font_size font size
 * @param color color of the glyph
 * @param alpha_mode whether to render as SDF or alpha mask.
 */
void render_draw_glyph(render_context_t* rc,
                       float offset_x, float offset_y,
                       skb_font_collection_t* font_collection, skb_font_handle_t font_handle, uint32_t glyph_id, float font_size,
                       skb_color_t color, skb_rasterize_alpha_mode_t alpha_mode);

/**
 * Draws and icon
 * @param rc render context
 * @param offset_x x offset of the icon to render
 * @param offset_y y offset of the icon to render
 * @param icon_collection icon collection to use
 * @param icon_handle handle to the icon to render
 * @param width requested with, if SKB_SIZE_AUTO the width will be calculated from height keeping aspect ratio.
 * @param height requested with, if SKB_SIZE_AUTO the height will be calculated from width keeping aspect ratio.
 * @param color color of the icon
 * @param alpha_mode whether to render as SDF or alpha mask.
 */
void render_draw_icon(render_context_t* rc,
                      float offset_x, float offset_y,
                      skb_icon_collection_t* icon_collection, skb_icon_handle_t icon_handle, float width, float height,
                      skb_color_t color, skb_rasterize_alpha_mode_t alpha_mode);

/**
 * Draws a text decoration
 * @param rc render context
 * @param offset_x x offset to render the glyph at.
 * @param offset_y y offset to render the glyph at.
 * @param position vertical position, used to align the quad relative to the y (e.g. throughline pattern is center aligned)
 * @param style style of the decoration to render.
 * @param length length of the decoration line to render (in same units as x and y).
 * @param pattern_offset offset of the pattern (in same units as x and y).
 * @param thickness thickness of the decoration to render.
 * @param color color of the decoration
 * @param alpha_mode whether to render the pattern as SDF or alpha mask.
 */
void render_draw_decoration(render_context_t* rc,
                            float offset_x, float offset_y,
                            skb_decoration_style_t style, skb_decoration_position_t position, float length, float pattern_offset, float thickness,
                            skb_color_t color, skb_rasterize_alpha_mode_t alpha_mode);

/**
 * Draws text layout
 * @param rc render context
 * @param layout layout to draw
 * @param alpha_mode whether to render as SDF or alpha mask.
 */
void render_draw_layout(render_context_t* rc, float offset_x, float offset_y, const skb_layout_t* layout, skb_rasterize_alpha_mode_t alpha_mode);


typedef enum {
	RENDER_OVERRIDE_FILL,
	RENDER_OVERRIDE_DECORATION,
} render_override_type_t;

typedef struct render_override_t {
	intptr_t content_id;
	skb_color_t color;
	uint8_t type;
} render_override_t;

typedef struct render_override_slice_t {
	const render_override_t* items;
	int32_t count;
} render_override_slice_t;

#define RENDER_OVERRIDE_SLICE_FROM_ARRAY(array) skb_new(render_override_slice_t) { .items = (array), .count = SKB_COUNTOF(array) }

render_override_t render_color_override_make_fill(intptr_t content_data, skb_color_t color);
render_override_t render_color_override_make_decoration(intptr_t content_data, skb_color_t color);

void render_draw_layout_with_color_overrides(render_context_t* rc, float offset_x, float offset_y, const skb_layout_t* layout, skb_rasterize_alpha_mode_t alpha_mode, render_override_slice_t color_overrides);

/**
 * Draws text layout with culling.
 * Items (glyphs, icons, objects) that fall completely outside the viewport will not be drawn nor requested from the image atlas.
 * The viewport is in the same coordinate space as the layout.
 * @param rc render context
 * @param view_bounds view bounds to cull the items against.
 * @param layout layout to draw
 * @param alpha_mode whether to render as SDF or alpha mask.
 */
void render_draw_layout_with_culling(render_context_t* rc, const skb_rect2_t view_bounds, float offset_x, float offset_y, const skb_layout_t* layout, skb_rasterize_alpha_mode_t alpha_mode);


#endif // RENDERER_H
