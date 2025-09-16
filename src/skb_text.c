// SPDX-License-Identifier: MIT

#include "skb_text.h"
#include "skb_common.h"
#include "skb_text_internal.h"

#include <assert.h>
#include <string.h>

skb_text_t* skb_text_create(void)
{
	skb_text_t* result = skb_malloc(sizeof(skb_text_t));
	memset(result, 0, sizeof(skb_text_t));
	result->should_free_instance = true;
	return result;
}

skb_text_t skb_text_make_empty(void)
{
	return (skb_text_t){0};
}

void skb_text_destroy(skb_text_t* text)
{
	if (!text) return;
	skb_free(text->text);
	skb_free(text->spans);
	memset(text, 0, sizeof(skb_text_t));
	if (text->should_free_instance)
		skb_free(text);
}

void skb_text_reset(skb_text_t* text)
{
	assert(text);
	text->text_count = 0;
	text->spans_count = 0;
}

void skb_text_reserve(skb_text_t* text, int32_t text_count, int32_t spans_count)
{
	assert(text);
	SKB_ARRAY_RESERVE(text->text, text_count);
	SKB_ARRAY_RESERVE(text->spans, spans_count);
}

int32_t skb_text_get_utf32_count(const skb_text_t* text)
{
	return text ? text->text_count : 0;
}

const uint32_t* skb_text_get_utf32(const skb_text_t* text)
{
	return text ? text->text : NULL;
}

int32_t skb_text_get_attribute_spans_count(const skb_text_t* text)
{
	return text ? text->spans_count : 0;
}

const skb_attribute_span_t* skb_text_get_attribute_spans(const skb_text_t* text)
{
	return text ? text->spans : NULL;
}


static skb_range_t skb__clamp_text_range(const skb_text_t* text, skb_range_t range)
{
	return (skb_range_t) {
		.start = skb_clampi(range.start, 0, text->text_count),
		.end = skb_clampi(range.end, range.start, text->text_count)
	};
}

static void skb__span_remove(skb_text_t* text, int32_t idx)
{
	assert(idx >= 0 && idx < text->spans_count);

	for (int32_t i = idx; i < text->spans_count - 1; i++)
		text->spans[i] = text->spans[i + 1];
	text->spans_count--;
}

static int32_t skb__spans_lower_bound(const skb_text_t* text, int32_t pos)
{
	int32_t low = 0;
	int32_t high = text->spans_count;
	while (low < high) {
		const int32_t mid = low + (high - low) / 2;
		if (text->spans[mid].text_range.start < pos)
			low = mid + 1;
		else
			high = mid;
	}
	return low;
}

static int32_t skb__span_insert(skb_text_t* text, skb_range_t text_range, skb_attribute_t attribute)
{
	assert(text);
	assert(text_range.start <= text_range.end);

	// Find location to insert at
	const int32_t idx = skb__spans_lower_bound(text, text_range.start);

	SKB_ARRAY_RESERVE(text->spans, text->spans_count + 1);
	text->spans_count++;

	for (int32_t i = text->spans_count - 1; i > idx; i--)
		text->spans[i] = text->spans[i - 1];

	text->spans[idx].text_range = text_range;
	text->spans[idx].attribute = attribute;

	return idx;
}


static int32_t skb__remove_from_active(skb_attribute_span_t** active_spans, int32_t active_spans_count, int32_t active_idx)
{
	// Remove, keep order.
	active_spans_count--;
	for (int32_t i = active_idx; i < active_spans_count; i++)
		active_spans[i] = active_spans[i + 1];
	return active_spans_count;
}

static int32_t skb__insert_to_active(skb_attribute_span_t** active_spans, int32_t active_spans_count, skb_attribute_span_t* span)
{
	assert(active_spans_count < SKB_MAX_ACTIVE_ATTRIBUTES);

	// Keep in order of first to expire first.
	const int32_t end = span->text_range.end;
	int32_t idx = 0;
	while (idx < active_spans_count) {
		if (end < active_spans[idx]->text_range.end)
			break;
		idx++;
	}

	active_spans_count++;

	// Make space
	for (int32_t j = active_spans_count - 1; j > idx; j--)
		active_spans[j] = active_spans[j - 1];
	active_spans[idx] = span;

	return active_spans_count;
}

static int32_t skb__find_active(skb_attribute_span_t** active_spans, int32_t active_spans_count, int32_t ends_at, const skb_attribute_t* attribute)
{
	for (int32_t i = 0; i < active_spans_count; i++) {
		if (active_spans[i]->text_range.end == ends_at && memcmp(&active_spans[i]->attribute, attribute, sizeof(skb_attribute_t)) == 0)
			return i;
	}
	return SKB_INVALID_INDEX;
}

static void skb__attributes_merge_adjacent(skb_text_t* text)
{
	assert(text);

	skb_attribute_span_t* active_spans[SKB_MAX_ACTIVE_ATTRIBUTES];
	int32_t active_spans_count = 0;

	int32_t span_idx = 0;
	while (span_idx < text->spans_count) {
		const int32_t pos = text->spans[span_idx].text_range.start;

		// Add new active spans that start at this event.
		while (span_idx < text->spans_count && text->spans[span_idx].text_range.start == pos) {
			// If a span of same type is adjacent to span of same type, merge.
			const int32_t adjacent_idx = skb__find_active(active_spans, active_spans_count, pos, &text->spans[span_idx].attribute);
			if (adjacent_idx != SKB_INVALID_INDEX) {
				// Merge, and remove current span.
				active_spans[adjacent_idx]->text_range.end = text->spans[span_idx].text_range.end;
				skb__span_remove(text, span_idx);
			} else {
				// Add, keep in order of first to expire first.
				active_spans_count = skb__insert_to_active(active_spans, active_spans_count, &text->spans[span_idx]);
				span_idx++;
			}
		}

		// Expire active spans
		for (int32_t i = 0; i < active_spans_count; i++) {
			if (active_spans[i]->text_range.end <= pos) {
				// Remove, keep order.
				active_spans_count = skb__remove_from_active(active_spans, active_spans_count, i);
				i--;
			}
		}
	}
}

// Clear removes attribute from specified range. Does not alter length.
static void skb__attributes_clear(skb_text_t* text, skb_range_t range, uint32_t attribute_kind)
{
	assert(text);

	if (range.start >= range.end)
		return;

	// Remove existing
	for (int32_t i = 0; i < text->spans_count; i++) {
		skb_attribute_span_t* span = &text->spans[i];

		if (span->attribute.kind != attribute_kind || attribute_kind == 0)
			continue;

		// If clear completely range is before, skip.
		if (range.end <= span->text_range.start)
			continue;
		// If clear completely range is after, skip.
		if (range.start >= span->text_range.end)
			continue;

		if (range.start <= span->text_range.start) {
			if (range.end >= span->text_range.end) {
				// Text range completely covers the whole span, remove
				skb__span_remove(text, i);
				i--;
			} else {
				// Covers start partially, trim.
				span->text_range.start = range.end;
			}
		} else {
			if (range.end >= span->text_range.end) {
				// Covers end partially, trim.
				span->text_range.end = range.start;
			} else {
				// Is inside the span, split.
				const skb_range_t tail_range = {range.end, span->text_range.end};
				// Trim head
				span->text_range.end = range.start;
				// Add tail
				skb__span_insert(text, tail_range, span->attribute);
			}
		}
	}
}

// Removes all attributes from 'range' and replaces it with empty segment.
void skb__attributes_replace_with_empty(skb_text_t* text, skb_range_t range, int32_t empty_count)
{
	assert(text);
	assert(empty_count >= 0);
	assert(range.start <= range.end);

	const int32_t offset = -(range.end - range.start) + empty_count;

	for (int32_t i = 0; i < text->spans_count; i++) {
		skb_attribute_span_t* span = &text->spans[i];

		// If text range is before, offset and skip.
		if (range.end <= span->text_range.start) {
			span->text_range.start += offset;
			span->text_range.end += offset;
			continue;
		}

		// If text range is after, skip.
		if (range.start >= span->text_range.end)
			continue;

		if (range.start <= span->text_range.start) {
			if (range.end >= span->text_range.end) {
				// Text range completely covers the whole span, remove
				skb__span_remove(text, i);
				i--;
			} else {
				// Covers start partially, trim.
				span->text_range.start = range.end + offset;
				span->text_range.end += offset;
			}
		} else {
			if (range.end >= span->text_range.end) {
				// Covers end partially, trim.
				span->text_range.end = range.start;
			} else {
				// Is inside the span, split.
				const skb_range_t tail_range = {range.end, span->text_range.end}; // No offset for the tail span, as the span is assumed to be placed somewhere after the current span.
				// Trim head
				span->text_range.end = range.start;
				// Add tail
				int32_t idx = skb__span_insert(text, tail_range, span->attribute);
				assert(idx > i); // we assume that the tail will come after this span, but not sure where.
			}
		}
	}
}

void skb__attributes_replace(skb_text_t* text, skb_range_t range, int32_t text_count, skb_attribute_slice_t attributes)
{
	skb__attributes_replace_with_empty(text, range, text_count);

	skb_range_t insert_range = {.start = range.start, .end = range.start + text_count};
	for (int32_t i = 0; i < attributes.count; i++)
		skb__span_insert(text, insert_range, attributes.items[i]);

	skb__attributes_merge_adjacent(text);
}


void skb_text_append(skb_text_t* text, const skb_text_t* text_from)
{
	assert(text);

	if (!text_from || !text_from->text_count)
		return;

	SKB_ARRAY_RESERVE(text->text, text->text_count + text_from->text_count);
	const int32_t start_offset = text->text_count;

	// Copy text
	memcpy(text->text + start_offset, text_from->text, text_from->text_count * sizeof(uint32_t));
	text->text_count += text_from->text_count;

	// Copy attributes
	if (text_from->spans_count > 0) {
		SKB_ARRAY_RESERVE(text->spans, text->spans_count + text_from->spans_count);
		for (int32_t i = 0; i < text_from->spans_count; i++) {
			skb_range_t span_range = text_from->spans[i].text_range;
			span_range.start += start_offset;
			span_range.end += start_offset;
			skb__span_insert(text, span_range, text_from->spans[i].attribute);
		}
		skb__attributes_merge_adjacent(text);
	}
}

void skb_text_append_range(skb_text_t* text, const skb_text_t* from_text, skb_range_t range)
{
	assert(text);

	if (!from_text || !from_text->text_count)
		return;

	range = skb__clamp_text_range(from_text, range);
	const int32_t copy_offset = range.start;
	const int32_t copy_count = range.end - range.start;

	if (copy_count <= 0)
		return;

	SKB_ARRAY_RESERVE(text->text, text->text_count + copy_count);
	const int32_t start_offset = text->text_count;

	// Copy text
	memcpy(text->text + start_offset, from_text->text + copy_offset, copy_count * sizeof(uint32_t));
	text->text_count += copy_count;

	// Copy attributes
	if (from_text->spans_count > 0) {
		const int32_t span_offset = start_offset - copy_offset;
		for (int32_t i = 0; i < from_text->spans_count; i++) {
			const skb_attribute_span_t* span = &from_text->spans[i];
			skb_range_t span_range = {
				.start = skb_maxi(span->text_range.start, range.start) + span_offset,
				.end = skb_mini(span->text_range.end, range.end) + span_offset,
			};
			if (span_range.end > span_range.start)
				skb__span_insert(text, span_range, span->attribute);
		}
		skb__attributes_merge_adjacent(text);
	}
}

void skb_text_append_utf8(skb_text_t* text, const char* utf8, int32_t utf8_count, skb_attribute_slice_t attributes)
{
	assert(text);
	assert(utf8);

	if (utf8_count < 0) utf8_count = (int32_t)strlen(utf8);

	const int32_t utf32_count = skb_utf8_to_utf32_count(utf8, utf8_count);
	SKB_ARRAY_RESERVE(text->text, text->text_count + utf32_count);

	const skb_range_t range = {
		.start = text->text_count,
		.end = text->text_count + utf32_count,
	};

	skb_utf8_to_utf32(utf8, utf8_count, text->text + text->text_count, utf32_count);
	text->text_count += utf32_count;

	for (int32_t i = 0; i < attributes.count; i++)
		skb__span_insert(text, range, attributes.items[i]);
}

void skb_text_append_utf32(skb_text_t* text, const uint32_t* utf32, int32_t utf32_count, skb_attribute_slice_t attributes)
{
	assert(text);
	assert(utf32);

	if (utf32_count < 0) utf32_count = skb_utf32_strlen(utf32);

	SKB_ARRAY_RESERVE(text->text, text->text_count + utf32_count);

	const skb_range_t range = {
		.start = text->text_count,
		.end = text->text_count + utf32_count,
	};

	memcpy(text->text + text->text_count, utf32, utf32_count * sizeof(uint32_t));
	text->text_count += utf32_count;

	for (int32_t i = 0; i < attributes.count; i++)
		skb__span_insert(text, range, attributes.items[i]);
}

void skb_text_replace(skb_text_t* text, skb_range_t range, const skb_text_t* other)
{
	assert(text);
	assert(other);

	range = skb__clamp_text_range(text, range);

	const int32_t remove_count = range.end - range.start;

	SKB_ARRAY_RESERVE(text->text, text->text_count + other->text_count - remove_count);
	const int32_t start_offset = text->text_count;

	// Make space for the inserted text
	memmove(text->text + range.start + other->text_count, text->text + range.end, (text->text_count - range.end) * sizeof(uint32_t));

	// Copy
	memcpy(text->text + range.start, other->text, other->text_count * sizeof(uint32_t));
	text->text_count += other->text_count - remove_count;

	// Make space for attributes.
	skb__attributes_replace_with_empty(text, range, text->text_count);

	// Insert existing spans
	SKB_ARRAY_RESERVE(text->spans, text->spans_count + other->spans_count);
	for (int32_t i = 0; i < other->spans_count; i++) {
		skb_range_t span_range = other->spans[i].text_range;
		span_range.start += start_offset;
		span_range.end += start_offset;
		skb__span_insert(text, span_range, other->spans[i].attribute);
	}

	skb__attributes_merge_adjacent(text);
}

void skb_text_replace_utf8(skb_text_t* text, skb_range_t range, const char* utf8, int32_t utf8_count, skb_attribute_slice_t attributes)
{
	assert(text);
	assert(utf8);

	range = skb__clamp_text_range(text, range);

	const int32_t remove_count = range.end - range.start;

	if (utf8_count < 0) utf8_count = (int32_t)strlen(utf8);
	const int32_t utf32_count = skb_utf8_to_utf32_count(utf8, utf8_count);

	SKB_ARRAY_RESERVE(text->text, text->text_count + utf32_count - remove_count);

	// Make space for the inserted text
	memmove(text->text + range.start + utf32_count, text->text + range.end, (text->text_count - range.end) * sizeof(uint32_t));

	// Copy
	skb_utf8_to_utf32(utf8, utf8_count, text->text + range.start, utf32_count);
	text->text_count += utf32_count - remove_count;

	// Replace attributes
	skb__attributes_replace(text, range, utf32_count, attributes);
}

void skb_text_replace_utf32(skb_text_t* text, skb_range_t range, const uint32_t* utf32, int32_t utf32_count, skb_attribute_slice_t attributes)
{
	assert(text);
	assert(utf32);

	range = skb__clamp_text_range(text, range);

	const int32_t remove_count = range.end - range.start;

	if (utf32_count < 0) utf32_count = skb_utf32_strlen(utf32);

	SKB_ARRAY_RESERVE(text->text, text->text_count + utf32_count - remove_count);

	// Make space for the inserted text
	memmove(text->text + range.start + utf32_count, text->text + range.end, (text->text_count - range.end) * sizeof(uint32_t));

	// Copy
	memcpy(text->text + range.start, utf32, utf32_count * sizeof(uint32_t));
	text->text_count += utf32_count - remove_count;

	// Replace attributes
	skb__attributes_replace(text, range, utf32_count, attributes);
}

void skb_text_remove(skb_text_t* text, skb_range_t range)
{
	assert(text);

	range = skb__clamp_text_range(text, range);
	if (range.end <= range.start) return;

	// Remove text
	memmove(text->text + range.start, text->text + range.end, (text->text_count - range.end) * sizeof(uint32_t));
	text->text_count -= range.end - range.start;

	// Remove attributes
	skb__attributes_replace(text, range, 0, (skb_attribute_slice_t){0});
}

void skb_text_clear_attribute(skb_text_t* text, skb_range_t range, uint32_t attribute_kind)
{
	assert(text);

	range = skb__clamp_text_range(text, range);

	skb__attributes_clear(text, range, attribute_kind);
}

void skb_text_clear_all_attributes(skb_text_t* text, skb_range_t range)
{
	assert(text);

	range = skb__clamp_text_range(text, range);

	skb__attributes_clear(text, range, 0);
}

void skb_text_add_attribute(skb_text_t* text, skb_range_t range, skb_attribute_t attribute)
{
	assert(text);

	range = skb__clamp_text_range(text, range);

	skb__attributes_clear(text, range, attribute.kind);
	skb__span_insert(text, range, attribute);
	skb__attributes_merge_adjacent(text);
}

void skb_text_iterate_attribute_runs(const skb_text_t* text, skb_attribute_run_iterator_func_t* callback, void* context)
{
	assert(text);
	assert(callback);

	skb_attribute_span_t* active_spans[SKB_MAX_ACTIVE_ATTRIBUTES];
	int32_t active_spans_count = 0;
	int32_t start_pos = 0;

	int32_t span_idx = 0;
	while (span_idx < text->spans_count) {
		const int32_t pos = text->spans[span_idx].text_range.start;

		// Expire active spans
		for (int32_t i = 0; i < active_spans_count; i++) {
			if (active_spans[i]->text_range.end <= pos) {

				const skb_range_t range = { .start = start_pos, .end = active_spans[i]->text_range.end };
				callback(text, range, active_spans, active_spans_count, context);
				start_pos = active_spans[i]->text_range.end;

				// Remove, keep order.
				active_spans_count = skb__remove_from_active(active_spans, active_spans_count, i);
				i--;
			}
		}

		if (start_pos < pos) {
			const skb_range_t range = { .start = start_pos, .end = pos };
			callback(text, range, active_spans, active_spans_count, context);
			start_pos = pos;
		}

		// Add new active spans that start at this event.
		while (span_idx < text->spans_count && text->spans[span_idx].text_range.start == pos) {
			// Add, keep in order of first to expire first.
			active_spans_count = skb__insert_to_active(active_spans, active_spans_count, &text->spans[span_idx]);
			span_idx++;
		}
	}

	// Expire remaining active spans
	for (int32_t i = 0; i < active_spans_count; i++) {
		int32_t pos = active_spans[i]->text_range.end;
		if (start_pos < pos) {
			const skb_range_t range = { .start = start_pos, .end = pos };
			callback(text, range, active_spans + i, active_spans_count - i, context);
			start_pos = pos;
		}
	}

	// The rest of the text
	if (start_pos < text->text_count) {
		const skb_range_t range = { .start = start_pos, .end = text->text_count };
		callback(text, range, NULL, 0, context);
	}
}
