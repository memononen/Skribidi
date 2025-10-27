﻿// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_ATTRIBUTES_H
#define SKB_ATTRIBUTES_H

#include "skb_common.h"
#include "skb_font_collection.h"
#include "skb_icon_collection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup attributes Attributes
 *
 * Attributes are used to describe text appearance, such of font family, size or weight.
 *
 * The attributes are passed to the layout functions using an attribute set, which has non-owning pointer to array of attributes, number of attributes,
 * and pointer to parent attribute set, which forms an attribute chain. The first item in the chain, is the first item of the furthest parent. And the last item
 * is the last attribute of the set. The parent can be thought as data inheritance parent.
 *
 * If an attribute is encountered multiple times in the chain, the last attribute is selected. That allows to override a parent attribute.
 *
 * Attributes marked with (layout only) only have affect on layout attribut set.
 *
 * When attributes are copied to a layout, the whole attribute chain is copied flattened.
 *
 * @{
 */

/**
 * Paragraph writing base direction attribute.
 * Subset of https://drafts.csswg.org/css-writing-modes-4/
 */
typedef struct skb_attribute_text_base_direction_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Text paragraph writing direction, if SKB_DIRECTION_AUTO it will be detect from the first strong character. */
	skb_text_direction_t direction;
} skb_attribute_text_base_direction_t;

/**
 * Writing mode text attribute.
 * Subset of https://drafts.csswg.org/css-writing-modes-4/
 */
typedef struct skb_attribute_lang_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** BCP 47 language tag, e.g. fi-FI. If NULL, do not override language.  */
	const char* lang;
} skb_attribute_lang_t;

/**
 * Font family attribute.
 * Subset of https://drafts.csswg.org/css-fonts/
 */
typedef struct skb_attribute_font_family_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Font family. Default SKB_FONT_FAMILY_DEFAULT. */
	uint8_t family;
} skb_attribute_font_family_t;

/**
 * Font size attribute.
 * Subset of https://drafts.csswg.org/css-fonts/
 */
typedef struct skb_attribute_font_size_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Font size (px). Default 16.0 */
	float size;
} skb_attribute_font_size_t;

/**
 * Font size scaling attribute.
 */
typedef struct skb_attribute_font_size_scaling_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Font size scaling type, see skb_font_scaling_t for options. */
	skb_font_size_scaling_t type;
	/** Scale factor, see 'type' on how the value is interpreted. */
	float scale;
} skb_attribute_font_size_scaling_t;

/**
 * Font weight attribute.
 * Subset of https://drafts.csswg.org/css-fonts/
 */
typedef struct skb_attribute_font_weight_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Font weight, see skb_weight_t. Default SKB_WEIGHT_NORMAL. */
	skb_weight_t weight;
} skb_attribute_font_weight_t;

/**
 * Font style attribute.
 * Subset of https://drafts.csswg.org/css-fonts/
 */
typedef struct skb_attribute_font_style_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Font style. Default SKB_STYLE_NORMAL. */
	skb_style_t style;
} skb_attribute_font_style_t;

/**
 * Font stretch attribute.
 * Subset of https://drafts.csswg.org/css-fonts/
 */
typedef struct skb_attribute_font_stretch_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Font stretch. Default SKB_STRETCH_NORMAL. */
	skb_stretch_t stretch;
} skb_attribute_font_stretch_t;

/**
 * Font feature text attribute.
 * The attribute set chain can contain multiple font feature attributes, all of which are applied.
 * See https://learn.microsoft.com/en-us/typography/opentype/spec/featuretags
 */
typedef struct skb_attribute_font_feature_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** OpenType font feature tag */
	uint32_t tag;
	/** Taga value, often 1 = on, 0 = off. */
	uint32_t value;
} skb_attribute_font_feature_t;

/**
 * Letter spacing attribute.
 * Subset of https://drafts.csswg.org/css-text/
 */
typedef struct skb_attribute_letter_spacing_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Letter spacing (px)  */
	float spacing;
} skb_attribute_letter_spacing_t;

/**
 * Word spacing attribute.
 * Subset of https://drafts.csswg.org/css-text/
 */
typedef struct skb_attribute_word_spacing_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Word spacing (px) */
	float spacing;
} skb_attribute_word_spacing_t;

/**
 * Line height attribute.
 */
typedef struct skb_attribute_line_height_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Line height type. See skb_line_height_t for types. */
	skb_line_height_t type;
	/** Line height, see 'type' on how the value is interpreted. */
	float height;
} skb_attribute_line_height_t;

/**
 * Tab stop increment attribute. (layout only)
 */
typedef struct skb_attribute_tab_stop_increment_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Defines the spacing between tab stops (px). If zero, the tab will have same width as space. */
	float increment;
} skb_attribute_tab_stop_increment_t;

/**
 * Vertical paragraph padding attribute.
 * The a paragraph is assigned to a group, the before/after spacing will be applied to the first and last item in the group,
 * and items within the group are spaced at group_spacing.
 */
typedef struct skb_attribute_vertical_padding_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Padding before the paragraph. */
	float before;
	/** Padding after the paragraph. */
	float after;
	/**  Spacing between paragraphs in a group. */
	float group_spacing;
} skb_attribute_vertical_padding_t;

/**
 * Horizontal paragraph padding attribute.
 */
typedef struct skb_attribute_horizontal_padding_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Padding at the start of the paragraph (text direction dependent) */
	float start;
	/** Padding at the end of the paragraph (text direction dependent) */
	float end;
} skb_attribute_horizontal_padding_t;

/**
 * Indent level of a paragraph attribute. (see skb_attribute_indent_increment_t)
 */
typedef struct skb_attribute_indent_level_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Paragraph indent level. */
	int32_t level;
} skb_attribute_indent_level_t;

/**
 * Indent increments attribute. (see skb_attribute_indent_increment_t)
 */
typedef struct skb_attribute_indent_increment_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Spacing between indent levels (px). */
	float level_increment;
	/** Offset of the first line (can be negative). */
	float first_line_increment;
} skb_attribute_indent_increment_t;

/**
 * Enum describing marker styles.
 */
typedef enum {
	/** No marker */
	SKB_LIST_MARKER_NONE,
	/** Marker is a specific character (codepoint). */
	SKB_LIST_MARKER_CODEPOINT,
	/** Marker is decimal counter. */
	SKB_LIST_MARKER_COUNTER_DECIMAL,
	/** Marker is lower case latin alphabet counter. */
	SKB_LIST_MARKER_COUNTER_LOWER_LATIN,
	/** Marker is upper case latin alphabet counter. */
	SKB_LIST_MARKER_COUNTER_UPPER_LATIN,
} skb_list_marker_style_t;

/**
 * List marker attribute.
 * The marker is placed outside the text, before the text start (depending on text direction).
 * Note: The marker will be added to the first line of a skb_layout_t, to create a list, use skb_rich_layout_t and create a paragraph per list item.
 */
typedef struct skb_attribute_list_marker_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Indent to be added at the start of the text. */
	float indent;
	/** Spacing between the marker and start of the text. */
	float spacing;
	/** Codepoint of the character to use as a bullet, if style is SKB_MARKER_STYLE_CODEPOINT. */
	uint32_t codepoint;
	/** Style of the marker, see skb_list_marker_style_t */
	uint8_t style;
} skb_attribute_list_marker_t;

/**
 * Text wrap attribute. (layout only)
 */
typedef struct skb_attribute_text_wrap_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Text wrapping. Used together with layout box to wrap the text to lines. See skb_text_wrap_t */
	skb_text_wrap_t text_wrap;
} skb_attribute_text_wrap_t;

/**
 * Text overflow attribute (layout only)
 */
typedef struct skb_attribute_text_overflow_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Text overflow. Used together with layout box to trim glyphs outside the layout bounds. See skb_text_overflow_t */
	skb_text_overflow_t text_overflow;
} skb_attribute_text_overflow_t;

/**
 * Vertical trim attribute. (layout only)
 */
typedef struct skb_attribute_vertical_trim_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Vertical trim controls which edges of the text is used to align the text vertically. See skb_vertical_trim_t */
	skb_vertical_trim_t vertical_trim;
} skb_attribute_vertical_trim_t;

/**
 * Align attribute, used for both horizontal and vertical alignment. (layout only)
 */
typedef struct skb_attribute_align_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Alignment relative to layout box. See skb_align_t. */
	skb_align_t align;
} skb_attribute_align_t;

/**
 * Baseline align attribute. (layout only)
 */
typedef struct skb_attribute_baseline_align_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Baseline alignment. Works similarly as dominant-baseline in CSS. */
	skb_baseline_t baseline;
} skb_attribute_baseline_align_t;


/**
 * Baseline shift attribute.
 */
typedef struct skb_attribute_baseline_shift_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Baseline shift type. See skb_baseline_shift_t for types. */
	skb_baseline_shift_t type;
	/** Baseline shift offset, see 'type' on how the value is interpreted. */
	float offset;
} skb_attribute_baseline_shift_t;

/**
 * Text fill color attribute.
 * It is up to the client code to decide if multiple fill attributes are supported.
 */
typedef struct skb_attribute_fill_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Color of the text */
	skb_color_t color;
} skb_attribute_fill_t;

/**
 * Text line decoration attribute.
 * It is up to the client rendering code to decide if multiple decoration attributes are supported.
 * Loosely based on https://drafts.csswg.org/css-text-decor-4/
 */
typedef struct skb_attribute_decoration_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Position of the decoration line relative to the text. See skb_decoration_position_t. */
	uint8_t position;
	/** Style of the decoration line. See skb_decoration_style_t. */
	uint8_t style;
	/** Thickness of the decoration line to draw. If left to 0.0, the thickness will be based on the font. */
	float thickness;
	/** Offset of the decoration line relative to the position. For under and bottom the offset grows down, and for through and top line the offset grows up. */
	float offset;
	/** Color of the decoration line. */
	skb_color_t color;
} skb_attribute_decoration_t;

/**
 * Object alignment attribute.
 * The alignment describes which baseline on the object (or icon) is aligned to a specific baseline of the surrounding text.
 */
typedef struct skb_attribute_object_align_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** The object's baseline is object's height multiplied by the baseline_ratio (measured from the top of the object). */
	float baseline_ratio;
	/** The reference text to align the object to, see skb_object_align_reference_t. */
	uint8_t align_ref;
	/** Which baseline of the reference text to align to, see skb_baseline_t */
	uint8_t align_baseline;
} skb_attribute_object_align_t;

/**
 * Object padding attribute.
 * Allows to define free space that should be left around object or icon.
 */
typedef struct skb_attribute_object_padding_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Horizontal padding at start (left for LTR, right for RTL). */
	float start;
	/** Horizontal padding at end (right for LTR, left for RTL). */
	float end;
	/** Vertical padding top */
	float top;
	/** Vertical padding bottom */
	float bottom;
} skb_attribute_object_padding_t;

// Forward declaration
typedef uint64_t skb_attribute_set_handle_t;

/**
 * Group tag attribute.
 * Some attributes create a single effect of a sequence of paragraphs with same group, as if the paragraphs were inside a container.
 */
typedef struct skb_attribute_group_tag_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Tag indentifying the group. Value 0 means no group. */
	uint32_t group_tag;
} skb_attribute_group_tag_t;

/**
 * Attribute collection reference attribute.
 * The referenced attribute set from the attribute collection is used at the place of the reference attribute.
 */
typedef struct skb_attribute_reference_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	skb_attribute_set_handle_t handle;
} skb_attribute_reference_t;


/** Enum describing tags for each of the attributes. */
typedef enum {
	/** Tag for skb_attribute_text_base_direction_t */
	SKB_ATTRIBUTE_TEXT_BASE_DIRECTION = SKB_TAG('t','b','d','r'),
	/** Tag for skb_attribute_lang_t */
	SKB_ATTRIBUTE_LANG = SKB_TAG('l','a','n','g'),
	/** Tag for skb_attribute_font_t */
	SKB_ATTRIBUTE_FONT_FAMILY = SKB_TAG('f','o','n','t'),
	/** Tag for skb_attribute_font_t */
	SKB_ATTRIBUTE_FONT_STRETCH = SKB_TAG('f','s','t','r'),
	/** Tag for skb_attribute_font_size_t */
	SKB_ATTRIBUTE_FONT_SIZE = SKB_TAG('f','s','i','z'),
	/** Tag for skb_attribute_font_size_scaling_t */
	SKB_ATTRIBUTE_FONT_SIZE_SCALING = SKB_TAG('f','s','c','l'),
	/** Tag for skb_attribute_font_weight_t */
	SKB_ATTRIBUTE_FONT_WEIGHT = SKB_TAG('f','w','e','i'),
	/** Tag for skb_attribute_font_size_t */
	SKB_ATTRIBUTE_FONT_STYLE = SKB_TAG('f','s','t','y'),
	/** Tag for skb_attribute_font_feature_t */
	SKB_ATTRIBUTE_FONT_FEATURE = SKB_TAG('f','e','a','t'),
	/** tag for skb_attribute_letter_spacing_t */
	SKB_ATTRIBUTE_LETTER_SPACING = SKB_TAG('l','e','s','p'),
	/** tag for skb_attribute_word_spacing_t */
	SKB_ATTRIBUTE_WORD_SPACING = SKB_TAG('w','o','s','p'),
	/** Tag for skb_attribute_line_height_t */
	SKB_ATTRIBUTE_LINE_HEIGHT = SKB_TAG('l','n','h','e'),
	/** Tag for skb_attribute_tab_stop_increment_t */
	SKB_ATTRIBUTE_TAB_STOP_INCREMENT = SKB_TAG('t','a','b','s'),
	/** Tag for skb_attribute_vertical_padding_t */
	SKB_ATTRIBUTE_VERTICAL_PADDING = SKB_TAG('v','p','a','d'),
	/** Tag for skb_attribute_horizontal_padding_t */
	SKB_ATTRIBUTE_HORIZONTAL_PADDING = SKB_TAG('h','p','a','d'),
	/** Tag for skb_attribute_indent_level_t */
	SKB_ATTRIBUTE_INDENT_LEVEL = SKB_TAG('i','l','v','l'),
	/** Tag for skb_attribute_indent_increment_t */
	SKB_ATTRIBUTE_INDENT_INCREMENT = SKB_TAG('i','i','n','c'),
	/** Tag for skb_attribute_list_marker_t */
	SKB_ATTRIBUTE_LIST_MARKER = SKB_TAG('l','i','m','k'),
	/** Tag for skb_attribute_tab_stop_increment_t */
	SKB_ATTRIBUTE_TEXT_WRAP = SKB_TAG('t','w','r','p'),
	/** Tag for skb_attribute_text_overflow_t */
	SKB_ATTRIBUTE_TEXT_OVERFLOW = SKB_TAG('t','o','f','l'),
	/** Tag for skb_attribute_vertical_trim_t */
	SKB_ATTRIBUTE_VERTICAL_TRIM = SKB_TAG('v','t','r','m'),
	/** Tag for skb_attribute_align_t (horizontal) */
	SKB_ATTRIBUTE_HORIZONTAL_ALIGN = SKB_TAG('h','a','l','n'),
	/** Tag for skb_attribute_align_t (vertical) */
	SKB_ATTRIBUTE_VERTICAL_ALIGN = SKB_TAG('v','a','l','n'),
	/** Tag for skb_attribute_baseline_align_t */
	SKB_ATTRIBUTE_BASELINE_ALIGN = SKB_TAG('b','a','l','n'),
	/** Tag for skb_attribute_baseline_shift_t */
	SKB_ATTRIBUTE_BASELINE_SHIFT = SKB_TAG('b','l','s','f'),
	/** Tag for skb_attribute_fill_t */
	SKB_ATTRIBUTE_FILL = SKB_TAG('f','i','l','l'),
	/** Tag for skb_attribute_decoration_t */
	SKB_ATTRIBUTE_DECORATION = SKB_TAG('d','e','c','o'),
	/** Tag for skb_attribute_object_align_t */
	SKB_ATTRIBUTE_OBJECT_ALIGN = SKB_TAG('o','b','a','l'),
	/** Tag for skb_attribute_object_padding_t */
	SKB_ATTRIBUTE_OBJECT_PADDING = SKB_TAG('o','b','p','a'),
	/** Tag for skb_attribute_group_t */
	SKB_ATTRIBUTE_GROUP_TAG = SKB_TAG('g','r','u','p'),
	/** Tag for skb_attribute_reference_t */
	SKB_ATTRIBUTE_REFERENCE = SKB_TAG('a','r','e','f'),
} skb_attribute_type_t;

/**
 * Tagged union which can hold any attribute.
 */

typedef union skb_attribute_t {
	uint32_t kind;
	skb_attribute_text_base_direction_t text_base_direction;
	skb_attribute_lang_t lang;
	skb_attribute_font_family_t font_family;
	skb_attribute_font_size_t font_size;
	skb_attribute_font_size_scaling_t font_size_scaling;
	skb_attribute_font_weight_t font_weight;
	skb_attribute_font_style_t font_style;
	skb_attribute_font_stretch_t font_stretch;
	skb_attribute_font_feature_t font_feature;
	skb_attribute_letter_spacing_t letter_spacing;
	skb_attribute_word_spacing_t word_spacing;
	skb_attribute_line_height_t line_height;
	skb_attribute_tab_stop_increment_t tab_stop_increment;
	skb_attribute_vertical_padding_t vertical_padding;
	skb_attribute_horizontal_padding_t horizontal_padding;
	skb_attribute_indent_level_t indent_level;
	skb_attribute_indent_increment_t indent_increment;
	skb_attribute_list_marker_t list_marker;
	skb_attribute_text_wrap_t text_wrap;
	skb_attribute_text_overflow_t text_overflow;
	skb_attribute_vertical_trim_t vertical_trim;
	skb_attribute_align_t horizontal_align;
	skb_attribute_align_t vertical_align;
	skb_attribute_baseline_align_t baseline_align;
	skb_attribute_baseline_shift_t baseline_shift;
	skb_attribute_fill_t fill;
	skb_attribute_decoration_t decoration;
	skb_attribute_object_align_t object_align;
	skb_attribute_object_padding_t object_padding;
	skb_attribute_group_tag_t group_tag;
	skb_attribute_reference_t reference;
} skb_attribute_t;

// Forward declaration
typedef struct skb_attribute_collection_t skb_attribute_collection_t;

/**
 * Struct describing a view to set of attributes. The attribute set does not own the attributes,
 * but it used in the API to pass in or get array of attributes.
 *
 * Functions accepting skb_attribute_set_t either take copy of the data, or use it immediately during the call.
 *
 * The parent pointer allows attribute sets to create chains.
 */
typedef struct skb_attribute_set_t {
	const skb_attribute_t* attributes;
	int32_t attributes_count;
	skb_attribute_set_handle_t set_handle;
	const struct skb_attribute_set_t* parent_set;
} skb_attribute_set_t;

#define SKB_ATTRIBUTE_SET_FROM_STATIC_ARRAY(array) (skb_attribute_set_t) { .attributes = (array), .attributes_count = SKB_COUNTOF(array) }

/** Creates attribute set that is a reference to specified set in a collection. */
skb_attribute_set_t skb_attribute_set_make_reference(skb_attribute_set_handle_t handle);

/** Creates attribute set that is a reference to specified set in a collection. */
skb_attribute_set_t skb_attribute_set_make_reference_by_name(const skb_attribute_collection_t* attribute_collection, const char* name);

/** @returns new text base direction attribute. See skb_attribute_text_base_direction_t */
skb_attribute_t skb_attribute_make_text_base_direction(skb_text_direction_t direction);

/** @returns new language attribute. See skb_attribute_lang_t */
skb_attribute_t skb_attribute_make_lang(const char* lang);

/** @returns new font family attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_family(uint8_t family);

/** @returns new font size attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_size(float size);

/** @returns new font size scaling attribute. See skb_attribute_font_scaling_t */
skb_attribute_t skb_attribute_make_font_size_scaling(skb_font_size_scaling_t type, float scale);

/** @returns new font weight attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_weight(skb_weight_t weight);

/** @returns new font style attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_style(skb_style_t style);

/** @returns new font attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_stretch(skb_stretch_t stretch);

/** @returns new font feature attribute. See skb_attribute_font_feature_t */
skb_attribute_t skb_attribute_make_font_feature(uint32_t tag, uint32_t value);

/** @returns new spacing attribute. See skb_attribute_letter_spacing_t */
skb_attribute_t skb_attribute_make_letter_spacing(float letter_spacing);

/** @returns new spacing attribute. See skb_attribute_word_spacing_t */
skb_attribute_t skb_attribute_make_word_spacing(float word_spacing);

/** @returns new line height attribute. See skb_attribute_line_height_t */
skb_attribute_t skb_attribute_make_line_height(skb_line_height_t type, float height);

/** @returns new tab stop increment attribute. See skb_attribute_tab_stop_increment_t */
skb_attribute_t skb_attribute_make_tab_stop_increment(float increment);

/** @returns new vertical paragraph padding attribute. See skb_attribute_vertical_padding_t */
skb_attribute_t skb_attribute_make_vertical_padding(float before, float after);

/** @returns new vertical paragraph padding attribute, including group spacing. See skb_attribute_vertical_padding_t */
skb_attribute_t skb_attribute_make_vertical_padding_with_spacing(float before, float after, float group_spacing);

/** @returns new horizontal paragraph padding attribute. See skb_attribute_horizontal_padding_t */
skb_attribute_t skb_attribute_make_horizontal_padding(float start, float end);

/** @returns new idnent level attribute. See skb_attribute_indent_level_t */
skb_attribute_t skb_attribute_make_indent_level(int32_t level);

/** @returns new indent increment attribute. See skb_attribute_indent_increment_t */
skb_attribute_t skb_attribute_make_indent_increment(float level_increment, float first_line_increment);

/** @returns new tab stop increment attribute. See skb_attribute_tab_stop_increment_t */
skb_attribute_t skb_attribute_make_list_marker(skb_list_marker_style_t style, float indent, float spacing, uint32_t codepoint);

/** @returns new text wrap attribute. See skb_attribute_text_wrap_t */
skb_attribute_t skb_attribute_make_text_wrap(skb_text_wrap_t text_wrap);

/** @returns new text overflow attribute. See skb_attribute_text overflow_t */
skb_attribute_t skb_attribute_make_text_overflow(skb_text_overflow_t text_overflow);

/** @returns new vertical trim attribute. See skb_attribute_vertical trim_t */
skb_attribute_t skb_attribute_make_vertical_trim(skb_vertical_trim_t vertical_trim);

/** @returns new horizontal align attribute. See skb_attribute_align_t */
skb_attribute_t skb_attribute_make_horizontal_align(skb_align_t horizontal_align);

/** @returns new vertical align attribute. See skb_attribute_align_t */
skb_attribute_t skb_attribute_make_vertical_align(skb_align_t vertical_align);

/** @returns new baseline align attribute. See skb_attribute_baseline_align_t */
skb_attribute_t skb_attribute_make_baseline_align(skb_baseline_t baseline_align);

/** @returns new baseline shift attribute. See skb_attribute_baseline_shift_t */
skb_attribute_t skb_attribute_make_baseline_shift(skb_baseline_shift_t type, float shift);

/** @returns new fill color text attribute. See skb_attribute_fill_t */
skb_attribute_t skb_attribute_make_fill(skb_color_t color);

/** @returns new text decoration attribute. See skb_attribute_decoration_t */
skb_attribute_t skb_attribute_make_decoration(skb_decoration_position_t position, skb_decoration_style_t style, float thickness, float offset, skb_color_t color);

/** @returns new object align attribute. See skb_attribute_object_align_t */
skb_attribute_t skb_attribute_make_object_align(float baseline_ratio, skb_object_align_reference_t align_ref, skb_baseline_t align_baseline);

/** @returns new object padding attribute. See skb_attribute_object_padding_t */
skb_attribute_t skb_attribute_make_object_padding(float start, float end, float top, float bottom);

/** @returns new object padding attribute. See skb_attribute_object_padding_t */
skb_attribute_t skb_attribute_make_object_padding_hv(float horizontal, float vertical);

/** @returns new group attribute. See skb_attribute_group_t */
skb_attribute_t skb_attribute_make_group_tag(uint32_t group_tag);

/** @returns new reference attribute. See skb_attribute_reference_t */
skb_attribute_t skb_attribute_make_reference(skb_attribute_set_handle_t set_handle);

/** @returns new reference attribute. See skb_attribute_reference_t */
skb_attribute_t skb_attribute_make_reference_by_name(const skb_attribute_collection_t* attribute_collection, const char* name);

/**
 * Returns text direction attribute or default value if not found.
 * The default value is empty, which causes no change.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_text_direction_t skb_attributes_get_text_base_direction(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns language attribute or default value if not found.
 * The default value is empty, which causes no change.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default avalue.
 */
const char* skb_attributes_get_lang(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first font attribute or default value if not found.
 * The default value is: SKB_FONT_FAMILY_DEFAULT, SKB_STRETCH_NORMAL.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
uint8_t skb_attributes_get_font_family(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first font text attribute or default value if not found.
 * The default value is: font size 16.0.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
float skb_attributes_get_font_size(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first font size attribute or default value if not found.
 * The default value is: SKB_FONT_SIZE_SCALING_NONE.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_font_size_scaling_t skb_attributes_get_font_size_scaling(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first font text attribute or default value if not found.
 * The default value is: font size 16.0, SKB_FONT_FAMILY_DEFAULT, SKB_WEIGHT_NORMAL, SKB_STYLE_NORMAL, SKB_STRETCH_NORMAL.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_weight_t skb_attributes_get_font_weight(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first font text attribute or default value if not found.
 * The default value is: font size 16.0, SKB_FONT_FAMILY_DEFAULT, SKB_WEIGHT_NORMAL, SKB_STYLE_NORMAL, SKB_STRETCH_NORMAL.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_style_t skb_attributes_get_font_style(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first font text attribute or default value if not found.
 * The default value is: SKB_STRETCH_NORMAL.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_stretch_t skb_attributes_get_font_stretch(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns letter spacing attribute or default value if not found.
 * The default value is 0.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
float skb_attributes_get_letter_spacing(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns letter spacing attribute or default value if not found.
 * The default value is 0.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
float skb_attributes_get_word_spacing(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first line height attribute or default value if not found.
 * The default value is: line spacing multiplier 1.0.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_line_height_t skb_attributes_get_line_height(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first tab stop increment attribute or default value if not found.
 * The default value is 0.0 (which will make the tab the same width as space).
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
float skb_attributes_get_tab_stop_increment(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first vertical paragraph padding attribute or default value if not found.
 * The default value is 0.0 (no padding).
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_vertical_padding_t skb_attributes_get_vertical_padding(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first horizontal paragraph padding attribute or default value if not found.
 * The default value is 0.0 (no padding).
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_horizontal_padding_t skb_attributes_get_horizontal_padding(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first paragraph indent level attribute or default value if not found.
 * The default value is 0.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
int32_t skb_attributes_get_indent_level(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first indent increment attribute or default value if not found.
 * The default value is 0.0 (no indent).
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_indent_increment_t skb_attributes_get_indent_increment(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first list marker attribute or default value if not found.
 * The default value is SKB_LIST_MARKER_NONE (no list marker).
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_list_marker_t skb_attributes_get_list_marker(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first fill attribute or default value if not found.
 * The default value is opaque black.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_fill_t skb_attributes_get_fill(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first object align attribute or default value if not found.
 * The default value is: SKB_OBJECT_ALIGN_TEXT_AFTER_OR_BEFORE, SKB_BASELINE_CENTRAL, 0.5f,
 * that is, align the object or icon centered to the central baseline of surrounding text.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_object_align_t skb_attributes_get_object_align(const skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first object padding attribute or default value if not found.
 * The default value is 0.0 on all sides.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_object_padding_t skb_attributes_get_object_padding(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first text wrap attribute or default value if not found.
 * The default value is SKB_WRAP_NONE.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_text_wrap_t skb_attributes_get_text_wrap(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first text overflow attribute or default value if not found.
 * The default value is SKB_OVERFLOW_NONE.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_text_overflow_t skb_attributes_get_text_overflow(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first vertical trim attribute or default value if not found.
 * The default value is SKB_VERTICAL_TRIM_DEFAULT.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_vertical_trim_t skb_attributes_get_vertical_trim(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first horizontal align attribute or default value if not found.
 * The default value is SKB_ALIGN_START.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_align_t skb_attributes_get_horizontal_align(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first vertical align attribute or default value if not found.
 * The default value is SKB_ALIGN_START.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_align_t skb_attributes_get_vertical_align(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first baseline align attribute or default value if not found.
 * The default value is SKB_BASELINE_ALPHABETIC.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_baseline_t skb_attributes_get_baseline_align(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first baseline align shift or default value if not found.
 * The default value is SKB_BASELINE_SHIFT_NONE.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
skb_attribute_baseline_shift_t skb_attributes_get_baseline_shift(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Returns first group tag attribute or default value if not found.
 * The default value is 0.
 * @param attributes attribute set where to look for the attributes from.
 * @param collection attribute collection which is used to lookup attribute references.
 * @return first found attribute or default value.
 */
uint32_t skb_attributes_get_group(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection);

/**
 * Collects attributes of specified type in results array from specified attribute set.
 * @param attributes attribute set to query.
 * @param collection attribute collection which is used to lookup attribute references.
 * @param kind the kind of attribute to query (see skb_attribute_type_t)
 * @param results array where to store the results.
 * @param results_cap capacity of the results array.
 * @return number of results stored in the results array.
 */
int32_t skb_attributes_get_by_kind(skb_attribute_set_t attributes, const skb_attribute_collection_t* collection, uint32_t kind, const skb_attribute_t** results, int32_t results_cap);

/**
 * Returns true if the attributes match. Reference attributes are matched by group.
 * @param a const pointer to first attribute to test
 * @param b const pointer to second attribute to test
 * @return true if attributes match.
 */
bool skb_attributes_match(const skb_attribute_t* a, const skb_attribute_t* b);

/**
 * Returns number of attributes in the attribute set and it's parent chain.
 * @param attributes attribute set to use
 * @return attribute count.
 */
int32_t skb_attributes_get_copy_flat_count(const skb_attribute_set_t attributes);

/**
 * Copies attributes from the attribute set, and it's parent chain to flat attribute list 'dest'.
 * Attribute references will not be flattened.
 * If an attribute set is reference, the reference will be flattened to the list as a reference attribute.
 * At maximum 'target_cap' attributes are copied. See skb_attributes_get_copy_flat_count().
 * The first attribute of the furthest parent is the first attribute in the target array.
 * @param attributes attribute set to use.
 * @param dest array of attributes to copy the attributes to.
 * @param dest_cap capacity of the target array.
 * @return number of attributes copied.
 */
int32_t skb_attributes_copy_flat(const skb_attribute_set_t attributes, skb_attribute_t* dest, const int32_t dest_cap);


/**
 * Appends the hash of the text attributes to the provided hash.
 * @param hash hash to append to.
 * @param attributes attributes to hash.
 * @return combined hash.
 */
uint64_t skb_attributes_hash_append(uint64_t hash, skb_attribute_set_t attributes);

/** @} */

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SKB_ATTRIBUTES_H
