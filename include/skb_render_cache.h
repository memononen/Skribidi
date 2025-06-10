// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_RENDER_CACHE_H
#define SKB_RENDER_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "skb_common.h"
#include "skb_font_collection.h"
#include "skb_icon_collection.h"
#include "skb_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct skb_font_t skb_font_t;
typedef struct skb_font_collection_t skb_font_collection_t;
typedef struct skb_icon_t skb_icon_t;
typedef struct skb_layout_t skb_layout_t;

/**
 * @defgroup render_cache Render Cache
 *
 * The render cache is used to manage the different sizes of glyphs and icons needed to render the text.
 * The cache is used in two phases:
 * 1) Request glyphs
 * 2) Render glyphs and update changed textures
 *
 * During the first part, the cache tracks which glyphs are used, places the glyphs into one of the atlasses, and returns a quad describing the dimension of a rectangle to draw,
 * and what portion of an atlas image to draw. The data created during this phase is guaranteed to be valid until the end of the frame.
 *
 * The atlas supports multiple textures. Alpha and color glyphs both are laid out in different textures, and new texture is created if we run out of space in existing textures.
 * You can register to get notified when a new texture is created. The user should do just enough work in that callback to be able to handle the new image index returned with the quad.
 *
 * In the second phase we have a list of glyphs and icons that need to be rendered. Once rendered, we can iterate over the images to see which portions of the images need to update.
 *
 * @{
 */

/** Opaque type for the render cache. Use skb_render_cache_create() to create. */
typedef struct skb_render_cache_t skb_render_cache_t;

/** Enum describing flags for skb_render_quad_t. */
enum skb_render_quad_flags_t {
	/** The quad uses color texture. */
	SKB_RENDER_QUAD_IS_COLOR = 1 << 0,
	/** The quad uses SDF. */
	SKB_RENDER_QUAD_IS_SDF   = 1 << 1,
};

/** Quad representing a glyph or icon. */
typedef struct skb_render_quad_t {
	/** Geometry of the quad to render */
	skb_rect2_t geom_bounds;
	/** Portion of the image to map to the render bounds. */
	skb_rect2_t image_bounds;
	/** Scaling factor between bounds and image bounds. Can be used to adjust the width of the anti-alising gradient. */
	float scale;
	/** Cache image index of the image to draw. */
	uint8_t image_idx;
	/** Render quad flags (see skb_render_quad_flags_t). */
	uint8_t flags;
} skb_render_quad_t;

/**
 * Signature of the texture create callback.
 * @param cache pointer to the render cache.
 * @param image_idx index of the new image.
 * @param context context pointer provided for the render cachen when setting up the callback.
 */
typedef void skb_create_texture_func_t(skb_render_cache_t* cache, uint8_t image_idx, void* context);

/**
 * Configuration for rendering specific image type.
 * The render sizes are calculated by first applying any scaling (view_scale, etc), then snapping, then clamping.
 * This will be the requested size (font size, or icon size).
 * The requested size value is used to calculate the actual image or glyph size, which includes padding.
 */
typedef struct skb_render_image_config_t {
	/** The size is rounded up to the next multiple of rounding. */
	float rounding;
	/** Minimum size of a requested image. */
	float min_size;
	/** Maximum size of a requested image. */
	float max_size;
	/** How much padding to add around the image. */
	int32_t padding;
} skb_render_image_config_t;

/** Enum describing flags for skb_render_cache_config_t. */
enum skb_render_cache_config_flags_t {
	/** The space in atlas for removed items is cleared. This makes it easier to see which parts of the atlas are unused. */
	SKB_RENDER_CACHE_CONFIG_DEBUG_CLEAR_REMOVED = 1 << 0,
};

/**
 * Render cache configuration.
 * Tall atlas performs much better than wide, as it can support more size variations.
 */
typedef struct skb_render_cache_config_t {
	/** Initial width of a newly create atlas. Default: 1024 */
	int32_t atlas_init_width;
	/** Initial height of a newly create atlas: Default 1024 */
	int32_t atlas_init_height;
	/** Increment of how much atlas is grown when running out of space. Default 512. */
	int32_t atlas_expand_size;
	/** Maximum atlas width. Default 1024. */
	int32_t atlas_max_width;
	/** Maximum atlas height. Default 4096. */
	int32_t atlas_max_height;
	/** The height of an item added to the atlas is rounded up to a multiple of this value Allows better resue of the atlas rows. Default: 8. */
	int32_t atlas_item_height_rounding;
	/** How much bigger or smaller and item can be so it may get added to too big or too small row in the atlas. E.g. if row size is 20 and fit factor is 0.25, then items from 15 to 25 are considered to be added to the row. Default: 0.25.*/
	float atlas_fit_max_factor;
	/** Defines after which duration inactive items are removed from the cache. Each call to skb_render_cache_compact() bumps the counter. Default: 0.25. */
	int32_t evict_inactive_duration;
	/** Render cache config flags (see skb_render_cache_config_flags_t). */
	uint8_t flags;
	/** Image config for SDF glyphs */
	skb_render_image_config_t glyph_sdf;
	/** Image config for alpha glyphs */
	skb_render_image_config_t glyph_alpha;
	/** Image config for SDF icons */
	skb_render_image_config_t icon_sdf;
	/** Image config for alpha glyphs */
	skb_render_image_config_t icon_alpha;
} skb_render_cache_config_t;

/**
 * Creates a new render cache with specified config.
 * @param config configuration to use for the new render cache.
 * @return pointer to the created render cache.
 */
skb_render_cache_t* skb_render_cache_create(const skb_render_cache_config_t* config);

/**
 * Destroys a render cache created with skb_render_cache_create().
 * @param cache pointer to the render cache to destroy.
 */
void skb_render_cache_destroy(skb_render_cache_t* cache);

/**
 * Returns default values for render cache config. Useful if you only want to change some specific values.
 * @return render cache config default values.
 */
skb_render_cache_config_t skb_render_cache_get_default_config(void);

/**
 * Returns the config used to create the render cache.
 * @param cache render cache to use.
 * @return config for the specified render cache.
 */
skb_render_cache_config_t skb_render_cache_get_config(skb_render_cache_t* cache);

/**
 * Sets the texture creation callback of the render cache.
 * @param cache render cache to use.
 * @param create_texture_callback pointer to the callback function.
 * @param context pointer passed to the callback function each time it is called.
 */
void skb_render_cache_set_create_texture_callback(skb_render_cache_t* cache, skb_create_texture_func_t* create_texture_callback, void* context);

/** @return number of images in the render cache. */
int32_t skb_render_cache_get_image_count(skb_render_cache_t* cache);

/**
 * Returns image at specified index. See skb_render_cache_get_image_count() to get number of images.
 * @param cache render cachen to use.
 * @param index index of the image to get.
 * @return pointer to the spcified image.
 */
const skb_image_t* skb_render_cache_get_image(skb_render_cache_t* cache, int32_t index);

/**
 * Returns the bounding rect of the modified portion of the specified image. See skb_render_cache_get_image_count() to get number of images.
 * @param cache render cachen to use.
 * @param index index of the image to query.
 * @return bounding rect of the modified portion of the image.
 */
skb_rect2i_t skb_render_cache_get_image_dirty_bounds(skb_render_cache_t* cache, int32_t index);

/**
 * Returns the bounding rect of the modified portion of the specified image, and reset the modified portion.
 * See skb_render_cache_get_image_count() to get number of images.
 * @param cache render cachen to use.
 * @param index index of the image to query.
 * @return bounding rect of the modified portion of the image.
 */
skb_rect2i_t skb_render_cache_get_and_reset_image_dirty_bounds(skb_render_cache_t* cache, int32_t index);

/**
 * Sets user data for a spcified image.
 * See skb_render_cache_get_image_count() to get number of images.
 * @param cache render cache to use.
 * @param index index of the image.
 * @param user_data user date to store.
 */
void skb_render_cache_set_image_user_data(skb_render_cache_t* cache, int32_t index, uintptr_t user_data);

/**
 * Gets user data set for specified image.
 * See skb_render_cache_set_image_user_data() to set the user data.
 * @param cache render cache to use.
 * @param index index of the image.
 * @return user data.
 */
uintptr_t skb_render_cache_get_image_user_data(skb_render_cache_t* cache, int32_t index);

/**
 * Signature of rectangle iterator functions.
 * @param x x location of the rectangle in the image.
 * @param y y location of the rectangle in the image.
 * @param width with of the rectangle.
 * @param height height of the rectangle.
 * @param context context that was passed to the iterator function.
 */
typedef void skb_debug_rect_iterator_func_t(int32_t x, int32_t y, int32_t width, int32_t height, void* context);

/**
 * Iterates all the free space in specified image. Used for debugging.
 * @param cache render cache to use.
 * @param index image to query.
 * @param callback callback that is called for each free space rectangle in the atlas.
 * @param context contest pointer passed to the callback.
 */
void skb_render_cache_debug_iterate_free_rects(skb_render_cache_t* cache, int32_t index, skb_debug_rect_iterator_func_t* callback, void* context);

/**
 * Iterates over all the used rectangles in a specific image. Used for debugging.
 * @param cache render cache to use.
 * @param index image to query.
 * @param callback callback that is called for each used rectangle in the atlas.
 * @param context contest pointer passed to the callback.
 */
void skb_render_cache_debug_iterate_used_rects(skb_render_cache_t* cache, int32_t index, skb_debug_rect_iterator_func_t* callback, void* context);

/**
 * Returns previous non-empty dirty bounds of the specified image. Can be used to visualize the last update region.
 * @param cache render cache to use.
 * @param index index of the image.
 * @return previous dirty rectangle.
 */
skb_rect2i_t skb_render_cache_debug_get_prev_dirty_bounds(skb_render_cache_t* cache, int32_t index);


/**
 * Get a quad representing the geometry and image portion of the specified glyph.
 *
 * The pixel scale is used to control the ratio between glyph geometry size and image size.
 * For example, if font_size 12 is requested, and pixel_scale is 2, then the geometry of the quad is based on 12 units, but the requested image will be twice the size.
 * This is useful for cases where geometry will later go through a separate transformation process, and we want to match the pixel density.
 *
 * The function will return an existing glyph or request a new glyph to be rendered if one does not exist.
 *
 * @param cache render cache to use
 * @param x position x to render the glyph at.
 * @param y position y to render the glyph at.
 * @param pixel_scale the size of a pixel compared to the geometry.
 * @param font_collection font collection to use.
 * @param font_handle handle to the font in the font collection.
 * @param glyph_id glyph id to render.
 * @param font_size font size.
 * @param alpha_mode whether to render the glyph as SDF or alpha mask.
 * @return quad representing the geometry to render, and portion of an image to use.
 */
skb_render_quad_t skb_render_cache_get_glyph_quad(
	skb_render_cache_t* cache, float x, float y, float pixel_scale,
	skb_font_collection_t* font_collection, skb_font_handle_t font_handle, uint32_t glyph_id, float font_size, skb_render_alpha_mode_t alpha_mode);

/**
 * Get a quad representing the geometry and image portion of the specified icon.
 *
 * The pixel scale is used to control the ratio between icon geometry size and image size.
 * For example, if icon of size 20 is requested, and pixel_scale is 2, then the geometry of the quad is based on the 20 units, but the requested image will be twice the size.
 * This is useful for cases where geometry will later go through a separate transformation process, and we want to match the pixel density.
 *
 * The function will return an existing icon or request a new icon to be rendered if one does not exist.
 *
 * @param cache render cache to use.
 * @param x position x to render the glyph at.
 * @param y position y to render the glyph at.
 * @param pixel_scale the size of a pixel compared to the geometry.
 * @param icon_collection icon collection to use.
 * @param icon_handle handle to icon in the icon collection.
 * @param icon_scale scale of the icon to render.
 * @param alpha_mode whether to render the icon as SDF or alpha mask.
 * @return quad representing the geometry to render, and portion of an image to use.
 */
skb_render_quad_t skb_render_cache_get_icon_quad(
	skb_render_cache_t* cache, float x, float y, float pixel_scale,
	const skb_icon_collection_t* icon_collection, skb_icon_handle_t icon_handle, skb_vec2_t icon_scale, skb_render_alpha_mode_t alpha_mode);

/**
 * Compacts the render cache based on usage.
 * Glyphs or icons in the cache that have not been queried for number of frames are removed (see config evict_inactive_duration).
 * This function should be called early in the frame so that we can free space for any new items that get requested during the frame.
 * @param cache render cache to use.
 * @return true if any items were removed.
 */
bool skb_render_cache_compact(skb_render_cache_t* cache);

/**
 * Rasterizes glyphs and icons that have been requested.
 * This function should be called after all the glyphs and icons have been requested.
 *
 * If the function returns true, you can use skb_render_cache_get_image_count() and skb_render_cache_get_and_reset_image_dirty_bounds() iterate
 * over all the images and see which ones, and what portions need to be uploaded to the GPU.
 *
 * @param cache cache to use.
 * @param temp_alloc temp alloc to use during rasterization.
 * @param renderer renderer to use during rasterization.
 * @return true if any images were changed.
 */
bool skb_render_cache_rasterize_missing_items(skb_render_cache_t* cache, skb_temp_alloc_t* temp_alloc, skb_renderer_t* renderer);

/** @} */

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // SKB_RENDER_CACHE_H
