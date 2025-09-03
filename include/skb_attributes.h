// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_ATTRIBUTES_H
#define SKB_ATTRIBUTES_H

#include "skb_common.h"
#include "skb_font_collection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup attributes Attributes
 *
 * Attributes are used to describe text appearance, such of font family, size or weight.
 *
 * The aatributes are passed to the layout functions using an attribute slice, which has non-owning pointer to array of attributes, number of attributes,
 * and pointer to parent attribute slice, which forms an attribute chain. The first item in the chain, is the first item of the furthest parent. And the last item
 * is the last attribute of the slice. The parent can be thought as data inheritance parent.
 *
 * If an attribute is encountered multiple times in the chain, the last attribute is selected. That a slice can override a parent attribute.
 *
 * When attributes are copied to a layout, the whole attribute chain is copied flattened.
 *
 * @{
 */

/**
 * Text direction attribute.
 * Subset of https://drafts.csswg.org/css-writing-modes-4/
 */
typedef struct skb_attribute_text_direction_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Text writing direction, no change if SKB_DIRECTION_AUTO. */
	skb_text_direction_t direction;
} skb_attribute_text_direction_t;

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
 * If multiple font size attributes are defined, only the last one is used.
 * Subset of https://drafts.csswg.org/css-fonts/
 */
typedef struct skb_attribute_font_size_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Font size (px). Default 16.0 */
	float size;
} skb_attribute_font_size_t;

/**
 * Font weight attribute.
 * If multiple font weight attributes are defined, only the last one is used.
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
 * If multiple font style attributes are defined, only the last one is used.
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
 * The attribute array can contain multiple font feature attributes.
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
 * If multiple spacing attributes are defined, only the last one is used.
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
 * If multiple spacing attributes are defined, only the last one is used.
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
 * If multiple line height attributes are defined, only the first one is used.
 */
typedef struct skb_attribute_line_height_t {
	// Attribute kind tag, must be first.
	uint32_t kind;
	/** Line height type. See skb_line_height_t for types. */
	uint8_t type;
	/** Line height, see line_height_type how the value is interpreted. */
	float height;
} skb_attribute_line_height_t;

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

/** Enum describing tags for each of the attributes. */
typedef enum {
	/** Tag for skb_attribute_text_direction_t */
	SKB_ATTRIBUTE_TEXT_DIRECTION = SKB_TAG('t','d','i','r'),
	/** Tag for skb_attribute_lang_t */
	SKB_ATTRIBUTE_LANG = SKB_TAG('l','a','n','g'),
	/** Tag for skb_attribute_font_t */
	SKB_ATTRIBUTE_FONT_FAMILY = SKB_TAG('f','o','n','t'),
	/** Tag for skb_attribute_font_t */
	SKB_ATTRIBUTE_FONT_STRETCH = SKB_TAG('f','s','t','r'),
	/** Tag for skb_attribute_font_size_t */
	SKB_ATTRIBUTE_FONT_SIZE = SKB_TAG('f','s','i','z'),
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
	/** Tag for skb_attribute_fill_t */
	SKB_ATTRIBUTE_FILL = SKB_TAG('f','i','l','l'),
	/** Tag for skb_attribute_decoration_t */
	SKB_ATTRIBUTE_DECORATION = SKB_TAG('d','e','c','o'),
	/** Tag for skb_attribute_object_align_t */
	SKB_ATTRIBUTE_OBJECT_ALIGN = SKB_TAG('o','b','a','l'),
	/** Tag for skb_attribute_object_padding_t */
	SKB_ATTRIBUTE_OBJECT_PADDING = SKB_TAG('o','b','p','a'),
} skb_attribute_type_t;

/**
 * Tagged union which can hold any attribute.
 */

typedef union skb_attribute_t {
	uint32_t kind;
	skb_attribute_text_direction_t text_direction;
	skb_attribute_lang_t lang;
	skb_attribute_font_family_t font_family;
	skb_attribute_font_size_t font_size;
	skb_attribute_font_weight_t font_weight;
	skb_attribute_font_style_t font_style;
	skb_attribute_font_stretch_t font_stretch;
	skb_attribute_font_feature_t font_feature;
	skb_attribute_letter_spacing_t letter_spacing;
	skb_attribute_word_spacing_t word_spacing;
	skb_attribute_line_height_t line_height;
	skb_attribute_fill_t fill;
	skb_attribute_decoration_t decoration;
	skb_attribute_object_align_t object_align;
	skb_attribute_object_padding_t object_padding;
} skb_attribute_t;

typedef struct skb_attribute_slice_t {
	const skb_attribute_t* items;
	int32_t count;
	struct skb_attribute_slice_t* parent;
} skb_attribute_slice_t;

#define SKB_ATTRIBUTE_SLICE_FROM_STATIC_ARRAY(array) (skb_attribute_slice_t) { .items = (array), .count = SKB_COUNTOF(array) }

/** @returns new text direction attribute. See skb_attribute_text_direction_t */
skb_attribute_t skb_attribute_make_text_direction(skb_text_direction_t direction);

/** @returns new language attribute. See skb_attribute_lang_t */
skb_attribute_t skb_attribute_make_lang(const char* lang);

/** @returns new font text attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_family(uint8_t family);

/** @returns new font size text attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_size(float size);

/** @returns new font weight text attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_weight(skb_weight_t weight);

/** @returns new font style text attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_style(skb_style_t style);

/** @returns new font text attribute. See skb_attribute_font_t */
skb_attribute_t skb_attribute_make_font_stretch(skb_stretch_t stretch);

/** @returns new font feature text attribute. See skb_attribute_font_feature_t */
skb_attribute_t skb_attribute_make_font_feature(uint32_t tag, uint32_t value);

/** @returns new spacing text attribute. See skb_attribute_letter_spacing_t */
skb_attribute_t skb_attribute_make_letter_spacing(float letter_spacing);

/** @returns new spacing text attribute. See skb_attribute_word_spacing_t */
skb_attribute_t skb_attribute_make_word_spacing(float word_spacing);

/** @returns new line height text attribute. See skb_attribute_line?height_t */
skb_attribute_t skb_attribute_make_line_height(skb_line_height_t type, float height);

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


/**
 * Returns text direction attribute or default value if not found.
 * The default value is empty, which causes no change.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
skb_text_direction_t skb_attributes_get_text_direction(const skb_attribute_slice_t attributes);

/**
 * Returns language attribute or default value if not found.
 * The default value is empty, which causes no change.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default avalue.
 */
const char* skb_attributes_get_lang(const skb_attribute_slice_t attributes);

/**
 * Returns first font attribute or default value if not found.
 * The default value is: SKB_FONT_FAMILY_DEFAULT, SKB_STRETCH_NORMAL.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
uint8_t skb_attributes_get_font_family(const skb_attribute_slice_t attributes);

/**
 * Returns first font text attribute or default value if not found.
 * The default value is: font size 16.0, SKB_FONT_FAMILY_DEFAULT, SKB_WEIGHT_NORMAL, SKB_STYLE_NORMAL, SKB_STRETCH_NORMAL.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
float skb_attributes_get_font_size(const skb_attribute_slice_t attributes);

/**
 * Returns first font text attribute or default value if not found.
 * The default value is: font size 16.0, SKB_FONT_FAMILY_DEFAULT, SKB_WEIGHT_NORMAL, SKB_STYLE_NORMAL, SKB_STRETCH_NORMAL.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
skb_weight_t skb_attributes_get_font_weight(const skb_attribute_slice_t attributes);

/**
 * Returns first font text attribute or default value if not found.
 * The default value is: font size 16.0, SKB_FONT_FAMILY_DEFAULT, SKB_WEIGHT_NORMAL, SKB_STYLE_NORMAL, SKB_STRETCH_NORMAL.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
skb_style_t skb_attributes_get_font_style(const skb_attribute_slice_t attributes);

/**
 * Returns first font text attribute or default value if not found.
 * The default value is: SKB_STRETCH_NORMAL.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
skb_stretch_t skb_attributes_get_font_stretch(const skb_attribute_slice_t attributes);

/**
 * Returns letter spacing attribute or default value if not found.
 * The default value is 0.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
float skb_attributes_get_letter_spacing(const skb_attribute_slice_t attributes);

/**
 * Returns letter spacing attribute or default value if not found.
 * The default value is 0.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
float skb_attributes_get_word_spacing(const skb_attribute_slice_t attributes);

/**
 * Returns first line height attribute or default value if not found.
 * The default value is: line spacing multiplier 1.0.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
skb_attribute_line_height_t skb_attributes_get_line_height(const skb_attribute_slice_t attributes);

/**
 * Returns first fill attribute or default value if not found.
 * The default value is opaque black.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
skb_attribute_fill_t skb_attributes_get_fill(const skb_attribute_slice_t attributes);

/**
 * Returns first object align attribute or default value if not found.
 * The default value is: SKB_OBJECT_ALIGN_TEXT_AFTER_OR_BEFORE, SKB_BASELINE_CENTRAL, 0.5f,
 * that is, align the object or icon centered to the central baseline of surrounding text.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
skb_attribute_object_align_t skb_attributes_get_object_align(const skb_attribute_slice_t attributes);

/**
 * Returns first object padding attribute or default value if not found.
 * The default value is 0.0 on all sides.
 * @param attributes attribute slice where to look for the attributes from.
 * @return first found attribute or default value.
 */
skb_attribute_object_padding_t skb_attributes_get_object_padding(skb_attribute_slice_t attributes);

/**
 * Returns number of attributes in the attribute slice and it's parent chain.
 * @param attributes attribute slice to use
 * @return attribute count.
 */
int32_t skb_attributes_get_count(const skb_attribute_slice_t attributes);

/**
 * Copies attributes from the attribute slice, and it's parent chain to flat attribute list 'target'.
 * At maximum 'target_cap' attributes are copied. See skb_attributes_get_count().
 * The first attribute of the furthest parent is the first attribute in the target array.
 * @param attributes attribute slice to use.
 * @param target array of attributes to copy the attributes to.
 * @param target_cap capacity of the target array.
 * @return number of attributes copied.
 */
int32_t skb_attributes_copy(const skb_attribute_slice_t attributes, skb_attribute_t* target, const int32_t target_cap);

/** @} */

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SKB_ATTRIBUTES_H
