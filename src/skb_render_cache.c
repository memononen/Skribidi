// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "skb_render_cache.h"

#include "skb_common_internal.h"
#include "skb_font_collection.h"
#include "skb_font_collection_internal.h"
#include "skb_icon_collection_internal.h"
#include "skb_render.h"

#include <assert.h>
#include <string.h>

#include "hb.h"
#include "hb-ot.h"


enum {
	SKB_ATLAS_NULL = 0xffff,
};

// Atlas row flags
enum skb__atlas_row_flags_t {
	SKB__ATLAS_ROW_IS_EMPTY = 1 << 0,
	SKB__ATLAS_ROW_IS_FREED = 1 << 1,
};

typedef struct skb__atlas_row_t {
	uint16_t y;
	uint16_t height;
	uint16_t base_height;
	uint16_t max_diff;
	uint16_t max_empty_item_width;
	uint16_t first_item;
	uint16_t next;
	uint8_t flags;
} skb__atlas_row_t;

// Atlas item flags
enum skb__atlas_item_flags_t {
	SKB__ATLAS_ITEM_IS_EMPTY = 1 << 0,
	SKB__ATLAS_ITEM_IS_FREED = 1 << 1,
};

typedef struct skb__atlas_item_t {
	uint16_t x;
	uint16_t width;
	uint16_t next;
	uint16_t generation;
	uint16_t row;
	uint8_t flags;
} skb__atlas_item_t;

typedef struct skb__atlas_handle_t {
	uint16_t index;
	uint16_t generation;
} skb__atlas_handle_t;

typedef struct skb__atlas_t {

	int32_t width;
	int32_t height;

	int32_t occupancy;

	skb__atlas_row_t* rows;
	int32_t rows_count;
	int32_t rows_cap;

	skb__atlas_item_t* items;
	int32_t items_count;
	int32_t items_cap;

	uint16_t first_row;
	uint16_t row_freelist;
	uint16_t item_freelist;

} skb__atlas_t;

static void skb__atlas_init(skb__atlas_t* atlas, int32_t width, int32_t height);
static void skb__atlas_destroy(skb__atlas_t* atlas);


typedef enum {
	SKB_RENDER_CACHE_ITEM_REMOVED = 0,
	SKB_RENDER_CACHE_ITEM_INITIALIZED,
	SKB_RENDER_CACHE_ITEM_RASTERIZED,
} skb__render_cache_item_state_t;

// Cached glyph flags
enum skb__cached_glyph_flags_t {
	SKB__CACHED_GLYPH_IS_COLOR = 1 << 0,
	SKB__CACHED_GLYPH_IS_SDF   = 1 << 1,
};

typedef struct skb__cached_glyph_t {
	const skb_font_t* font;
	uint64_t hash_id;
	uint32_t gid;
	float clamped_font_size;
	skb__atlas_handle_t atlas_handle;
	int32_t last_access_stamp;
	skb_list_item_t lru;
	int16_t width;
	int16_t height;
	int16_t pen_offset_x;
	int16_t pen_offset_y;
	int16_t atlas_offset_x;
	int16_t atlas_offset_y;
	uint8_t state;
	uint8_t flags;
	uint8_t texture_idx;
} skb__cached_glyph_t;

// Cached icon flags
enum skb__cached_icon_flags_t {
	SKB__CACHED_ICON_IS_COLOR = 1 << 0,
	SKB__CACHED_ICON_IS_SDF   = 1 << 1,
};

typedef struct skb__cached_icon_t {
	const skb_icon_t* icon;
	uint64_t hash_id;
	skb_vec2_t icon_scale;
	skb__atlas_handle_t atlas_handle;
	int32_t last_access_stamp;
	skb_list_item_t lru;
	int16_t width;
	int16_t height;
	int16_t pen_offset_x;
	int16_t pen_offset_y;
	int16_t atlas_offset_x;
	int16_t atlas_offset_y;
	uint8_t state;
	uint8_t flags;
	uint8_t texture_idx;
} skb__cached_icon_t;

// Note: atlas size is always up to date, image gets resized during rasterization.
typedef struct skb_atlas_image_t {
	skb_image_t image;
	skb__atlas_t atlas;
	skb_rect2i_t dirty_bounds;
	skb_rect2i_t prev_dirty_bounds;
	uint8_t index;
	uintptr_t user_data;
} skb_atlas_image_t;

typedef struct skb_render_cache_t {
	bool initialized;
	bool valid;

	skb_atlas_image_t* images;
	int32_t images_count;
	int32_t images_cap;

	skb_hash_table_t* glyphs_lookup;
	skb__cached_glyph_t* glyphs;
	int32_t glyphs_count;
	int32_t glyphs_cap;
	int32_t glyphs_freelist;
	skb_list_t glyphs_lru;
	bool has_new_glyphs;

	skb_hash_table_t* icons_lookup;
	skb__cached_icon_t* icons;
	int32_t icons_count;
	int32_t icons_cap;
	int32_t icons_freelist;
	skb_list_t icons_lru;
	bool has_new_icons;

	int32_t now_stamp;
	int32_t last_evicted_stamp;

	skb_render_cache_config_t config;
	skb_create_texture_func_t* create_texture_callback;
	void* create_texture_callback_context;

} skb_render_cache_t;

static skb_list_item_t* skb__get_glyph(int32_t item_idx, void* context)
{
	skb_render_cache_t* cache = (skb_render_cache_t*)context;
	return &cache->glyphs[item_idx].lru;
}

static skb_list_item_t* skb__get_icon(int32_t item_idx, void* context)
{
	skb_render_cache_t* cache = (skb_render_cache_t*)context;
	return &cache->icons[item_idx].lru;
}

static void skb__image_resize(skb_image_t* image, int32_t new_width, int32_t new_height, uint8_t new_bpp)
{
	uint8_t* new_buffer = skb_malloc(new_width * new_height * new_bpp);
	memset(new_buffer, 0xff, new_width * new_height * new_bpp);

	int32_t new_stride_bytes = new_width * new_bpp;
	// Copy old
	if (image->buffer) {
		if (image->bpp == new_bpp) {
			const int32_t min_width = skb_mini(image->width, new_width);
			const int32_t min_height = skb_mini(image->height, new_height);
			for (int32_t y = 0; y < min_height; y++)
				memcpy(&new_buffer[y * new_stride_bytes], &image->buffer[y * image->stride_bytes], min_width * image->bpp);
		}
		skb_free(image->buffer);
	}
	image->buffer = new_buffer;
	image->width = new_width;
	image->height = new_height;
	image->stride_bytes = new_stride_bytes;
	image->bpp = new_bpp;
}

static void skb__image_destroy(skb_image_t* image)
{
	if (!image) return;
	skb_free(image->buffer);
}

static void skb__atlas_image_init(skb_atlas_image_t* atlas_image, uint8_t index, int32_t width, int32_t height, int32_t bpp)
{
	memset(atlas_image, 0, sizeof(*atlas_image));
	atlas_image->index = index;

	skb__image_resize(&atlas_image->image, width, height, bpp);
	skb__atlas_init(&atlas_image->atlas, width, height);

	atlas_image->prev_dirty_bounds = (skb_rect2i_t){0};
	atlas_image->dirty_bounds = (skb_rect2i_t){ 0, 0, width, height };
}

static void skb__atlas_image_destroy(skb_atlas_image_t* atlas_image)
{
	if (!atlas_image) return;
	skb__image_destroy(&atlas_image->image);

	skb__atlas_destroy(&atlas_image->atlas);

	memset(atlas_image, 0, sizeof(*atlas_image));
}


skb_render_cache_t* skb_render_cache_create(const skb_render_cache_config_t* config)
{
	skb_render_cache_t* cache = skb_malloc(sizeof(skb_render_cache_t));
	memset(cache, 0, sizeof(skb_render_cache_t));

	cache->glyphs_lookup = skb_hash_table_create();
	cache->icons_lookup = skb_hash_table_create();

	cache->glyphs_freelist = SKB_INVALID_INDEX;
	cache->glyphs_lru = skb_list_make();

	cache->icons_freelist = SKB_INVALID_INDEX;
	cache->icons_lru = skb_list_make();

	if (config)
		cache->config = *config;
	else
		cache->config = skb_render_cache_get_default_config();

	return cache;
}

void skb_render_cache_destroy(skb_render_cache_t* cache)
{
	if (!cache) return;

	for (int32_t i = 0; i < cache->images_count; i++)
		skb__atlas_image_destroy(&cache->images[i]);
	skb_free(cache->images);

	skb_hash_table_destroy(cache->glyphs_lookup);
	skb_free(cache->glyphs);
	skb_hash_table_destroy(cache->icons_lookup);
	skb_free(cache->icons);

	memset(cache, 0, sizeof(skb_render_cache_t));

	skb_free(cache);
}

static int32_t skb__round_up(int32_t x, int32_t n)
{
	return ((x + n-1) / n) * n;
}

static skb_atlas_image_t* skb__render_cache_add_atlas_image(skb_render_cache_t* cache, int32_t desired_width, int32_t desired_height, int32_t bpp)
{
	assert(bpp == 4 || bpp == 1);

	SKB_ARRAY_RESERVE(cache->images, cache->images_count+1);
	int32_t image_idx = cache->images_count++;
	assert(image_idx <= 255); // using uint8_t to store image_id

	desired_width = skb_mini(skb_maxi(skb__round_up(desired_width, cache->config.atlas_expand_size), cache->config.atlas_init_width), cache->config.atlas_max_width);
	desired_height = skb_mini(skb_maxi(skb__round_up(desired_height, cache->config.atlas_expand_size), cache->config.atlas_init_height), cache->config.atlas_max_height);

	skb_atlas_image_t* atlas_image = &cache->images[image_idx];
	skb__atlas_image_init(atlas_image, (uint8_t)image_idx, desired_width, desired_height, bpp);

	if (cache->create_texture_callback)
		cache->create_texture_callback(cache, (uint8_t)image_idx, cache->create_texture_callback_context);

	return atlas_image;
}

skb_render_cache_config_t skb_render_cache_get_default_config(void)
{
	return (skb_render_cache_config_t)  {
		.atlas_init_width = 1024,
		.atlas_init_height = 1024,
		.atlas_expand_size = 512,
		.atlas_max_width = 1024,
		.atlas_max_height = 4096,
		.atlas_item_height_rounding = 8,
		.atlas_fit_max_factor = 0.25f,
		.evict_inactive_duration = 10,
		.glyph_sdf = {
			.padding = 8,
			.rounding = 16.f,
			.min_size = 32.f,
			.max_size = 256.f,
		} ,
		.glyph_alpha = {
			.padding = 2,
			.rounding = 1.f,
			.min_size = 8.f,
			.max_size = 256.f,
		},
		.icon_sdf = {
			.padding = 8,
			.rounding = 16.f,
			.min_size = 32.f,
			.max_size = 256.f,
		},
		.icon_alpha = {
			.padding = 2,
			.rounding = 1.f,
			.min_size = 8.f,
			.max_size = 256.f,
		},
	};
}

skb_render_cache_config_t skb_render_cache_get_config(skb_render_cache_t* cache)
{
	assert(cache);
	return cache->config;
}

void skb_render_cache_set_create_texture_callback(skb_render_cache_t* cache, skb_create_texture_func_t* create_texture_callback, void* context)
{
	assert(cache);
	cache->create_texture_callback = create_texture_callback;
	cache->create_texture_callback_context = context;
}

int32_t skb_render_cache_get_image_count(skb_render_cache_t* cache)
{
	assert(cache);
	return cache->images_count;
}

const skb_image_t* skb_render_cache_get_image(skb_render_cache_t* cache, int32_t index)
{
	assert(cache);
	assert(index >= 0 && index < cache->images_count);

	return &cache->images[index].image;
}

skb_rect2i_t skb_render_cache_get_image_dirty_bounds(skb_render_cache_t* cache, int32_t index)
{
	assert(cache);
	assert(index >= 0 && index < cache->images_count);

	return cache->images[index].dirty_bounds;
}

skb_rect2i_t skb_render_cache_get_and_reset_image_dirty_bounds(skb_render_cache_t* cache, int32_t index)
{
	assert(cache);
	assert(index >= 0 && index < cache->images_count);

	const skb_rect2i_t bounds = cache->images[index].dirty_bounds;

	if (!skb_rect2i_is_empty(bounds))
		cache->images[index].prev_dirty_bounds = bounds;

	cache->images[index].dirty_bounds = skb_rect2i_make_undefined();

	return bounds;
}

uintptr_t skb_render_cache_get_image_user_data(skb_render_cache_t* cache, int32_t index)
{
	assert(cache);
	assert(index >= 0 && index < cache->images_count);

	return cache->images[index].user_data;
}

void skb_render_cache_set_image_user_data(skb_render_cache_t* cache, int32_t index, uintptr_t user_data)
{
	assert(cache);
	assert(index >= 0 && index < cache->images_count);

	cache->images[index].user_data = user_data;
}

void skb_render_cache_debug_iterate_free_rects(skb_render_cache_t* cache, int32_t index, skb_debug_rect_iterator_func_t* callback, void* context)
{
	assert(cache);
	assert(index >= 0 && index < cache->images_count);

	const skb_atlas_image_t* atlas_image = &cache->images[index];

	const skb__atlas_t* atlas = &atlas_image->atlas;
	for (uint16_t row_idx = atlas->first_row; row_idx != SKB_ATLAS_NULL; row_idx = atlas->rows[row_idx].next) {
		const skb__atlas_row_t* row = &atlas->rows[row_idx];
		for (uint16_t it = row->first_item; it != SKB_ATLAS_NULL; it = atlas->items[it].next) {
			const skb__atlas_item_t* item = &atlas->items[it];
			if (item->flags & SKB__ATLAS_ITEM_IS_EMPTY) {
				callback(item->x, row->y, item->width, row->height, context);
			}
		}
	}

}

void skb_render_cache_debug_iterate_used_rects(skb_render_cache_t* cache, int32_t index, skb_debug_rect_iterator_func_t* callback, void* context)
{
	assert(cache);
	assert(index >= 0 && index < cache->images_count);

	for (int32_t i = 0; i < cache->glyphs_count; i++) {
		skb__cached_glyph_t* cached_glyph = &cache->glyphs[i];
		if (cached_glyph->state == SKB_RENDER_CACHE_ITEM_RASTERIZED) {
			if (cached_glyph->texture_idx == index) {
				callback(cached_glyph->atlas_offset_x, cached_glyph->atlas_offset_y, cached_glyph->width, cached_glyph->height, context);
			}
		}
	}

	for (int32_t i = 0; i < cache->icons_count; i++) {
		skb__cached_icon_t* cached_icon = &cache->icons[i];
		if (cached_icon->state == SKB_RENDER_CACHE_ITEM_RASTERIZED) {
			if (cached_icon->texture_idx == index) {
				callback(cached_icon->atlas_offset_x, cached_icon->atlas_offset_y, cached_icon->width, cached_icon->height, context);
			}
		}
	}
}

skb_rect2i_t skb_render_cache_debug_get_prev_dirty_bounds(skb_render_cache_t* cache, int32_t index)
{
	assert(cache);
	assert(index >= 0 && index < cache->images_count);
	return cache->images[index].prev_dirty_bounds;
}


//
// Atlas shelf packer
//

static uint16_t skb__atlas_alloc_row(skb__atlas_t* atlas)
{
	uint16_t row_idx = SKB_ATLAS_NULL;
	if (atlas->row_freelist != SKB_ATLAS_NULL) {
		row_idx = atlas->row_freelist;
		atlas->row_freelist = atlas->rows[atlas->row_freelist].next;
	} else {
		SKB_ARRAY_RESERVE(atlas->rows, atlas->rows_count+1);
		row_idx = (uint16_t)(atlas->rows_count++);
	}

	skb__atlas_row_t* row = &atlas->rows[row_idx];
	memset(row, 0, sizeof(skb__atlas_row_t));

	return row_idx;
}

static void skb__atlas_free_item(skb__atlas_t* atlas, uint16_t item_idx); // fwd decl

static void skb__atlas_free_row(skb__atlas_t* atlas, uint16_t row_idx)
{
	assert(row_idx != SKB_ATLAS_NULL);

	// Free items
	uint16_t item_it = atlas->rows[row_idx].first_item;
	while (item_it != SKB_ATLAS_NULL) {
		const uint16_t next_it = atlas->items[item_it].next;
		skb__atlas_free_item(atlas, item_it);
		item_it = next_it;
	}

	atlas->rows[row_idx].next = atlas->row_freelist;
	atlas->rows[row_idx].flags |= SKB__ATLAS_ROW_IS_FREED;
	atlas->row_freelist = row_idx;
}

static uint16_t skb__atlas_alloc_item(skb__atlas_t* atlas)
{
	uint16_t item_idx = SKB_ATLAS_NULL;
	uint16_t generation = 1;
	if (atlas->item_freelist != SKB_ATLAS_NULL) {
		item_idx = atlas->item_freelist;
		atlas->item_freelist = atlas->items[atlas->item_freelist].next;
		generation = atlas->items[item_idx].generation;
	} else {
		SKB_ARRAY_RESERVE(atlas->items, atlas->items_count+1);
		item_idx = (uint16_t)(atlas->items_count++);
	}
	skb__atlas_item_t* item = &atlas->items[item_idx];
	memset(item, 0, sizeof(skb__atlas_item_t));
	item->generation = generation;

	return item_idx;
}

static void skb__atlas_free_item(skb__atlas_t* atlas, uint16_t item_idx)
{
	atlas->items[item_idx].flags |= SKB__ATLAS_ITEM_IS_FREED;
	atlas->items[item_idx].next = atlas->item_freelist;
	atlas->item_freelist = item_idx;
}

static uint16_t skb__atlas_alloc_empty_row(skb__atlas_t* atlas, uint16_t y, uint16_t height)
{
	// Init atlas with empty row and empty item covering the whole area.
	uint16_t row_idx = skb__atlas_alloc_row(atlas);
	skb__atlas_row_t* row = &atlas->rows[row_idx];
	row->y = y;
	row->height = height;
	row->max_diff = 0;
	row->base_height = 0;
	row->max_empty_item_width = SKB_ATLAS_NULL; // to be calculated later
	row->flags |= SKB__ATLAS_ROW_IS_EMPTY;
	row->next = SKB_ATLAS_NULL;

	uint16_t item_idx = skb__atlas_alloc_item(atlas);
	skb__atlas_item_t* item = &atlas->items[item_idx];
	item->x = 0;
	item->width = (uint16_t)atlas->width;
	item->next = SKB_ATLAS_NULL;
	item->flags |= SKB__ATLAS_ITEM_IS_EMPTY;
	item->row = row_idx;

	row->first_item = item_idx;

	return row_idx;
}

static void skb__atlas_init(skb__atlas_t* atlas, int32_t width, int32_t height)
{
	memset(atlas, 0, sizeof(skb__atlas_t));
	atlas->width = width;
	atlas->height = height;
	atlas->row_freelist = SKB_ATLAS_NULL;
	atlas->item_freelist = SKB_ATLAS_NULL;

	// Init atlas with empty rown and empty item covering the whole area.
	atlas->first_row = skb__atlas_alloc_empty_row(atlas, 0, (uint16_t)height);
}

static void skb__atlas_destroy(skb__atlas_t* atlas)
{
	if (atlas) {
		skb_free(atlas->rows);
		skb_free(atlas->items);
		memset(atlas, 0, sizeof(skb__atlas_t));
	}
}

static bool skb__atlas_handle_is_null(skb__atlas_handle_t handle)
{
	return handle.generation == 0;
}

static void skb__atlas_get_item_offset(const skb__atlas_t* atlas, skb__atlas_handle_t handle, int32_t* offset_x, int32_t* offset_y)
{
	if ((int32_t)handle.index >= atlas->items_count || atlas->items[handle.index].generation != handle.generation) {
		*offset_x = 0;
		*offset_y = 0;
	}

	skb__atlas_item_t* item = &atlas->items[handle.index];
	*offset_x = (int32_t)item->x;
	*offset_y = (int32_t)atlas->rows[item->row].y;
}

static skb__atlas_handle_t skb__atlas_row_alloc_item(skb__atlas_t* atlas, uint16_t row_idx, uint16_t requested_width)
{
	skb__atlas_row_t* row = &atlas->rows[row_idx];
	assert(!(row->flags & SKB__ATLAS_ROW_IS_FREED));

	uint16_t item_idx = SKB_ATLAS_NULL;
	for (uint16_t item_it = row->first_item; item_it != SKB_ATLAS_NULL; item_it = atlas->items[item_it].next) {
		assert(!(atlas->items[item_it].flags & SKB__ATLAS_ITEM_IS_FREED));
		if ((atlas->items[item_it].flags & SKB__ATLAS_ITEM_IS_EMPTY) && atlas->items[item_it].width >= requested_width) {
			item_idx = item_it;
			break;
		}
	}

	if (item_idx == SKB_ATLAS_NULL)
		return (skb__atlas_handle_t) {0};

	row->flags &= ~SKB__ATLAS_ROW_IS_EMPTY;
	row->max_empty_item_width = -1;

	// Split
	uint16_t remainder_item_idx = skb__atlas_alloc_item(atlas);

	skb__atlas_item_t* item = &atlas->items[item_idx];
	skb__atlas_item_t* remainter_item = &atlas->items[remainder_item_idx];

	uint16_t available_space = item->width;
	uint16_t next_item_idx = item->next;

	item->width = requested_width;
	item->flags &= ~SKB__ATLAS_ITEM_IS_EMPTY;
	item->next = remainder_item_idx;

	remainter_item->row = row_idx;
	remainter_item->x = item->x + requested_width;
	remainter_item->width = available_space - requested_width;
	remainter_item->flags |= SKB__ATLAS_ITEM_IS_EMPTY;
	remainter_item->next = next_item_idx;

	return (skb__atlas_handle_t) {
		.index = item_idx,
		.generation = item->generation,
	};
}

static bool skb__atlas_row_has_space(const skb__atlas_t* atlas, skb__atlas_row_t* row, uint16_t requested_width)
{
	if (row->max_empty_item_width == SKB_ATLAS_NULL) {
		row->max_empty_item_width = 0;
		for (uint16_t item_it = row->first_item; item_it != SKB_ATLAS_NULL; item_it = atlas->items[item_it].next) {
			assert(!(atlas->items[item_it].flags & SKB__ATLAS_ITEM_IS_FREED));
			if ((atlas->items[item_it].flags & SKB__ATLAS_ITEM_IS_EMPTY) && atlas->items[item_it].width > row->max_empty_item_width)
				row->max_empty_item_width = atlas->items[item_it].width;
		}
	}
	return row->max_empty_item_width >= requested_width;
}

static bool skb__atlas_alloc_rect(
	skb__atlas_t* atlas, int32_t requested_width, int32_t requested_height,
	int32_t* offset_x, int32_t* offset_y, skb__atlas_handle_t* handle,
	const skb_render_cache_config_t* config)
{
	requested_width = skb_maxi(requested_width, 1);
	requested_height = skb_align(requested_height, config->atlas_item_height_rounding);

	if (requested_width > atlas->width || requested_height > atlas->height)
		return false;

	uint16_t best_row_idx = SKB_ATLAS_NULL;
	int32_t best_row_error = atlas->height;

	for (uint16_t row_it = atlas->first_row; row_it != SKB_ATLAS_NULL; row_it = atlas->rows[row_it].next) {
		skb__atlas_row_t* row = &atlas->rows[row_it];
		assert(!(row->flags & SKB__ATLAS_ROW_IS_FREED));

		if (row->flags & SKB__ATLAS_ROW_IS_EMPTY) {
			if (requested_height > (int32_t)row->height)
				continue;

			if (!skb__atlas_row_has_space(atlas, row, (uint16_t)requested_width))
				continue;

			const int32_t error = requested_height;
			if (error < best_row_error) {
				best_row_error = requested_height;
				best_row_idx = row_it;
			}
		} else {
			const int32_t min_height = (int32_t)row->base_height - (int32_t)row->max_diff;
			const int32_t max_height = (int32_t)row->base_height + (int32_t)row->max_diff;
			if (requested_height < min_height || requested_height > max_height)
				continue;

			if (!skb__atlas_row_has_space(atlas, row, (uint16_t)requested_width))
				continue;

			if (row->height == requested_height) {
				skb__atlas_handle_t item = skb__atlas_row_alloc_item(atlas, row_it, (uint16_t)requested_width);
				assert(!skb__atlas_handle_is_null(item));
				skb__atlas_get_item_offset(atlas, item, offset_x, offset_y);
				atlas->occupancy += (int32_t)row->height * requested_width;
				*handle = item;
				return true;
			}

			if (requested_height <= (int32_t)row->height) {
				// Allow up max_diff size difference to be packed into same row.
				const int32_t error = (int32_t)row->height - requested_height;
				if (error < best_row_error) {
					best_row_error = error;
					best_row_idx = row_it;
				}
			} else if (requested_height > (int32_t)row->height && row->next != SKB_ATLAS_NULL) {
				// Check to see if we can grow this row to accommodate the height.
				const int32_t error = requested_height - (int32_t)row->height;
				if (error < best_row_error) {
					skb__atlas_row_t* next_row = &atlas->rows[row->next];
					if ((next_row->flags & SKB__ATLAS_ROW_IS_EMPTY) && (int32_t)(row->height + next_row->height) >= requested_height) {
						best_row_error = error;
						best_row_idx = row_it;
					}
				}
			}
		}
	}

	// If no row was found, there's no space in the atlas.
	if (best_row_idx == SKB_ATLAS_NULL)
		return false;

	if (atlas->rows[best_row_idx].flags & SKB__ATLAS_ROW_IS_EMPTY) {
		// The best row is empty, split it to requested size.
		uint16_t row_y = atlas->rows[best_row_idx].y;
		uint16_t row_height = atlas->rows[best_row_idx].height;
		uint16_t next_row_idx = atlas->rows[best_row_idx].next;

		assert((int32_t)row_height >= requested_height);

		uint16_t remainder_row_idx = skb__atlas_alloc_empty_row(atlas, row_y + (uint16_t)requested_height, row_height - (uint16_t)requested_height);

		atlas->rows[best_row_idx].height = (uint16_t)requested_height;
		atlas->rows[best_row_idx].base_height = (uint16_t)requested_height;
		atlas->rows[best_row_idx].max_diff = (uint16_t)((float)requested_height * config->atlas_fit_max_factor);
		atlas->rows[best_row_idx].next = remainder_row_idx;

		atlas->rows[remainder_row_idx].next = next_row_idx;

	} else if (requested_height > atlas->rows[best_row_idx].height) {
		// Make the best row larger.
		uint16_t next_row_idx = atlas->rows[best_row_idx].next;
		assert(next_row_idx != SKB_ATLAS_NULL && (atlas->rows[next_row_idx].flags & SKB__ATLAS_ROW_IS_EMPTY));
		assert(!(atlas->rows[next_row_idx].flags & SKB__ATLAS_ROW_IS_FREED));

		uint16_t combined_height = atlas->rows[best_row_idx].height + atlas->rows[next_row_idx].height;
		assert((int32_t)combined_height >= requested_height);
		uint16_t diff = (uint16_t)requested_height - atlas->rows[best_row_idx].height;

		atlas->rows[best_row_idx].height += diff;

		atlas->rows[next_row_idx].y += diff;
		atlas->rows[next_row_idx].height -= diff;
	}

	skb__atlas_handle_t item = skb__atlas_row_alloc_item(atlas, best_row_idx, (uint16_t)requested_width);
	assert(!skb__atlas_handle_is_null(item));
	skb__atlas_get_item_offset(atlas, item, offset_x, offset_y);
	*handle = item;

	atlas->occupancy += (int32_t)atlas->rows[best_row_idx].height * requested_width;

	return true;
}

static bool skb__atlas_free_rect(skb__atlas_t* atlas, skb__atlas_handle_t handle)
{
	uint16_t item_idx = handle.index;
	if ((int32_t)item_idx >= atlas->items_count || atlas->items[item_idx].generation != handle.generation)
		return false;

	skb__atlas_item_t* item = &atlas->items[item_idx];
	assert(!(item->flags & SKB__ATLAS_ITEM_IS_FREED));

	uint16_t row_idx = item->row;
	skb__atlas_row_t* row = &atlas->rows[row_idx];
	assert(!(row->flags & SKB__ATLAS_ROW_IS_FREED));

	atlas->occupancy -= (int32_t)row->height * (int32_t)item->width;

	// Find prev item index as we don't store it explicitly.
	uint16_t prev_item_idx = SKB_ATLAS_NULL;
	for (uint16_t item_it = row->first_item; item_it != SKB_ATLAS_NULL; item_it = atlas->items[item_it].next) {
		assert(!(atlas->items[item_it].flags & SKB__ATLAS_ITEM_IS_FREED));
		if (item_it == item_idx)
			break;
		prev_item_idx = item_it;
	}

	// Mark the item empty
	item->flags |= SKB__ATLAS_ITEM_IS_EMPTY;
	item->generation++; // bump generation to recognize stale access

	// Merge with previous empty
	if (prev_item_idx != SKB_ATLAS_NULL && (atlas->items[prev_item_idx].flags & SKB__ATLAS_ITEM_IS_EMPTY)) {
		skb__atlas_item_t* prev_item = &atlas->items[prev_item_idx];
		prev_item->width += item->width;
		prev_item->next = item->next;
		skb__atlas_free_item(atlas, item_idx);
		item = prev_item;
	}

	// Merge with next empty
	if (item->next != SKB_ATLAS_NULL && (atlas->items[item->next].flags & SKB__ATLAS_ITEM_IS_EMPTY)) {
		uint16_t next_item_idx = item->next;
		skb__atlas_item_t* next_item = &atlas->items[next_item_idx];
		item->width += next_item->width;
		item->next = next_item->next;
		skb__atlas_free_item(atlas, next_item_idx);
	}

	row->max_empty_item_width = SKB_ATLAS_NULL; // to be calculated later

	assert(row->first_item != SKB_ATLAS_NULL);
	const bool is_empty =
		(atlas->items[row->first_item].flags & SKB__ATLAS_ITEM_IS_EMPTY)
		&& atlas->items[row->first_item].next == SKB_ATLAS_NULL;
	SKB_SET_FLAG(row->flags, SKB__ATLAS_ROW_IS_EMPTY, is_empty);

	// The row became empty
	if (row->flags & SKB__ATLAS_ROW_IS_EMPTY) {

		row->max_diff = 0;
		row->base_height = 0;

		// Find prev row index as we don't store it explicitly.
		uint16_t prev_row_idx = SKB_ATLAS_NULL;
		for (uint16_t row_it = atlas->first_row; row_it != SKB_ATLAS_NULL; row_it = atlas->rows[row_it].next) {
			assert(!(atlas->rows[row_it].flags & SKB__ATLAS_ROW_IS_FREED));
			if (row_it == row_idx)
				break;
			prev_row_idx = row_it;
		}

		// Merge with previous empty
		if (prev_row_idx != SKB_ATLAS_NULL && (atlas->rows[prev_row_idx].flags & SKB__ATLAS_ROW_IS_EMPTY)) {
			skb__atlas_row_t* prev_row = &atlas->rows[prev_row_idx];
			prev_row->height += row->height;
			prev_row->next = row->next;
			skb__atlas_free_row(atlas, row_idx);
			row = prev_row;
		}

		// Merge with next empty
		if (row->next != SKB_ATLAS_NULL && (atlas->rows[row->next].flags & SKB__ATLAS_ROW_IS_EMPTY)) {
			uint16_t next_row_idx = row->next;
			skb__atlas_row_t* next_row = &atlas->rows[next_row_idx];
			row->height += next_row->height;
			row->next = next_row->next;
			skb__atlas_free_row(atlas, next_row_idx);
		}
	}

	return true;
}

static float skb__atlas_get_occupancy_percent(skb__atlas_t* atlas)
{
	return (float)atlas->occupancy / (float)(atlas->width * atlas->height);
}

static void skb__atlas_expand(skb__atlas_t* atlas, int32_t new_width, int32_t new_height)
{
	if (new_width > atlas->width) {
		const uint16_t expansion_x = (uint16_t)atlas->width;
		const uint16_t expansion_width = (uint16_t)(new_width - atlas->width);

		atlas->width = new_width;

		for (uint16_t row_it = atlas->first_row; row_it != SKB_ATLAS_NULL; row_it = atlas->rows[row_it].next) {
			skb__atlas_row_t* row = &atlas->rows[row_it];

			uint16_t last_item_idx = SKB_ATLAS_NULL;
			for (uint16_t item_it = row->first_item; item_it != SKB_ATLAS_NULL; item_it = atlas->items[item_it].next)
				last_item_idx = item_it;
			assert(last_item_idx != SKB_ATLAS_NULL);

			if (atlas->items[last_item_idx].flags & SKB__ATLAS_ITEM_IS_EMPTY) {
				// Expand existing empty item.
				atlas->items[last_item_idx].width += expansion_width;
			} else {
				// Create empty item at end.
				uint16_t item_idx = skb__atlas_alloc_item(atlas);
				skb__atlas_item_t* item = &atlas->items[item_idx];
				item->x = expansion_x;
				item->width = expansion_width;
				item->flags |= SKB__ATLAS_ITEM_IS_EMPTY;
				item->next = SKB_INVALID_INDEX;
				item->row = row_it;
				atlas->items[last_item_idx].next = item_idx;
			}
			row->max_empty_item_width = -1;
		}
	}

	if (new_height > atlas->height) {
		const uint16_t expansion_y = (uint16_t)atlas->height;
		const uint16_t expansion_height = (uint16_t)(new_height - atlas->height);

		atlas->height = new_height;

		uint16_t last_row_idx = SKB_ATLAS_NULL;
		for (uint16_t row_it = atlas->first_row; row_it != SKB_ATLAS_NULL; row_it = atlas->rows[row_it].next)
			last_row_idx = row_it;

		if (atlas->rows[last_row_idx].flags & SKB__ATLAS_ROW_IS_EMPTY) {
			// Last row is empty, just increase height
			atlas->rows[last_row_idx].height += expansion_height;
		} else {
			// Create empty row at end.
			uint16_t row_idx = skb__atlas_alloc_empty_row(atlas, expansion_y, expansion_height);
			skb__atlas_row_t* row = &atlas->rows[row_idx];
			atlas->rows[last_row_idx].next = row_idx;
		}
	}
}

//
// Cache
//

static bool skb__try_evict_from_cache(skb_render_cache_t* cache, int32_t evict_after_duration); // fwd

static int32_t skb__add_rect(skb_render_cache_t* cache, int32_t requested_width, int32_t requested_height, const uint8_t requested_bpp, int32_t* offset_x, int32_t* offset_y, skb__atlas_handle_t* handle)
{
	// Try to add to existing images first.
	for (int32_t i = cache->images_count - 1; i >= 0; i--) {
		skb_atlas_image_t* atlas_image = &cache->images[i];
		if (atlas_image->image.bpp == requested_bpp) {
			if (skb__atlas_alloc_rect(&atlas_image->atlas, requested_width, requested_height, offset_x, offset_y, handle, &cache->config))
				return atlas_image->index;
		}
	}
	return SKB_INVALID_INDEX;
}

static int32_t skb__add_rect_or_grow_atlas(skb_render_cache_t* cache, int32_t requested_width, int32_t requested_height, const uint8_t requested_bpp, int32_t* offset_x, int32_t* offset_y, skb__atlas_handle_t* handle)
{
	assert(cache);

	// If there's no change to fit the rectangle at all, do not even try.
	if (requested_width > cache->config.atlas_max_width || requested_height > cache->config.atlas_max_height)
		return SKB_INVALID_INDEX;

	int32_t image_idx = SKB_INVALID_INDEX;

	// Try to add to existing images first.
	image_idx = skb__add_rect(cache, requested_width, requested_height, requested_bpp, offset_x, offset_y, handle);
	if (image_idx != SKB_INVALID_INDEX)
		return image_idx;

	// Could not fit into any existing images, try to aggressively evict unused glyphs, and try again.
	static int32_t urgent_evict_after_duration = 0;
	if (skb__try_evict_from_cache(cache, urgent_evict_after_duration)) {
		image_idx = skb__add_rect(cache, requested_width, requested_height, requested_bpp, offset_x, offset_y, handle);
		if (image_idx != SKB_INVALID_INDEX)
			return image_idx;
	}

	// Could not find free space, try to expand the last atlas of matching bpp.
	skb_atlas_image_t* last_atlas_image = NULL;
	for (int32_t i = cache->images_count - 1; i >= 0; i--) {
		if (cache->images[i].image.bpp == requested_bpp) {
			last_atlas_image = &cache->images[i];
			break;
		}
	}

	if (last_atlas_image) {
		const int32_t expand_size = cache->config.atlas_expand_size;
		const int32_t expanded_width = skb_mini(last_atlas_image->atlas.width + expand_size, cache->config.atlas_max_width);
		const int32_t expanded_height = skb_mini(last_atlas_image->atlas.height + expand_size, cache->config.atlas_max_height);

		for (int32_t retry = 0; retry < 8; retry++) {
			int32_t new_width = last_atlas_image->atlas.width;
			int32_t new_height = last_atlas_image->atlas.height;

			if (last_atlas_image->atlas.width <= last_atlas_image->atlas.height && expanded_width != last_atlas_image->atlas.width)
				new_width = expanded_width;
			else
				new_height = expanded_height;

			// Check if we failed to resize
			if (new_width == last_atlas_image->atlas.width && new_height == last_atlas_image->atlas.height)
				break;

			skb__atlas_expand(&last_atlas_image->atlas, new_width, new_height);

			if (skb__atlas_alloc_rect(&last_atlas_image->atlas, requested_width, requested_height, offset_x, offset_y, handle, &cache->config))
				return last_atlas_image->index;
		}
	}

	// Could not expand the last image, create a new one.
	skb_atlas_image_t* new_atlas_image = skb__render_cache_add_atlas_image(cache, requested_width, requested_height, requested_bpp);

	if (skb__atlas_alloc_rect(&new_atlas_image->atlas, requested_width, requested_height, offset_x, offset_y, handle, &cache->config))
		return new_atlas_image->index;

	return SKB_INVALID_INDEX;
}


static uint64_t skb__render_get_glyph_hash(uint32_t gid, const skb_font_t* font, float font_size, skb_render_alpha_mode_t alpha_mode)
{
	uint64_t hash = font->hash;
	hash = skb_hash64_append_uint32(hash, gid);
	hash = skb_hash64_append_float(hash, font_size);
	hash = skb_hash64_append_uint8(hash, (uint8_t)alpha_mode);
	return hash;
}

skb_render_quad_t skb_render_cache_get_glyph_quad(
	skb_render_cache_t* cache, float x, float y, float pixel_scale,
	skb_font_collection_t* font_collection, skb_font_handle_t font_handle, uint32_t glyph_id, float font_size,
	skb_render_alpha_mode_t alpha_mode)
{
	assert(cache);

	const skb_font_t* font = skb_font_collection_get_font(font_collection, font_handle);
	if (!font) return (skb_render_quad_t) {0};

	const skb_render_image_config_t* img_config = alpha_mode == SKB_RENDER_ALPHA_SDF ? &cache->config.glyph_sdf : &cache->config.glyph_alpha;

	const float rounded_font_size = skb_ceilf(font_size * pixel_scale / img_config->rounding) * img_config->rounding;
	const float clamped_font_size = skb_clampf(rounded_font_size, img_config->min_size, img_config->max_size);

	const uint64_t hash_id = skb__render_get_glyph_hash(glyph_id, font, clamped_font_size, alpha_mode);

	skb__cached_glyph_t* cached_glyph = NULL;
	int32_t glyph_idx = SKB_INVALID_INDEX;

	if (skb_hash_table_find(cache->glyphs_lookup, hash_id, &glyph_idx)) {
		// Use existing.
		cached_glyph = &cache->glyphs[glyph_idx];
	} else {

		// Calc size
		skb_rect2i_t bounds = skb_render_get_glyph_dimensions(glyph_id, font, clamped_font_size, img_config->padding);
		int32_t requested_atlas_width = bounds.width;
		int32_t requested_atlas_height = bounds.height;

		// Add to atlas
		int32_t atlas_offset_x = 0;
		int32_t atlas_offset_y = 0;
		skb__atlas_handle_t atlas_handle = {0};

		hb_face_t* face = hb_font_get_face(font->hb_font);
		const bool is_color = hb_ot_color_glyph_has_paint(face, glyph_id);
		const uint8_t requested_bpp = is_color ? 4 : 1;

		const int32_t image_idx = skb__add_rect_or_grow_atlas(cache, requested_atlas_width, requested_atlas_height, requested_bpp, &atlas_offset_x, &atlas_offset_y, &atlas_handle);
		if (image_idx == SKB_INVALID_INDEX)
			return (skb_render_quad_t){0};

		// Alloc and init the new glyph
		if (cache->glyphs_freelist != SKB_INVALID_INDEX) {
			glyph_idx = cache->glyphs_freelist;
			cache->glyphs_freelist = cache->glyphs[glyph_idx].lru.next;
		} else {
			SKB_ARRAY_RESERVE(cache->glyphs, cache->glyphs_count + 1);
			glyph_idx = cache->glyphs_count++;
		}
		skb_hash_table_add(cache->glyphs_lookup, hash_id, glyph_idx);

		cached_glyph = &cache->glyphs[glyph_idx];
		cached_glyph->font = font;
		cached_glyph->gid = glyph_id;
		cached_glyph->clamped_font_size = clamped_font_size;
		cached_glyph->width = (int16_t)requested_atlas_width;
		cached_glyph->height = (int16_t)requested_atlas_height;
		cached_glyph->atlas_offset_x = (int16_t)atlas_offset_x;
		cached_glyph->atlas_offset_y = (int16_t)atlas_offset_y;
		cached_glyph->atlas_handle = atlas_handle;
		cached_glyph->pen_offset_x = (int16_t)bounds.x;
		cached_glyph->pen_offset_y = (int16_t)bounds.y;
		SKB_SET_FLAG(cached_glyph->flags, SKB__CACHED_GLYPH_IS_SDF, alpha_mode == SKB_RENDER_ALPHA_SDF);
		SKB_SET_FLAG(cached_glyph->flags, SKB__CACHED_GLYPH_IS_COLOR, is_color);
		cached_glyph->state = SKB_RENDER_CACHE_ITEM_INITIALIZED;
		cached_glyph->texture_idx = (uint8_t)image_idx;
		cached_glyph->hash_id = hash_id;
		cached_glyph->atlas_handle = atlas_handle;
		cached_glyph->lru = skb_list_item_make();

		cache->has_new_glyphs = true;
	}

	assert(cached_glyph);
	assert(glyph_idx != SKB_INVALID_INDEX);

	// Move glyph to front of the LRU list.
	skb_list_move_to_front(&cache->glyphs_lru, glyph_idx, skb__get_glyph, cache);
	cached_glyph->last_access_stamp = cache->now_stamp;

	const float scale = (font_size / cached_glyph->clamped_font_size);

	static const int32_t inset = 1; // Inset the rectangle by one texel, so that interpolation will not try to use data outside the atlas rect.

	skb_render_quad_t quad = {0};
	quad.image_bounds.x = (float)(cached_glyph->atlas_offset_x + inset);
	quad.image_bounds.y = (float)(cached_glyph->atlas_offset_y + inset);
	quad.image_bounds.width = (float)(cached_glyph->width - inset*2);
	quad.image_bounds.height = (float)(cached_glyph->height - inset*2);
	quad.geom_bounds.x = x + (float)(cached_glyph->pen_offset_x + inset) * scale;
	quad.geom_bounds.y = y + (float)(cached_glyph->pen_offset_y + inset) * scale;
	quad.geom_bounds.width = (float)(cached_glyph->width - inset*2) * scale;
	quad.geom_bounds.height = (float)(cached_glyph->height - inset*2) * scale;
	quad.scale = scale * pixel_scale;
	quad.image_idx = cached_glyph->texture_idx;
	SKB_SET_FLAG(quad.flags, SKB_RENDER_QUAD_IS_COLOR, cached_glyph->flags & SKB__CACHED_GLYPH_IS_COLOR);
	SKB_SET_FLAG(quad.flags, SKB_RENDER_QUAD_IS_SDF, cached_glyph->flags & SKB__CACHED_GLYPH_IS_SDF);

	return quad;
}


static uint64_t skb__render_get_icon_hash(const skb_icon_t* icon, skb_vec2_t icon_scale, skb_render_alpha_mode_t alpha_mode)
{
	uint64_t hash = icon ? icon->hash : skb_hash64_empty();
	hash = skb_hash64_append_float(hash, icon_scale.x);
	hash = skb_hash64_append_float(hash, icon_scale.y);
	hash = skb_hash64_append_uint8(hash, (uint8_t)alpha_mode);

	return hash;
}

skb_render_quad_t skb_render_cache_get_icon_quad(
	skb_render_cache_t* cache, float x, float y, float pixel_scale,
	const skb_icon_collection_t* icon_collection, skb_icon_handle_t icon_handle, skb_vec2_t icon_scale,
    skb_render_alpha_mode_t alpha_mode)
{
	assert(cache);
	assert(icon_collection);

	const skb_icon_t* icon = skb_icon_collection_get_icon(icon_collection, icon_handle);
	if (!icon) return (skb_render_quad_t) {0};

	const skb_render_image_config_t* img_config = alpha_mode == SKB_RENDER_ALPHA_SDF ? &cache->config.glyph_sdf : &cache->config.glyph_alpha;

	const float requested_width = icon->view.width * icon_scale.x;
	const float requested_height = icon->view.height * icon_scale.y;

	// Scale proportionally when image is clamped or rounded
	const float max_dim = skb_maxf(requested_width, requested_height);
	const float rounded_max_dim = skb_ceilf(max_dim * pixel_scale / img_config->rounding) * img_config->rounding;
	const float clamped_max_dim = skb_clampf(rounded_max_dim, img_config->min_size, img_config->max_size);
	const float clamp_scale = clamped_max_dim / max_dim;

	const float clamped_width = requested_width * clamp_scale;
	const float clamped_height = requested_height * clamp_scale;
	const skb_vec2_t scale = {
		.x = clamped_width / icon->view.width,
		.y = clamped_height / icon->view.height,
	};

	const uint64_t hash_id = skb__render_get_icon_hash(icon, scale, alpha_mode);

	skb__cached_icon_t* cached_icon = NULL;
	int32_t icon_idx = SKB_INVALID_INDEX;

	if (skb_hash_table_find(cache->icons_lookup, hash_id, &icon_idx)) {
		// Use existing.
		cached_icon = &cache->icons[icon_idx];
	} else {
		// Not found, create new.

		// Calc size
		skb_rect2i_t bounds = skb_render_get_icon_dimensions(icon, scale, img_config->padding);
		int32_t requested_atlas_width = bounds.width;
		int32_t requested_atlas_height = bounds.height;

		// Add to atlas
		int32_t atlas_offset_x = 0;
		int32_t atlas_offset_y = 0;
		skb__atlas_handle_t atlas_handle = {0};

		const uint8_t requested_bpp = 4;

		const int32_t image_idx = skb__add_rect_or_grow_atlas(cache, requested_atlas_width, requested_atlas_height, requested_bpp, &atlas_offset_x, &atlas_offset_y, &atlas_handle);
		if (image_idx == SKB_INVALID_INDEX)
			return (skb_render_quad_t){0};

		// Alloc and init the new icon
		if (cache->icons_freelist != SKB_INVALID_INDEX) {
			icon_idx = cache->icons_freelist;
			cache->icons_freelist = cache->icons[icon_idx].lru.next;
		} else {
			SKB_ARRAY_RESERVE(cache->icons, cache->icons_count + 1);
			icon_idx = cache->icons_count++;
		}
		skb_hash_table_add(cache->icons_lookup, hash_id, icon_idx);

		cached_icon = &cache->icons[icon_idx];
		cached_icon->icon = icon;
		cached_icon->icon_scale = scale;
		cached_icon->width = (int16_t)requested_atlas_width;
		cached_icon->height = (int16_t)requested_atlas_height;
		cached_icon->atlas_offset_x = (int16_t)atlas_offset_x;
		cached_icon->atlas_offset_y = (int16_t)atlas_offset_y;
		cached_icon->atlas_handle = atlas_handle;
		cached_icon->pen_offset_x = (int16_t)bounds.x;
		cached_icon->pen_offset_y = (int16_t)bounds.y;
		SKB_SET_FLAG(cached_icon->flags, SKB__CACHED_ICON_IS_SDF, alpha_mode == SKB_RENDER_ALPHA_SDF);
		cached_icon->flags |= SKB__CACHED_ICON_IS_COLOR;
		cached_icon->state = SKB_RENDER_CACHE_ITEM_INITIALIZED;
		cached_icon->texture_idx = (uint8_t)image_idx;
		cached_icon->hash_id = hash_id;
		cached_icon->lru = skb_list_item_make();

		cache->has_new_icons = true;
	}

	assert(cached_icon);
	assert(icon_idx != SKB_INVALID_INDEX);

	// Move glyph to front of the LRU list.
	skb_list_move_to_front(&cache->icons_lru, icon_idx, skb__get_icon, cache);

	cached_icon->last_access_stamp = cache->now_stamp;

	const float render_scale_x = requested_width / clamped_width;
	const float render_scale_y = requested_height / clamped_height;

	static const int32_t inset = 1; // Inset the rectangle by one texel, so that interpolation will not try to use data outside the atlas rect.

	skb_render_quad_t quad = {0};
	quad.image_bounds.x = (float)(cached_icon->atlas_offset_x + inset);
	quad.image_bounds.y = (float)(cached_icon->atlas_offset_y + inset);
	quad.image_bounds.width = (float)(cached_icon->width - inset*2);
	quad.image_bounds.height = (float)(cached_icon->height - inset*2);
	quad.geom_bounds.x = x + (float)(cached_icon->pen_offset_x + inset) * render_scale_x;
	quad.geom_bounds.y = y + (float)(cached_icon->pen_offset_y + inset) * render_scale_y;
	quad.geom_bounds.width = (float)(cached_icon->width - inset*2) * render_scale_x;
	quad.geom_bounds.height = (float)(cached_icon->height - inset*2) * render_scale_y;
	quad.scale = skb_maxf(render_scale_x, render_scale_y) * pixel_scale;
	quad.image_idx = cached_icon->texture_idx;
	quad.flags |= SKB_RENDER_QUAD_IS_COLOR;
	SKB_SET_FLAG(quad.flags, SKB_RENDER_QUAD_IS_SDF, cached_icon->flags & SKB__CACHED_ICON_IS_SDF);

	return quad;
}

void skb__image_clear(skb_image_t* image, int32_t offset_x, int32_t offset_y, int32_t width, int32_t height)
{
	for (int32_t y = offset_y; y < offset_y + height; y++) {
		uint8_t* row_buf = &image->buffer[offset_x * image->bpp + y * image->stride_bytes];
		memset(row_buf, 0xff, width * image->bpp);
	}
}

static bool skb__try_evict_from_cache(skb_render_cache_t* cache, int32_t evict_after_duration)
{
	assert(cache);

	int32_t evicted_count = 0;

	// Try to evict unused glyphs.

	int32_t glyph_idx = cache->glyphs_lru.tail; // Tail has least used items.
	while (glyph_idx != SKB_INVALID_INDEX) {
		skb__cached_glyph_t* cached_glyph = &cache->glyphs[glyph_idx];

		const int32_t inactive_duration = cache->now_stamp - cached_glyph->last_access_stamp;
		if (inactive_duration <= evict_after_duration)
			break;

		int32_t prev_glyph_idx = cached_glyph->lru.prev;

		if (cached_glyph->state == SKB_RENDER_CACHE_ITEM_RASTERIZED) {

			skb_atlas_image_t* atlas_image = &cache->images[cached_glyph->texture_idx];
			skb__atlas_t* atlas = &atlas_image->atlas;

			// Remove from lookup.
			skb_hash_table_remove(cache->glyphs_lookup, cached_glyph->hash_id);

			// Remove from atlas
			skb__atlas_free_rect(atlas, cached_glyph->atlas_handle);

			// Remove from LRU
			skb_list_remove(&cache->glyphs_lru, glyph_idx, skb__get_glyph, cache);

			if (cache->config.flags & SKB_RENDER_CACHE_CONFIG_DEBUG_CLEAR_REMOVED) {
				const skb_rect2i_t dirty = {
					.x = cached_glyph->atlas_offset_x,
					.y = cached_glyph->atlas_offset_y,
					.width = cached_glyph->width,
					.height = cached_glyph->height,
				};
				atlas_image->dirty_bounds = skb_rect2i_union(atlas_image->dirty_bounds, dirty);
				skb__image_clear(&atlas_image->image, cached_glyph->atlas_offset_x, cached_glyph->atlas_offset_y, cached_glyph->width, cached_glyph->height);
			}

			// Returns glyph to freelist.
			memset(cached_glyph, 0, sizeof(skb__cached_glyph_t));
			cached_glyph->state = SKB_RENDER_CACHE_ITEM_REMOVED;
			cached_glyph->lru.next = cache->glyphs_freelist;
			cache->glyphs_freelist = glyph_idx;

			evicted_count++;
		}

		glyph_idx = prev_glyph_idx;
	}

	// Try to evict unused icons.
	int32_t icon_idx = cache->icons_lru.tail; // Tail has least used items.
	while (icon_idx != SKB_INVALID_INDEX) {
		skb__cached_icon_t* cached_icon = &cache->icons[icon_idx];

		const int32_t inactive_duration = cache->now_stamp - cached_icon->last_access_stamp;
		if (inactive_duration <= evict_after_duration)
			break;

		int32_t prev_icon_idx = cached_icon->lru.prev;

		if (cached_icon->state == SKB_RENDER_CACHE_ITEM_RASTERIZED) {
			skb_atlas_image_t* atlas_image = &cache->images[cached_icon->texture_idx];
			skb__atlas_t* atlas = &atlas_image->atlas;

			// Remove from lookup.
			skb_hash_table_remove(cache->icons_lookup, cached_icon->hash_id);

			// Remove from atlas
			skb__atlas_free_rect(atlas, cached_icon->atlas_handle);

			// Remove from LRU
			skb_list_remove(&cache->icons_lru, icon_idx, skb__get_icon, cache);

			if (cache->config.flags & SKB_RENDER_CACHE_CONFIG_DEBUG_CLEAR_REMOVED) {
				const skb_rect2i_t dirty = {
					.x = cached_icon->atlas_offset_x,
					.y = cached_icon->atlas_offset_y,
					.width = cached_icon->width,
					.height = cached_icon->height,
				};
				atlas_image->dirty_bounds = skb_rect2i_union(atlas_image->dirty_bounds, dirty);
				skb__image_clear(&atlas_image->image, cached_icon->atlas_offset_x, cached_icon->atlas_offset_y, cached_icon->width, cached_icon->height);
			}

			// Returns icon to freelist.
			memset(cached_icon, 0, sizeof(skb__cached_glyph_t));
			cached_icon->state = SKB_RENDER_CACHE_ITEM_REMOVED;
			cached_icon->lru.next = cache->icons_freelist;
			cache->icons_freelist = icon_idx;

			evicted_count++;
		}

		icon_idx = prev_icon_idx;
	}

	return evicted_count > 0;
}

bool skb_render_cache_compact(skb_render_cache_t* cache)
{
	assert(cache);

	cache->now_stamp++;

	// TODO: smarted eviction strategy.
	// This tries to evict more items from the cache the higher the max usage is.
	// Maybe better option would be to do this per image. In which case the LRU lists should be per image.

	float max_occupancy = 0.f;
	for (int32_t i = 0; i < cache->images_count; i++) {
		skb_atlas_image_t* atlas_image = &cache->images[i];
		const float occupancy = skb__atlas_get_occupancy_percent(&atlas_image->atlas);
		max_occupancy = skb_maxf(max_occupancy, occupancy);
	}

	int32_t evict_after_duration = cache->config.evict_inactive_duration;
	if (max_occupancy > 0.65f) // high pressure
		evict_after_duration = 1;
	else if (max_occupancy > 0.35f) // medium pressure
		evict_after_duration = (evict_after_duration+1)/2;

	skb__try_evict_from_cache(cache, evict_after_duration);

	return true;
}

bool skb_render_cache_rasterize_missing_items(skb_render_cache_t* cache, skb_temp_alloc_t* temp_alloc, skb_renderer_t* renderer)
{
	assert(cache);

	bool updated = false;

	// Check if the atlases have resized, and resize image too.
	for (int32_t i = 0; i < cache->images_count; i++) {
		skb_atlas_image_t* atlas_image = &cache->images[i];
		if (atlas_image->image.width != atlas_image->atlas.width || atlas_image->image.height != atlas_image->atlas.height) {
			// Dirty the whole old texture.
			skb_rect2i_t dirty = { .x = 0, .y = 0, .width = atlas_image->atlas.width, .height = atlas_image->atlas.height, };
			atlas_image->dirty_bounds = skb_rect2i_union(atlas_image->dirty_bounds, dirty);
			skb__image_resize(&atlas_image->image, atlas_image->atlas.width, atlas_image->atlas.height, atlas_image->image.bpp);
		}

		if (!skb_rect2i_is_empty(atlas_image->dirty_bounds))
			updated = true;
	}

	// Glyphs
	if (cache->has_new_glyphs) {
		for (int32_t i = 0; i < cache->glyphs_count; i++) {
			skb__cached_glyph_t* cached_glyph = &cache->glyphs[i];
			if (cached_glyph->state == SKB_RENDER_CACHE_ITEM_INITIALIZED) {

				skb_rect2i_t atlas_bounds = {
					.x = cached_glyph->atlas_offset_x,
					.y = cached_glyph->atlas_offset_y,
					.width = cached_glyph->width,
					.height = cached_glyph->height,
				};

				const skb_render_alpha_mode_t alpha_mode = (cached_glyph->flags & SKB__CACHED_GLYPH_IS_SDF) ? SKB_RENDER_ALPHA_SDF : SKB_RENDER_ALPHA_MASK;
				skb_atlas_image_t* atlas_image = &cache->images[cached_glyph->texture_idx];

				skb_image_t target = {0};
				target.width = cached_glyph->width;
				target.height = cached_glyph->height;
				target.bpp = atlas_image->image.bpp;
				target.buffer = &atlas_image->image.buffer[cached_glyph->atlas_offset_x * atlas_image->image.bpp + cached_glyph->atlas_offset_y * atlas_image->image.stride_bytes];
				target.stride_bytes = atlas_image->image.stride_bytes;

				assert(atlas_image->image.stride_bytes == atlas_image->image.width * atlas_image->image.bpp);

				if (cached_glyph->flags & SKB__CACHED_GLYPH_IS_COLOR) {
					skb_render_rasterize_color_glyph(renderer, temp_alloc, cached_glyph->gid, cached_glyph->font, cached_glyph->clamped_font_size, alpha_mode,
						cached_glyph->pen_offset_x, cached_glyph->pen_offset_y, &target);
				} else {
					skb_render_rasterize_alpha_glyph(renderer, temp_alloc, cached_glyph->gid, cached_glyph->font, cached_glyph->clamped_font_size, alpha_mode,
						cached_glyph->pen_offset_x, cached_glyph->pen_offset_y, &target);
				}

				atlas_image->dirty_bounds = skb_rect2i_union(atlas_image->dirty_bounds, atlas_bounds);

				cached_glyph->state = SKB_RENDER_CACHE_ITEM_RASTERIZED;

				updated = true;
			}
		}
		cache->has_new_glyphs = false;
	}

	// Icons
	if (cache->has_new_icons) {
		for (int32_t i = 0; i < cache->icons_count; i++) {
			skb__cached_icon_t* cached_icon = &cache->icons[i];
			if (cached_icon->state == SKB_RENDER_CACHE_ITEM_INITIALIZED) {

				skb_rect2i_t atlas_bounds = {
					.x = cached_icon->atlas_offset_x,
					.y = cached_icon->atlas_offset_y,
					.width = cached_icon->width,
					.height = cached_icon->height,
				};

				const skb_render_alpha_mode_t alpha_mode = (cached_icon->flags & SKB__CACHED_ICON_IS_SDF) ? SKB_RENDER_ALPHA_SDF : SKB_RENDER_ALPHA_MASK;

				skb_atlas_image_t* atlas_image = &cache->images[cached_icon->texture_idx];

				skb_image_t target = {0};
				target.width = cached_icon->width;
				target.height = cached_icon->height;
				target.bpp = atlas_image->image.bpp;
				target.buffer = &atlas_image->image.buffer[cached_icon->atlas_offset_x * atlas_image->image.bpp + cached_icon->atlas_offset_y * atlas_image->image.stride_bytes];
				target.stride_bytes = atlas_image->image.stride_bytes;

				skb_render_rasterize_icon(renderer, temp_alloc, cached_icon->icon, cached_icon->icon_scale, alpha_mode, cached_icon->pen_offset_x, cached_icon->pen_offset_y, &target);

				atlas_image->dirty_bounds = skb_rect2i_union(atlas_image->dirty_bounds, atlas_bounds);

				cached_icon->state = SKB_RENDER_CACHE_ITEM_RASTERIZED;

				updated = true;
			}
		}
		cache->has_new_icons = false;
	}

	return updated;
}
