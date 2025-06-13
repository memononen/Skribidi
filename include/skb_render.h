// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_RENDERER_H
#define SKB_RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include "skb_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct skb_font_t skb_font_t;
typedef struct skb_icon_t skb_icon_t;

/**
 * @defgroup renderer Renderer
 *
 * The renderer is used to rasterize glyphs and icons. It holds some internal state that is needed for rasterization.
 * It's not thread safe, each thread should hold their own renderer and temp allocator.
 *
 * The render allows icons and glyphs to be rendered with signed distance field (SDF) or mask as alpha channel.
 * The SDF allows the images to be rendered at different sizes while maintaining scrisp outline.
 * Color images can also be rendered with SDF alpha channel, in which case the colors inside the image are interpolated, but the outline can be crisp.
 *
 * The following code is used to convert the floating point distance field to 8-bit alpha channel:
 * ```
 *  uint8_t alpha = clamp(on_edge_value + distance * pixel_dist_scale, 0, 255);
 * ```
 * The _on_edge_value_ defines the location of SDF zero in 8-bit alpha, and _pixel_dist_scale_ defines the resolution. It allows to tune how much of the SDF range is inside or outside of the image.
 * Smaller values of _pixel_dist_scale_ will cause jagginess when the SDF image is scaled, and larger values will reduce the range of the SDF (e.g. when used for effects).
 * See skb_renderer_config_t.
 *
 * @{
 */

/** Opaque type for the renderer. Use skb_renderer_create() to create. */
typedef struct skb_renderer_t skb_renderer_t;

/** Renderer configuration. */
typedef struct skb_renderer_config_t {
	/** Defines the zero of the SDF when converted to alpha [0..255]. Default: 128 */
	uint8_t on_edge_value;
	/** Defines the scale of one SDF pixel when converted to alpha [0...255]. Default: 32.0f */
	float pixel_dist_scale;
} skb_renderer_config_t;

/** Enum describing how the alpha channel should be created. */
typedef enum {
	SKB_RENDER_ALPHA_MASK, /**< Render alpha channel as mask. */
	SKB_RENDER_ALPHA_SDF, /**< Render alpha channel as sign distance field. */
} skb_render_alpha_mode_t;

/**
 * Creates a renderer.
 * @param config pointer to renderer configuration.
 * @return pointer to the created renderer.
 */
skb_renderer_t* skb_renderer_create(skb_renderer_config_t* config);

/**
 * Returns default values for the renderer config. Can be used if you only want to modify a specific value.
 * @return default config.
 */
skb_renderer_config_t skb_renderer_get_default_config(void);

/**
 * Returns the config the renderer was initialized with.
 * @param renderer pointer to the renderer
 * @return config for the specified renderer.
 */
skb_renderer_config_t skb_renderer_get_config(const skb_renderer_t* renderer);

/**
 * Destroys a renderer previously created using skb_renderer_create().
 * @param renderer pointer to the renderer.
 */
void skb_renderer_destroy(skb_renderer_t* renderer);

/**
 * Calculates the dimensions required to render a specific glyph at spcified size.
 * The width and height of the returned rectangle defines the image size, and origin defines the offset the glyph should be rendered at.
 * @param glyph_id glyph id to render
 * @param font font used for rendering
 * @param font_size font size
 * @param padding padding to leave around the glyph.
 * @return rect describing size and offset required to render the glyph.
 */
skb_rect2i_t skb_render_get_glyph_dimensions(uint32_t glyph_id, const skb_font_t* font, float font_size, int32_t padding);

/**
 * Rasterizes a glyph as alpha.
 * The offset and render target size can be obtained using skb_render_get_glyph_dimensions().
 * @param renderer pointer to renderer.
 * @param temp_alloc pointer to temp alloc used during the rendering.
 * @param glyph_id glyph id to rasterize.
 * @param font font.
 * @param font_size font size .
 * @param alpha_mode alpha mode, defines if the alpha channel of the result is SDF or alpha mask.
 * @param offset_x offset x where to render the glyph.
 * @param offset_y offset y where to render the glyph.
 * @param target target image to render to. The image must be 1 byte-per-pixel.
 * @return true of the rasterization succeeded.
 */
bool skb_render_rasterize_alpha_glyph(
	skb_renderer_t* renderer, skb_temp_alloc_t* temp_alloc,
	uint32_t glyph_id, const skb_font_t* font, float font_size, skb_render_alpha_mode_t alpha_mode,
	float offset_x, float offset_y, skb_image_t* target);

/**
 * Rasterizes a glyph as RGBA.
 * The offset and render target size can be obtained using skb_render_get_glyph_dimensions().
 * @param renderer pointer to renderer.
 * @param temp_alloc pointer to temp alloc used during the rendering.
 * @param glyph_id glyph id to rasterize.
 * @param font font.
 * @param font_size font size .
 * @param alpha_mode alpha mode, defines if the alpha channel of the result is SDF or alpha mask.
 * @param offset_x offset x where to render the glyph.
 * @param offset_y offset y where to render the glyph.
 * @param target target image to render to. The image must be 4 bytes-per-pixel.
 * @return true of the rasterization succeeded.
 */
bool skb_render_rasterize_color_glyph(
	skb_renderer_t* renderer, skb_temp_alloc_t* temp_alloc,
	uint32_t glyph_id, const skb_font_t* font, float font_size, skb_render_alpha_mode_t alpha_mode,
	int32_t offset_x, int32_t offset_y, skb_image_t* target);

/**
 * Calculates the dimensions required to render a specific icon at spcified size.
 * The width and height of the returned rectangle defines the image size, and origin defines the offset the icon should be rendered at.
 * @param icon icon to render.
 * @param icon_scale icon scale (see skb_render_calc_proportional_icon_scale()).
 * @param padding padding to leave around the icon.
 * @return rect describing size and offset required to render the icon.
 */
skb_rect2i_t skb_render_get_icon_dimensions(const skb_icon_t* icon, skb_vec2_t icon_scale, int32_t padding);

/**
 * Rasterizes an icon as RGBA.
 * The offset and render target size can be obtained using skb_render_get_glyph_dimensions().
 * @param renderer pointer to renderer.
 * @param temp_alloc pointer to temp alloc used during the rendering.
 * @param icon icon to render.
 * @param icon_scale scale of the icon.
 * @param alpha_mode alpha mode, defines if the alpha channel of the result is SDF or alpha mask.
 * @param offset_x offset x where to render the icon.
 * @param offset_y offset y where to render the icon.
 * @param target target image to render to. The image must be 4 bytes-per-pixel.
 * @return true of the rasterization succeeded.
 */
bool skb_render_rasterize_icon(
	skb_renderer_t* renderer, skb_temp_alloc_t* temp_alloc,
	const skb_icon_t* icon, skb_vec2_t icon_scale, skb_render_alpha_mode_t alpha_mode,
	int32_t offset_x, int32_t offset_y, skb_image_t* target);

/** @} */

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // SKB_RENDERER_H
