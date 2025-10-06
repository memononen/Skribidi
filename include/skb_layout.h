// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_LAYOUT_H
#define SKB_LAYOUT_H

#include <stdint.h>
#include <stdbool.h>
#include "skb_common.h"
#include "skb_attributes.h"
#include "skb_text.h"
#include "skb_font_collection.h"
#include "skb_icon_collection.h"
#include "skb_attribute_collection.h"

#ifdef __cplusplus
extern "C" {
#endif

// Harfbuzz forward declarations
typedef struct hb_font_t hb_font_t;
typedef const struct hb_language_impl_t *hb_language_t;

/**
 * @defgroup layout Layout
 *
 * The layout takes runs of text with attributes, and fonts as input, and gives a runs of glyphs of same font and style to render as output.
 *
 * To build the layout, the text is first split into bidi runs based on the Unicode bidirectional algorithm.
 * Then the text is itemized into runs of same script (writing system), style, and direction.
 * Next the runs of text are shaped, arranging and combining the glyphs based on the rules of the script,
 * and finally the runs of glyphs are arranged into lines.
 *
 * Some units are marked as pixels (px), but they can be interpreted as a generic units too.
 * If you are using the renderer or render cache, the values will correspond to pixels.
 *
 * Layout represents the text internally as utf-32 (codepoints) to avoid extra layer of offset translations.
 * Functions and structs that describe text position have the offset as utf-32. If it is needed to
 * convert back to utf-8, use skb_utf8_codepoint_offset().
 *
 * _Attributes_
 *
 * The attributes for the layout and text are described as a stack. When looking for attributes, like font size,
 * the stack is traversed from top to bottom and matching attribute is used. The stack looks like this, top to bottom:
 *
 * - Attributes from text (skb_text_t)
 * - Run attributes (skb_content_run_t)
 * - Layout attributes (skb_layout_params_t)
 *
 * The last attribute at the top most level is the topmost attribute.
 *
 * The stack allows to define common default values for the attributes at the lower levels, and override them per run or text span.
 *
 * Some attributes are looked up at specific level. For example layout specific attributes, like text alignment
 * are looked up starting from the layout level in the stack.
 *
 * @{
 */


/** Enum describing flags for skb_layout_params_t. */
enum skb_layout_params_flags_t {
	/** Ignored line breaks from control characters. */
	SKB_LAYOUT_PARAMS_IGNORE_MUST_LINE_BREAKS = 1 << 0,
};

/** Struct describing parameters that apply to the whole text layout. */
typedef struct skb_layout_params_t {
	/** Pointer to font collection to use. */
	skb_font_collection_t* font_collection;
	/** Pointer to the icon collection to use. */
	skb_icon_collection_t* icon_collection;
	/** Pointer to the attribute collection to use. */
	skb_attribute_collection_t* attribute_collection;
	/** Layout box width. Used for alignment, wrapping, and overflow */
	float layout_width;
	/** Layout box height. Used for alignment, wrapping, and overflow */
	float layout_height;
	/** Layout parameter flags (see skb_layout_params_flags_t). */
	uint8_t flags;
	/** Attributes to apply for the whole layout. Each content run can add or override these attributes. */
	skb_attribute_set_t layout_attributes;
} skb_layout_params_t;


/** Struct describing utf-8 text content */
typedef struct skb_content_text_utf8_t {
	const char* text;
	int32_t text_count;
} skb_content_text_utf8_t;

/** Struct describing utf-32 text content. */
typedef struct skb_content_text_utf32_t {
	const uint32_t* text;
	int32_t text_count;
} skb_content_text_utf32_t;

/** Struct describing object content */
typedef struct skb_content_object_t {
	float width;
	float height;
	intptr_t data;
} skb_content_object_t;

/** Struct describing icon content. */
typedef struct skb_content_icon_t {
	float width;
	float height;
	skb_icon_handle_t icon_handle;
} skb_content_icon_t;

/** Enum describing content run type. */
typedef enum {
	/** Content is utf-8 text. */
	SKB_CONTENT_RUN_UTF8,
	/** Content is utf-32 text. */
	SKB_CONTENT_RUN_UTF32,
	/** Content is one inline object. */
	SKB_CONTENT_RUN_OBJECT,
	/** Content is one inline icon. */
	SKB_CONTENT_RUN_ICON,
} skb_content_run_type_t;

/**
 * Struct describing a run of content with attributes.
 * Use one of the skb_content_run_make_*() functions to initialize specific type of content.
 * Note: the struct does not take copy of the data, it's just used to pass data to immediate function call.
 *	The pointers must be valid until a function taking the runs is called, e.g. skb_layout_create_from_runs().
 */
typedef struct skb_content_run_t {
	union {
		/** The utf-8 content, if type == SKB_CONTENT_RUN_UTF8. */
		skb_content_text_utf8_t utf8;
		/** The utf-32 content, if type == SKB_CONTENT_RUN_UTF32. */
		skb_content_text_utf32_t utf32;
		/** The object content, if type == SKB_CONTENT_RUN_OBJECT. */
		skb_content_object_t object;
		/** The icon content, if type == SKB_CONTENT_RUN_OBJECT. */
		skb_content_icon_t icon;
	};
	/** ID of the run, which can be later used to identify content in the layout. 0 is treated as invalid value. */
	intptr_t run_id;
	/** Attribute set to apply for the run. */
	skb_attribute_set_t attributes;
	/** Type of the content, see skb_content_run_type_t. */
	uint8_t type;
} skb_content_run_t;

/**
 * Makes utf-8 content run.
 *
 * Note: the function does not take copy of the data. The passed pointers (including attribute slice)
 * must be valid until a function taking the runs is called, e.g. skb_layout_create_from_runs().
 *
 * @param text pointer to the utf-8 text.
 * @param text_count length of the text, or -1 if not known.
 * @param attributes attributes to apply for the text.
 * @param run_id id representing the run, id of 0 is treated as invalid, in which case the run cannot be queried.
 * @return initialized content run.
 */
skb_content_run_t skb_content_run_make_utf8(const char* text, int32_t text_count, skb_attribute_set_t attributes, intptr_t run_id);

/**
 * Makes utf-32 content run.
 *
 * Note: the function does not take copy of the data. The passed pointers (including attribute slice)
 * must be valid until a function taking the runs is called, e.g. skb_layout_create_from_runs().
 *
 * @param text pointer to the utf-32 text.
 * @param text_count length of the text, or -1 if not known.
 * @param attributes attributes to apply for the text.
 * @param run_id id representing the run, id of 0 is treated as empty, in which case the run is ignored by content queries.
 * @return initialized content run.
 */
skb_content_run_t skb_content_run_make_utf32(const uint32_t* text, int32_t text_count, skb_attribute_set_t attributes, intptr_t run_id);

/**
 * Makes inline object content run.
 *
 * When inline object content run is added:
 *  - A replacement object character (U+FFFC) will be added to the text which is used to track the position of the object in the text
 *  - The object definition is stored in attribute span, and can also be accessed from skb_layout_run_t.
 *
 * Note: the function does not take copy of the data. The passed pointers (including attribute slice)
 * must be valid until a function taking the runs is called, e.g. skb_layout_create_from_runs().
 *
 * @param data data that can be used to identify the object.
 * @param width width of the object.
 * @param height height of the object.
 * @param attributes attributes to apply for the object.
 * @return initialized content run.
 * @param run_id id representing the run, id of 0 is treated as empty, in which case the run is ignored by content queries.
 */
skb_content_run_t skb_content_run_make_object(intptr_t data, float width, float height, skb_attribute_set_t attributes, intptr_t run_id);

/**
 * Makes inline icon content run.
 *
 * When inline icon content run is added:
 * - A replacement object character (U+FFFC) will be added to the text which is used to track the position of the icon in the text
 * - The object definition is stored in attribute span, and can also be accessed from skb_layout_run_t
 * - The icon name will be resolved into an icon handle, which is stored in the "handle" of the icon attribute
 * - The icon size will be calculated, and stored to the "width" and "height" of the icon attribute
 *
 * Note: the function does not take copy of the data. The passed pointers (including attribute slice)
 * must be valid until a function taking the runs is called, e.g. skb_layout_create_from_runs().
 *
 * @param icon_handle handle of the icon to add, the handle must point to the icon_collection specified in the layout params.
 * @param width width of the icon, if SKB_SIZE_AUTO the width will be calculated from height keeping aspect ratio.
 * @param height height of the icon, if SKB_SIZE_AUTO the height will be calculated from width keeping aspect ratio.
 * @param attributes attributes to apply for the icon.
 * @param run_id id representing the run, id of 0 is treated as empty, in which case the run is ignored by content queries.
 * @return initialized content run.
 */
skb_content_run_t skb_content_run_make_icon(skb_icon_handle_t icon_handle, float width, float height, skb_attribute_set_t attributes, intptr_t run_id);

/** Enum describing flags for skb_layout_line_t. */
typedef enum {
	/** Flag indicating that layout line is truncated (see skb_text_overflow_t). */
	SKB_LAYOUT_LINE_IS_TRUNCATED	= 1 << 0,
} skb_layout_line_flags_t;

/**
 * Struct describing a line of text.
 *
 * Note: text_range contain the range of text before line overflow handling,
 *		it may contain data that is not visible, and does not contain the ellipsis.
 *
 * Use layout_run_range to get range of visible glyphs.
 */
typedef struct skb_layout_line_t {
	/** Range of text (codepoints) that belong to the line. */
	skb_range_t text_range;
	/** Range of layout runs that belong to the line (glyphs are stored in layout runs). */
	skb_range_t layout_run_range;
	/** Range of decorations that belong to the line. */
	skb_range_t decorations_range;
	/** Text offset (codepoints) of the start of the last codepoint on the line. */
	int32_t last_grapheme_offset;
	/** Combined ascender of the line. Describes how much the line extends above the baseline. */
	float ascender;
	/** Combined descender of the line. Describes how much the line extends below the baseline. */
	float descender;
	/** Y position of the baseline the text on the line was aligned to (see skb_layout_params_t.baseline_align). */
	float baseline;
	/** Logical bounding rectangle of the line. The Y extends of the rectangle is set to the line height, which can differ from the ascender and descender. */
	skb_rect2_t bounds;
	/** Bounding rectangle of the line that contains all the content (might overestimate). */
	skb_rect2_t culling_bounds;
	/** Common glyph bounds can encompass any glyph in the line, used for per glyph culling (relative to glyph offset). Empty if no glyphs in line. */
	skb_rect2_t common_glyph_bounds;
	/** Line flags, see skb_layout_line_flags_t. */
	uint8_t flags;
} skb_layout_line_t;

/**
 * Struct describing continuous run of shaped and positioned layout content.
 *
 * Text content:
 *  - "glyph_range" describes the run of glyphs to render using font_handle
 *  - the other font data is stored in the attributes, e.g. fonts size can be queried using skb_attributes_get_font()
 *  - "bounds" describes the logical bounding box of all the glyphs
 *
 * Object or icon content:
 *  - the object data is stored in the attributes and can be accessed using skb_attributes_get_object()
 *  - "bounds" describes the location and size of the object or icon
 */
typedef struct skb_layout_run_t {
	/** Type of the content, see skb_content_run_type_t */
	uint8_t type;
	/** Text direction of the run. */
	uint8_t direction;
	/** Script of the run */
	uint8_t script;
	/** Bidi level of the run */
	uint8_t bidi_level;
	/** Index of the content run where the layout run originates. Can be used to detect style changes. */
	int32_t content_run_idx;
	/** Range of glyphs the content corresponds to. Note: glyphs are in visual order. */
	skb_range_t glyph_range;
	/** Range of clusters the content corresponds to. Note: clusters are in logical order. */
	skb_range_t cluster_range;
	/** Logical bounding rectangle of the content. */
	skb_rect2_t bounds;
	/** Y position of the reference baseline of the run (in practice the alphabetic baseline). The text decorations are positioned relative to this baseline. */
	float ref_baseline;
	/** Cached font size. */
	float font_size;
	/** Attributes assigned to the run. */
	skb_attribute_set_t attributes;
	/** ID of the content run where the layout run originates. */
	intptr_t content_run_id;
	union {
		/** Font handle of the text content, if text content. */
		skb_font_handle_t font_handle;
		/** Object data, if object content. */
		intptr_t object_data;
		/** Icon handle, if icon content. */
		skb_icon_handle_t icon_handle;
	};
} skb_layout_run_t;

/** Struct describing the smallest inseparable shaping unit. Maps range of codepoints to range of glyphs. */
typedef struct skb_cluster_t {
	/** Offset of first codepoint in the cluster */
	int32_t text_offset;
	/** Offset of first glyph in the cluster.  */
	int32_t glyphs_offset;
	/** Number of codepoints in the cluster. */
	uint8_t text_count;
	/** Number of glyphs in the cluster. */
	uint8_t glyphs_count;
} skb_cluster_t;

/** Struct describing shaped and positioned glyph. */
typedef struct skb_glyph_t {
	/** X offset of the glyph (including layout origin). */
	float offset_x;
	/** Y offset of the glyph (including layout origin). */
	float offset_y;
	/** Typographic advancement to the next glyph. */
	float advance_x;
	/** Index of the cluster that the glyph relates to */
	int32_t cluster_idx;
	/** Glyph ID to render. */
	uint16_t gid;
} skb_glyph_t;

/** Struct describing text decoration  */
typedef struct skb_decoration_t {
	/** Index of the layout run the decoration is related to. */
	int32_t layout_run_idx;
	/** Range of glyphs the decoration relates to. */
	skb_range_t glyph_range;
	/** X offset of the decoration (including layout origin). */
	float offset_x;
	/** Y offset of the decoration (including layout origin). */
	float offset_y;
	/** Length of the decoration. */
	float length;
	/** Offset of the start of the pattern. */
	float pattern_offset;
	/** Thickness of the decoration. */
	float thickness;
	/** Color of the decoration line. */
	skb_color_t color;
	/** Position of the decoration line relative to the text. See skb_decoration_position_t. */
	uint8_t position;
	/** Style of the decoration line. See skb_decoration_style_t. */
	uint8_t style;
} skb_decoration_t;

/** Enum describing flags for skb_text_property_t. */
enum skb_text_prop_flags_t {
	/** Grapheme break after the codepoint. */
	SKB_TEXT_PROP_GRAPHEME_BREAK   = 1 << 0,
	/** Word break after the codepoint. */
	SKB_TEXT_PROP_WORD_BREAK       = 1 << 1,
	/** Must break line after the code point. */
	SKB_TEXT_PROP_MUST_LINE_BREAK  = 1 << 2,
	/** Allow line break after the codepoint. */
	SKB_TEXT_PROP_ALLOW_LINE_BREAK = 1 << 3,
	/** The codepoint is an emoji. */
	SKB_TEXT_PROP_EMOJI            = 1 << 4,
	/** The codepoint is a control character. */
	SKB_TEXT_PROP_CONTROL          = 1 << 5,
	/** The codepoint is a white space character. */
	SKB_TEXT_PROP_WHITESPACE       = 1 << 6,
	/** The codepoint is a punctuation character. */
	SKB_TEXT_PROP_PUNCTUATION      = 1 << 7,
};

/** Struct describing properties if a single codepoint. */
typedef struct skb_text_property_t {
	/** Text property flags (see skb_text_prop_flags_t). */
	uint8_t flags;
	/** Script of the codepoint. */
	uint8_t script;
} skb_text_property_t;

/** Opaque type for the text layout. Use skb_layout_create*() to create. */
typedef struct skb_layout_t skb_layout_t;

/**
 * Appends the hash of the layout params to the provided hash.
 * @param hash hash to append to.
 * @param params pointer to the parameters to hash.
 * @return combined hash.
 */
uint64_t skb_layout_params_hash_append(uint64_t hash, const skb_layout_params_t* params);

/**
 * Creates empty layout with specified parameters.
 * @param params parameters to use for the layout.
 * @return newly create empty layout.
 */
skb_layout_t* skb_layout_create(const skb_layout_params_t* params);

/**
 * Creates new layout from the provided parameters, text and text attributes.
 * @param temp_alloc temp alloc to use during building the layout.
 * @param params paramters to use for the layout.
 * @param text text to layout as utf-8.
 * @param text_count length of the text, or -1 is nul terminated.
 * @param attributes attributes to apply for the text.
 * @return newly create layout.
 */
skb_layout_t* skb_layout_create_utf8(
	skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params,
	const char* text, int32_t text_count, skb_attribute_set_t attributes);

/**
 * Creates new layout from the provided parameters, text and text attributes.
 * @param temp_alloc temp alloc to use during building the layout.
 * @param params paramters to use for the layout.
 * @param text text to layout as utf-32.
 * @param text_count length of the text, or -1 is nul terminated.
 * @param attributes attributes to apply for the text.
 * @return newly create layout.
 */
skb_layout_t* skb_layout_create_utf32(
	skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params,
	const uint32_t* text, int32_t text_count, skb_attribute_set_t attributes);

/**
 * Creates new layout from the provided parameters and text runs.
 * The text runs are combined into one attributed string and laid out as one.
 * @param temp_alloc temp alloc to use during building the layout.
 * @param params paramters to use for the layout.
 * @param runs utf-8 text runs to combine into continuous text.
 * @param runs_count number of runs.
 * @return newly create layout.
 */
skb_layout_t* skb_layout_create_from_runs(
	skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params,
	const skb_content_run_t* runs, int32_t runs_count);

/**
 * Creates new layout from the provided parameters and text.
 * @param temp_alloc temp alloc to use during building the layout.
 * @param params paramters to use for the layout.
 * @param text pointer to the text to copy the text and attributes from.
 * @return newly create layout.
 */
skb_layout_t* skb_layout_create_from_text(skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params, const skb_text_t* text, skb_attribute_set_t attributes);

/**
 * Sets the layout from the provided parameters, text and text attributes.
 * @param layout layout to set up
 * @param temp_alloc temp alloc to use during building the layout.
 * @param params paramters to use for the layout.
 * @param text text to layout as utf-8.
 * @param text_count length of the text, or -1 is nul terminated.
 * @param attributes attributes to apply for the text.
 * @return newly create layout.
 */
void skb_layout_set_utf8(
	skb_layout_t* layout, skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params,
	const char* text, int32_t text_count, skb_attribute_set_t attributes);

/**
 * Sets the layout from the provided parameters, text and text attributes.
 * @param layout layout to set up
 * @param temp_alloc temp alloc to use during building the layout.
 * @param params paramters to use for the layout.
 * @param text text to layout as utf-8.
 * @param text_count length of the text, or -1 is nul terminated.
 * @param attributes attributes to apply for the text.
 * @return newly create layout.
 */
void skb_layout_set_utf32(
	skb_layout_t* layout, skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params,
	const uint32_t* text, int32_t text_count, skb_attribute_set_t attributes);

/**
 * Sets the layout from the provided parameters and text runs.
 * The text runs are combined into one attributes string and laid out as one.
 * @param layout layout to set up
 * @param temp_alloc temp alloc to use during building the layout.
 * @param params paramters to use for the layout.
 * @param runs utf-8 text runs to combine into continuous text.
 * @param runs_count number of runs.
 */
void skb_layout_set_from_runs(
	skb_layout_t* layout, skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params,
	const skb_content_run_t* runs, int32_t runs_count);

/**
 * Sets the layout from the provided parameters and text.
 * The text runs are combined into one attributes string and laid out as one.
 * @param layout layout to set up
 * @param temp_alloc temp alloc to use during building the layout.
 * @param params paramters to use for the layout.
 * @param text pointer to the text to copy the text and attributes from.
 */
void skb_layout_set_from_text(
	skb_layout_t* layout, skb_temp_alloc_t* temp_alloc, const skb_layout_params_t* params,
	const skb_text_t* text, skb_attribute_set_t attributes);

/**
 * Empties the specified layout. Keeps the existing allocations.
 * @param layout layout to reset.
 */
void skb_layout_reset(skb_layout_t* layout);

/**
 * Destroys specified layout.
 * @param layout layout to destroy.
 */
void skb_layout_destroy(skb_layout_t* layout);

/**
 * Returns parameters that were used to create th elayout.
 * @param layout layout to use
 * @return const pointer to the parameters.
 */
const skb_layout_params_t* skb_layout_get_params(const skb_layout_t* layout);

/** @return number of codepoints in the layout text. */
int32_t skb_layout_get_text_count(const skb_layout_t* layout);
/** @return const pointer to the codepoints of the text. See skb_layout_get_text_count() to get text length. */
const uint32_t* skb_layout_get_text(const skb_layout_t* layout);
/** @return const pointer to the codepoint properties of the text. See skb_layout_get_text_count() to get text length. */
const skb_text_property_t* skb_layout_get_text_properties(const skb_layout_t* layout);

/** @return number of layout runs in the layout. */
int32_t skb_layout_get_layout_runs_count(const skb_layout_t* layout);
/** @return const pointer to the layout runs. See skb_layout_get_layout_runs_count() to get number of glyph runs. */
const skb_layout_run_t* skb_layout_get_layout_runs(const skb_layout_t* layout);

/** @return number of glyphs in the layout. */
int32_t skb_layout_get_glyphs_count(const skb_layout_t* layout);
/** @return const pointer to the glyphs. See skb_layout_get_glyphs_count() to get number of glyphs. */
const skb_glyph_t* skb_layout_get_glyphs(const skb_layout_t* layout);

/** @return number of clusters in the layout. */
int32_t skb_layout_get_clusters_count(const skb_layout_t* layout);
/** @return const pointer to the clusters. See skb_layout_get_clusters_count() to get number of clusters. */
const skb_cluster_t* skb_layout_get_clusters(const skb_layout_t* layout);

/** @return number of decorations in the layout. */
int32_t skb_layout_get_decorations_count(const skb_layout_t* layout);
/** @return const pointer to the lines. See skb_layout_get_decorations_count() to get number of decorations. */
const skb_decoration_t* skb_layout_get_decorations(const skb_layout_t* layout);

/** @return number of lines in the layout. */
int32_t skb_layout_get_lines_count(const skb_layout_t* layout);
/** @return const pointer to the lines. See skb_layout_get_lines_count() to get number of lines. */
const skb_layout_line_t* skb_layout_get_lines(const skb_layout_t* layout);

/** @return typographic bounds of the layout. */
skb_rect2_t skb_layout_get_bounds(const skb_layout_t* layout);

/** @return text direction of the layout, if the direction was auto, the direction inferred from the text. */
skb_text_direction_t skb_layout_get_resolved_direction(const skb_layout_t* layout);

/**
 * Get the start of the next grapheme in the layout based on text offset.
 * @param layout layout to use
 * @param offset offset (codepoints) in the text where to start looking.
 * @return offset (codepoints) to the start of the next grapheme.
 */
int32_t skb_layout_next_grapheme_offset(const skb_layout_t* layout, int32_t offset);

/**
 * Get the start of the previous grapheme in the layout based on text offset.
 * @param layout layout to use
 * @param offset offset (codepoints) in the text where to start looking.
 * @return offset (codepoints) to the start of the previous grapheme.
 */
int32_t skb_layout_prev_grapheme_offset(const skb_layout_t* layout, int32_t offset);

/**
 * Get the start of the current grapheme in the layout based on text offset.
 * @param layout layout to use
 * @param offset offset (codepoints) in the text where to start looking.
 * @return offset (codepoints) to the start of the current grapheme.
 */
int32_t skb_layout_align_grapheme_offset(const skb_layout_t* layout, int32_t offset);

//
// Text Selection
//

/** Enum describing the caret's position in relation a codepoint, in logical text order. */
typedef enum skb_caret_affinity_t {
	/** Not specified. Generally translates to SKB_AFFINITY_TRAILING. */
	SKB_AFFINITY_NONE,
	/** The caret is at the trailing edge of the codepoint. */
	SKB_AFFINITY_TRAILING,
	/** The caret is at the leading edge of the codepoint. */
	SKB_AFFINITY_LEADING,
	/** The caret is at the start of the line. This can be different than trailing when line direction and text direction do not match. */
	SKB_AFFINITY_SOL,
	/** The caret is at the end of the line. This can be different than leading when line direction and text direction do not match. */
	SKB_AFFINITY_EOL,
} skb_caret_affinity_t;

/** Struct describing position within the text in a layout. */
typedef struct skb_text_position_t {
	/** Offset (codepoints) within the text. */
	int32_t offset;
	/** Relation to the codepoint. See skb_caret_affinity_t */
	skb_caret_affinity_t affinity;
} skb_text_position_t;

/** Struct describing a selection range of the text in a layout. There's no expectation of the order of start and end positions. */
typedef struct skb_text_selection_t {
	/** Start position of the selection. */
	skb_text_position_t start_pos;
	/** End position of the selection. */
	skb_text_position_t end_pos;
} skb_text_selection_t;

/** Struct describing visual caret location.
 * The caret line can be described as: (x+descender*slope, y+descender) - (x+ascender*slope, y+ascender).
 */
typedef struct skb_visual_caret_t {
	/** X baseline location of the caret */
	float x;
	/** Y baseline location of the caret */
	float y;
	/** Ascender of the caret (negative) */
	float ascender;
	/** Descender of the caret. */
	float descender;
	/** Slope of the caret (dx = dy * slope) */
	float slope;
	/** Text direction at caret location. */
	uint8_t direction;
} skb_visual_caret_t;

/**
 * Returns the line number where the text position lies.
 * @param layout layout to use
 * @param pos position within the text.
 * @return zero based line number.
 */
int32_t skb_layout_get_line_index(const skb_layout_t* layout, skb_text_position_t pos);

/**
 * Returns the text offset (codepoint) if specific text position, taking affinity into account.
 * @param layout layout to use
 * @param pos position within the text.
 * @return text offset.
 */
int32_t skb_layout_get_text_offset(const skb_layout_t* layout, skb_text_position_t pos);

/**
 * Returns text direction at the specified text postition.
 * @param layout layout to use
 * @param pos position within the text.
 * @return text direction at the specified text postition.
 */
skb_text_direction_t skb_layout_get_text_direction_at(const skb_layout_t* layout, skb_text_position_t pos);


/** Enum describing intended movement. Caret movement and selection cursor movement have diffent behavior at the end of hte line. */
typedef enum {
	/** Moving the caret. */
	SKB_MOVEMENT_CARET,
	/** Moving selection end. */
	SKB_MOVEMENT_SELECTION,
} skb_movement_type_t;

/**
 * Returns text position under the hit location on the specified line.
 * Start or end of the line is returned if the position is outside the line bounds.
 * @param layout layout to use.
 * @param type type of interaction.
 * @param line_idx index of the line to test.
 * @param hit_x hit X location.
 * @return text position under the specified hit location.
 */
skb_text_position_t skb_layout_hit_test_at_line(const skb_layout_t* layout, skb_movement_type_t type, int32_t line_idx, float hit_x);

/**
 * Returns text position under the hit location.
 * First or last line is tested if the hit location is outside the vertical bounds.
 * Start or end of the line is returned if the hit location is outside the horizontal bounds.
 * @param layout layout to use.
 * @param type type of interaction
 * @param hit_x hit X location
 * @param hit_y hit Y location
 * @return text position under the specified hit location.
 */
skb_text_position_t skb_layout_hit_test(const skb_layout_t* layout, skb_movement_type_t type, float hit_x, float hit_y);


/** Struct identifying run of content. */
typedef struct skb_layout_content_hit_t {
	/** Run id of the hit content, 0 if no hit was found. */
	intptr_t run_id;
	/** Line index of the hit content. */
	int32_t line_idx;
	/** Layout run index of the hit content. */
	int32_t layout_run_idx;
} skb_layout_content_hit_t;

/**
 * Returns data identifying the content under the hit location at specified line.
 * If no hit was found, the "run_id" of the return value will be 0.
 * @param layout layout to use.
 * @param line_idx index of the line to test.
 * @param hit_x hit X location
 * @return struct representing the content under the hit location.
 */
skb_layout_content_hit_t skb_layout_hit_test_content_at_line(const skb_layout_t* layout, int32_t line_idx, float hit_x);

/**
 * Returns data identifying the content under the hit location.
 * If no hit was found, the "run_id" of the return value will be 0.
 * @param layout layout to use.
 * @param hit_x hit X location
 * @param hit_y hit Y location
 * @return struct representing the content under the hit location.
 */
skb_layout_content_hit_t skb_layout_hit_test_content(const skb_layout_t* layout, float hit_x, float hit_y);

/**
 * Signature of content bounds getter callback.
 * @param rect content rectangle.
 * @param layout_run_idx layout run index of the content rectangle
 * @param line_idx lined index of the content rectangle
 * @param context context passed to skb_layout_get_content_bounds()
 */
typedef void skb_content_rect_func_t(skb_rect2_t rect, int32_t layout_run_idx, int32_t line_idx, void* context);

/**
 * Return set of rectangles that represent the specified run at specified line.
 * Runs what come one after each other are reported as one rectangle.
 * If the content got broken into multiple lines, multiple rectangles will be returned.
 * Note: runs with run_id = 0 will be ignored.
 * @param layout layout to use.
 * @param line_idx index of the line where to look for the runs
 * @param run_id id of the run to query
 * @param callback callback to call on each rectangle.
 * @param context context passed to the callback.
 */
void skb_layout_get_content_bounds_at_line(const skb_layout_t* layout, int32_t line_idx, intptr_t run_id, skb_content_rect_func_t* callback, void* context);

/**
 * Return set of rectangles that represent the specified run.
 * Runs what come one after each other are reported as one rectangle.
 * Note: runs with run_id = 0 will be ignored.
 * @param layout layout to use.
 * @param run_id id of the run to query
 * @param callback callback to call on each rectangle.
 * @param context context passed to the callback.
 */
void skb_layout_get_content_bounds(const skb_layout_t* layout, intptr_t run_id, skb_content_rect_func_t* callback, void* context);


/**
 * Returns visual caret location of the text position at specified line.
 * @param layout layout to use
 * @param line_idx index of the line where the text position is.
 * @param pos text position to use.
 * @return visual caret location.
 */
skb_visual_caret_t skb_layout_get_visual_caret_at_line(const skb_layout_t* layout, int32_t line_idx, skb_text_position_t pos);

/**
 * Returns visual caret location of the text position.
 * @param layout layout to use
 * @param pos text position to use.
 * @return visual caret location.
 */
skb_visual_caret_t skb_layout_get_visual_caret_at(const skb_layout_t* layout, skb_text_position_t pos);

/** @return text position of nearest start of the line, starting from specified text position. */
skb_text_position_t skb_layout_get_line_start_at(const skb_layout_t* layout, skb_text_position_t pos);

/** @return text position of nearest end of the line, starting from specified text position. */
skb_text_position_t skb_layout_get_line_end_at(const skb_layout_t* layout, skb_text_position_t pos);

/** @return text position of nearest start of a word, starting from specified text position. */
skb_text_position_t skb_layout_get_word_start_at(const skb_layout_t* layout, skb_text_position_t pos);

/** @return text position of nearest end of a word, starting from specified text position. */
skb_text_position_t skb_layout_get_word_end_at(const skb_layout_t* layout, skb_text_position_t pos);

/** @return text position of selection start, which is first in text order. */
skb_text_position_t skb_layout_get_selection_ordered_start(const skb_layout_t* layout, skb_text_selection_t selection);

/** @return text position of selection end, which is last in text order. */
skb_text_position_t skb_layout_get_selection_ordered_end(const skb_layout_t* layout, skb_text_selection_t selection);

/** @return ordered range of text (codepoints) representing the selection. End exclusive. */
skb_range_t skb_layout_get_selection_text_offset_range(const skb_layout_t* layout, skb_text_selection_t selection);

/** @return number of codepoints in the selection. */
int32_t skb_layout_get_selection_count(const skb_layout_t* layout, skb_text_selection_t selection);

/**
 * Signature of selection bounds getter callback.
 * @param rect rectangle that has part of the selection.
 * @param context context passed to skb_layout_get_selection_bounds()
 */
typedef void skb_selection_rect_func_t(skb_rect2_t rect, void* context);

/**
 * Returns set of rectangles that represent the selection.
 * Due to bidirectional text the selection in logical order can span across multiple visual rectangles.
 * @param layout layout to use.
 * @param selection selection to get.
 * @param callback callback to call on each rectangle
 * @param context context passed to the callback.
 */
void skb_layout_get_selection_bounds(const skb_layout_t* layout, skb_text_selection_t selection, skb_selection_rect_func_t* callback, void* context);

/**
 * Returns set of rectangles that represent the selection.
 * Due to bidirectional text the selection in logical order can span across multiple visual rectangles.
 * @param layout layout to use.
 * @param offset_y y-offset added to each rectangle.
 * @param selection selection to get.
 * @param callback callback to call on each rectangle
 * @param context context passed to the callback.
 */
void skb_layout_get_selection_bounds_with_offset(const skb_layout_t* layout, float offset_y, skb_text_selection_t selection, skb_selection_rect_func_t* callback, void* context);

//
// Caret iterator
//

/** Struct describing result of caret iterator. */
typedef struct skb_caret_iterator_result_t {
	/** Text position of the caret */
	skb_text_position_t text_position;
	/** Layout run index of the caret. */
	int32_t layout_run_idx;
	/** Glyph index of the caret. */
	int32_t glyph_idx;
	/** Cluster index of the caret. */
	int32_t cluster_idx;
	/** Text direction at the text position. */
	uint8_t direction;
} skb_caret_iterator_result_t;

/** Struct holding state for iterating over all caret locations in a layout. */
typedef struct skb_caret_iterator_t {
	// Internal
	const skb_layout_t* layout;

	float advance;
	float x;

	int32_t layout_run_idx;
	int32_t layout_run_end;

	int32_t cluster_idx;
	int32_t cluster_end;

	int32_t glyph_idx;

	int32_t grapheme_pos;
	int32_t grapheme_end;

	bool end_of_runs;
	bool end_of_line;

	int32_t line_first_grapheme_offset;
	int32_t line_last_grapheme_offset;

	skb_caret_iterator_result_t pending_left;
} skb_caret_iterator_t;

/**
 * Make a caret iterator for specific line in the layout.
 * The caret iterator iterates between all grapheme boundaries (also before and after the first and last) from left to right along a line (even inside ligatures).
 * @param layout laout to use
 * @param line_idx index of the line to iterate.
 * @return initialized caret iterator.
 */
skb_caret_iterator_t skb_caret_iterator_make(const skb_layout_t* layout, int32_t line_idx);

/**
 * Advances to the next caret location.
 * @param iter iterator to advance.
 * @param x (out) x location of the caret between two graphemes.
 * @param advance (out) distance to the next caret location.
 * @param left iterator result on left grapheme.
 * @param right iterator result on right grapheme.
 * @return true as long as the output values are valid.
 */
bool skb_caret_iterator_next(skb_caret_iterator_t* iter, float* x, float* advance, skb_caret_iterator_result_t* left, skb_caret_iterator_result_t* right);

/**
 * Returns four-letter ISO 15924 script tag of the specified script.
 * @param script scrip to covert.
 * @return four letter tag.
 */
uint32_t skb_script_to_iso15924_tag(uint8_t script);

/**
 * Returns script from four-letter ISO 15924 script tag.
 * @param script_tag ISO 15924 script tag scrip to covert.
 * @return script.
 */
uint8_t skb_script_from_iso15924_tag(uint32_t script_tag);

/** @} */

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // SKB_LAYOUT_H
