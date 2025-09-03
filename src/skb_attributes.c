// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "skb_attributes.h"
#include "skb_common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "hb.h"

static const char* skb__make_hb_lang(const char* lang)
{
	// Use Harfbuzz to sanitize and allocate a string, and return pointer to it.
	hb_language_t hb_lang = hb_language_from_string(lang, -1);
	return hb_language_to_string(hb_lang);
}

skb_attribute_t skb_attribute_make_text_direction(skb_text_direction_t direction)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.text_direction = (skb_attribute_text_direction_t) {
		.kind = SKB_ATTRIBUTE_TEXT_DIRECTION,
		.direction = (uint8_t)direction,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_lang(const char* lang)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.lang = (skb_attribute_lang_t) {
		.kind = SKB_ATTRIBUTE_LANG,
		.lang = skb__make_hb_lang(lang),
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_font_family(skb_font_family_t family)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.font_family = (skb_attribute_font_family_t) {
		.kind = SKB_ATTRIBUTE_FONT_FAMILY,
		.family = family,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_font_size(float size)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.font_size = (skb_attribute_font_size_t) {
		.kind = SKB_ATTRIBUTE_FONT_SIZE,
		.size = size,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_font_weight(skb_weight_t weight)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.font_weight = (skb_attribute_font_weight_t) {
		.kind = SKB_ATTRIBUTE_FONT_WEIGHT,
		.weight = weight,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_font_style(skb_style_t style)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.font_style = (skb_attribute_font_style_t) {
		.kind = SKB_ATTRIBUTE_FONT_STYLE,
		.style = style,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_font_stretch(skb_stretch_t stretch)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.font_stretch = (skb_attribute_font_stretch_t) {
		.kind = SKB_ATTRIBUTE_FONT_STYLE,
		.stretch = stretch,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_font_feature(uint32_t tag, uint32_t value)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.font_feature = (skb_attribute_font_feature_t) {
		.kind = SKB_ATTRIBUTE_FONT_FEATURE,
		.tag = tag,
		.value = value,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_letter_spacing(float letter_spacing)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.letter_spacing = (skb_attribute_letter_spacing_t) {
		.kind = SKB_ATTRIBUTE_LETTER_SPACING,
		.spacing = letter_spacing,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_word_spacing(float word_spacing)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.word_spacing = (skb_attribute_word_spacing_t) {
		.kind = SKB_ATTRIBUTE_WORD_SPACING,
		.spacing = word_spacing,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_line_height(skb_line_height_t type, float height)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.line_height = (skb_attribute_line_height_t) {
		.kind = SKB_ATTRIBUTE_LINE_HEIGHT,
		.type = (uint8_t)type,
		.height = height,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_fill(skb_color_t color)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.fill = (skb_attribute_fill_t) {
		.kind = SKB_ATTRIBUTE_FILL,
		.color = color,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_decoration(skb_decoration_position_t position, skb_decoration_style_t style, float thickness, float offset, skb_color_t color)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.decoration = (skb_attribute_decoration_t) {
		.kind = SKB_ATTRIBUTE_DECORATION,
		.position = (uint8_t)position,
		.style = (uint8_t)style,
		.thickness = thickness,
		.offset = offset,
		.color = color,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_object_align(float baseline_ratio, skb_object_align_reference_t align_ref, skb_baseline_t align_baseline)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.object_align = (skb_attribute_object_align_t) {
		.kind = SKB_ATTRIBUTE_OBJECT_ALIGN,
		.align_ref = (uint8_t)align_ref,
		.align_baseline = (uint8_t)align_baseline,
		.baseline_ratio = baseline_ratio,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_object_padding(float start, float end, float top, float bottom)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.object_padding = (skb_attribute_object_padding_t) {
		.kind = SKB_ATTRIBUTE_OBJECT_PADDING,
		.start = start,
		.end = end,
		.top = top,
		.bottom = bottom,
	};
	return attribute;
}

skb_attribute_t skb_attribute_make_object_padding_hv(float horizontal, float vertical)
{
	skb_attribute_t attribute;
	memset(&attribute, 0, sizeof(attribute)); // Using memset() so that the padding gets zeroed too.
	attribute.object_padding = (skb_attribute_object_padding_t) {
		.kind = SKB_ATTRIBUTE_OBJECT_PADDING,
		.start = horizontal,
		.end = horizontal,
		.top = vertical,
		.bottom = vertical,
	};
	return attribute;
}


skb_text_direction_t skb_attributes_get_text_direction(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_TEXT_DIRECTION)
			return attributes.items[i].text_direction.direction;
	}

	if (attributes.parent)
		return skb_attributes_get_text_direction(*attributes.parent);

	return SKB_DIRECTION_AUTO;
}

const char* skb_attributes_get_lang(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_LANG)
			return attributes.items[i].lang.lang;
	}

	if (attributes.parent)
		return skb_attributes_get_lang(*attributes.parent);

	return NULL;
}

uint8_t skb_attributes_get_font_family(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_FONT_FAMILY)
			return attributes.items[i].font_family.family;
	}

	if (attributes.parent)
		return skb_attributes_get_font_family(*attributes.parent);

	return SKB_FONT_FAMILY_DEFAULT;
}

float skb_attributes_get_font_size(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_FONT_SIZE)
			return attributes.items[i].font_size.size;
	}

	if (attributes.parent)
		return skb_attributes_get_font_size(*attributes.parent);

	return 16.f;
}

skb_weight_t skb_attributes_get_font_weight(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_FONT_WEIGHT)
			return attributes.items[i].font_weight.weight;
	}

	if (attributes.parent)
		return skb_attributes_get_font_weight(*attributes.parent);

	return SKB_WEIGHT_NORMAL;
}

skb_style_t skb_attributes_get_font_style(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_FONT_STYLE)
			return attributes.items[i].font_style.style;
	}

	if (attributes.parent)
		return skb_attributes_get_font_style(*attributes.parent);

	return SKB_STYLE_NORMAL;
}

skb_stretch_t skb_attributes_get_font_stretch(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_FONT_STRETCH)
			return attributes.items[i].font_stretch.stretch;
	}

	if (attributes.parent)
		return skb_attributes_get_font_stretch(*attributes.parent);

	return SKB_STRETCH_NORMAL;
}

float skb_attributes_get_letter_spacing(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_LETTER_SPACING)
			return attributes.items[i].letter_spacing.spacing;
	}

	if (attributes.parent)
		return skb_attributes_get_letter_spacing(*attributes.parent);

	return 0.f;
}

float skb_attributes_get_word_spacing(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_WORD_SPACING)
			return attributes.items[i].word_spacing.spacing;
	}

	if (attributes.parent)
		return skb_attributes_get_word_spacing(*attributes.parent);

	return 0.f;
}

skb_attribute_line_height_t skb_attributes_get_line_height(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_LINE_HEIGHT)
			return attributes.items[i].line_height;
	}

	if (attributes.parent)
		return skb_attributes_get_line_height(*attributes.parent);

	static const skb_attribute_line_height_t default_line_height = {
		.type = SKB_LINE_HEIGHT_NORMAL,
	};
	return default_line_height;
}

skb_attribute_fill_t skb_attributes_get_fill(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_FILL)
			return attributes.items[i].fill;
	}

	if (attributes.parent)
		return skb_attributes_get_fill(*attributes.parent);

	static const skb_attribute_fill_t default_fill = {
		.color = {0, 0, 0, 255 },
	};
	return default_fill;
}

skb_attribute_object_align_t skb_attributes_get_object_align(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_OBJECT_ALIGN)
			return attributes.items[i].object_align;
	}

	if (attributes.parent)
		return skb_attributes_get_object_align(*attributes.parent);

	static const skb_attribute_object_align_t default_object_align = {
		.align_ref = SKB_OBJECT_ALIGN_TEXT_AFTER_OR_BEFORE,
		.align_baseline = SKB_BASELINE_CENTRAL,
		.baseline_ratio = 0.5f,
	};
	return default_object_align;
}

skb_attribute_object_padding_t skb_attributes_get_object_padding(const skb_attribute_slice_t attributes)
{
	for (int32_t i = attributes.count-1; i >= 0; i--) {
		if (attributes.items[i].kind == SKB_ATTRIBUTE_OBJECT_PADDING)
			return attributes.items[i].object_padding;
	}

	if (attributes.parent)
		return skb_attributes_get_object_padding(*attributes.parent);

	static const skb_attribute_object_padding_t default_object_padding = { 0 };
	return default_object_padding;
}

int32_t skb_attributes_get_count(const skb_attribute_slice_t attributes)
{
	int32_t count = attributes.count;
	if (attributes.parent)
		count += skb_attributes_get_count(*attributes.parent);
	return count;
}

int32_t skb_attributes_copy(const skb_attribute_slice_t attributes, skb_attribute_t* target, int32_t target_cap)
{
	int32_t copied = 0;
	if (attributes.parent)
		copied += skb_attributes_copy(*attributes.parent, target, target_cap);

	int32_t count = skb_mini(attributes.count, target_cap - copied);
	if (count > 0) {
		memcpy(target + copied, attributes.items, sizeof(skb_attribute_t) * count);
		copied += count;
	}

	return copied;
}
