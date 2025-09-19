// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_TEXT_H
#define SKB_TEXT_H

#include "skb_common.h"
#include "skb_attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup text Attributed Text
 *
 * The attributed text describes text in utf-32 format with spans of attributes.
 *
 * The attributes are stored in ordered array of spans. The spans of same type of attribute will split and merge as they are modified.
 *
 * @{
 */

enum {
	/** Maximum number of supported active/overlapping attributes at a run of text. */
	SKB_MAX_ACTIVE_ATTRIBUTES = 64,
};

/** Struct describing attribute applied to a span of text. */
typedef struct skb_attribute_span_t {
	/** Range of text the attribute is applied to. */
	skb_range_t text_range;
	/** The attribute to apply. */
	skb_attribute_t attribute;
} skb_attribute_span_t;

/** Opaque type for the text. Use skb_text_create() to create. */
typedef struct skb_text_t skb_text_t;

/**
 * Creates empty attributed text. Use skb_text_destroy() to destroy.
 * @return pointer to the newly created text.
 */
skb_text_t* skb_text_create(void);

/**
 * Creates empty attributed text that uses the temp allocator. Use skb_text_destroy() to destroy.
 * Note: the temp allocated text should be only used in very limited scope, e.g. as a text builder, which is disposed right after a layout is created.
 * @param temp_alloc temp alloc to use with the text.
 * @return pointer to the newly created text.
 */
skb_text_t* skb_text_create_temp(skb_temp_alloc_t* temp_alloc);

/**
 * Destroys specified text.
 * @param text text to destroy
 */
void skb_text_destroy(skb_text_t* text);

/**
 * Makes the text empty, but keeps the underlying buffers.
 * @param text text to reset.
 */
void skb_text_reset(skb_text_t* text);

/**
 * Reserves memory in the text buffer for text and attributes. If larger buffer is already allocated, nothing changes.
 * @param text text to change
 * @param text_count reserved number of codepoints
 * @param spans_count reserved number of attribute spans.
 */
void skb_text_reserve(skb_text_t* text, int32_t text_count, int32_t spans_count);

/** @return length of the text (utf-32 codeunits).  */
int32_t skb_text_get_utf32_count(const skb_text_t* text);
/** @return const pointer to the utf-32 string. */
const uint32_t * skb_text_get_utf32(const skb_text_t* text);

/** @return number of attribute spans of the text. */
int32_t skb_text_get_attribute_spans_count(const skb_text_t* text);
/** @return const pointer to the attribute spans of the text. */
const skb_attribute_span_t* skb_text_get_attribute_spans(const skb_text_t* text);

/**
 * Appends the contents from other text.
 * @param text pointer to the text to modify
 * @param text_from text to copy from.
 */
void skb_text_append(skb_text_t* text, const skb_text_t* text_from);

/**
 * Appends range of contents from other text.
 * @param text pointer to the text to modify
 * @param from_text text to copy from.
 * @param from_range text range to copy (in utf-32 codepoints).
 */
void skb_text_append_range(skb_text_t* text, const skb_text_t* from_text, skb_range_t from_range);

/**
 * Appends utf-8 string with attributes.
 * @param text pointer to the text to modify
 * @param utf8 pointer to a utf-8 string
 * @param utf8_count length of the utf-8 string, or -1 if the string is zero terminated.
 * @param attributes slice of attributes to apply to the appended text.
 */
void skb_text_append_utf8(skb_text_t* text, const char* utf8, int32_t utf8_count, skb_attribute_set_t attributes);

/**
 * Appends utf-32 string with attributes.
 * @param text pointer to the text to modify
 * @param utf32 pointer to a utf-32 string
 * @param utf32_count length of the utf-32 string, or -1 if the string is zero terminated.
 * @param attributes slice of attributes to apply to the appended text.
 */
void skb_text_append_utf32(skb_text_t* text, const uint32_t* utf32, int32_t utf32_count, skb_attribute_set_t attributes);


/**
 * Replaces portion of text with the contents of given text.
 * @param text pointer to the text to modify
 * @param range text range to replace (in utf-32 codepoints)
 * @param other text to insert.
 */
void skb_text_replace(skb_text_t* text, skb_range_t range, const skb_text_t* other);

/**
 * Replaces portion of text with the contents of given utf-8 string and attributes.
 * @param text pointer to the text to modify
 * @param range text range to replace (in utf-32 codepoints)
 * @param utf8 pointer to the utf-8 string to insert
 * @param utf8_count length of the utf-8 string, or -1 if the string is zero terminated.
 * @param attributes slice of attributes to apply to the inserted text.
 */
void skb_text_replace_utf8(skb_text_t* text, skb_range_t range, const char* utf8, int32_t utf8_count, skb_attribute_set_t attributes);

/**
 * Replaces portion of text with the contents of given utf-32 string and attributes.
 * @param text pointer to the text to modify
 * @param range text range to replace (in utf-32 codepoints)
 * @param utf32 pointer to the utf-32 string to insert
 * @param utf32_count length of the utf-32 string, or -1 if the string is zero terminated.
 * @param attributes slice of attributes to apply to the inserted text.
 */
void skb_text_replace_utf32(skb_text_t* text, skb_range_t range, const uint32_t* utf32, int32_t utf32_count, skb_attribute_set_t attributes);

/**
 * Removes range of text.
 * @param text pointer to the text to modify
 * @param range range of text and attributes (in utf-32 codepoints) to remove.
 */
void skb_text_remove(skb_text_t* text, skb_range_t range);

/**
 * Clears attribute of specific type from specified range.
 * @param text pointer to the text to modify
 * @param range text range of attributes to remove (in utf-32 codepoints)
 * @param attribute_kind tag of the attribute to remove.
 */
void skb_text_clear_attribute(skb_text_t* text, skb_range_t range, uint32_t attribute_kind);

/**
 * Clears all attributes from specified range.
 * @param text pointer to the text to modify
 * @param range text range of attributes to remove (in utf-32 codepoints)
 */
void skb_text_clear_all_attributes(skb_text_t* text, skb_range_t range);

/**
 * Add attribute to specified text range.
 * @param text pointer to the text to modify
 * @param range text range to apply the attributes for (in utf-32 codepoints)
 * @param attribute attribute to add
 */
void skb_text_add_attribute(skb_text_t* text, skb_range_t range, skb_attribute_t attribute);

/**
 * Signature of attribute iterator callback.
 * @param text pointer to the text that is iterated.
 * @param range text range of the run of attributes.
 * @param active_spans array of pointers to active attributes for the range
 * @param active_spans_count number of active attributes
 * @param context context pointer passed to skb_text_iterate_attribute_runs().
 */
typedef void skb_attribute_run_iterator_func_t(const skb_text_t* text, skb_range_t range, skb_attribute_span_t** active_spans, int32_t active_spans_count, void* context);

/**
 * Iterate through combined attributes runs of the text, returning active attributes for given run.
 * @param text text to query.
 * @param callback function to call for each attribute run.
 * @param context context that is passed to the callback.
 */
void skb_text_iterate_attribute_runs(const skb_text_t* text, skb_attribute_run_iterator_func_t* callback, void* context);

/** @} */

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SKB_TEXT_H
