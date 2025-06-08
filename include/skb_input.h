// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_INPUT_H
#define SKB_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "skb_layout.h"

/**
 * @defgroup input Text Input
 * The Text input provides the logic to handle text editing. It takes mouse movement and key presses as input and
 * modifies the text buffer.
 *
 * The text is internally stored as utf-32 (unicode codepoints), the text positions are also tracked as codepoints.
 * There are functions to get utf-8 version of the text out, and skb_utf8_codepoint_offset() can be used to covert
 * the text positions.
 *
 * In order to support partial updates, the text is split into paragraphs at paragraph break characters.
 * Each paragraph has its own layout, which may consist of multiple lines. Externally text positions are
 * tracked as if the text was one big buffer.
 *
 * An user interface with a lot of text fields can usually have just one text input. Each text field is rendered
 * using a layout until the user focuses on the field, in which case the text input is filled with the text, and takes over.
 *
 * @{
 */

/** Opaque type for the text input. Use skb_input_create() to create. */
typedef struct skb_input_t skb_input_t;

/**
 * Signature of text input change function.
 * @param input input that changed.
 * @param context context pointer that was passed to skb_input_set_on_change_callback().
 */
typedef void skb_input_on_change_func_t(skb_input_t* input, void* context);

/** Enum describing the caret movement mode. */
typedef enum {
	/** Skribidi mode, the caret is move in logical order, but the caret makes extra stop when the writing direction
	 * changes to make it easier to place the caret at the start and end of the words. */
	SKB_CARET_MODE_SKRIBIDI = 0,
	/** Simple mode, similar to Windows, the caret moves in logical order, always one grapheme at a time. */
	SKB_CARET_MODE_SIMPLE,
} skb_input_caret_mode_t;

/** Struct describing paramters for the text input. */
typedef struct skb_input_params_t {
	/** Layout parameters used for each paragraph layout. */
	skb_layout_params_t layout_params;
	/** Text attributes for all the text. */
	skb_text_attribs_t text_attribs;
	/** Base direction of the text input. */
	uint8_t base_direction;
	/** Care movement mode */
	skb_input_caret_mode_t caret_mode;
} skb_input_params_t;

/** Keys handled by the input */
typedef enum {
	SKB_KEY_NONE = 0,
	/** Left arrow key */
	SKB_KEY_LEFT,
	/** Right arrow key */
	SKB_KEY_RIGHT,
	/** Up arrow key */
	SKB_KEY_UP,
	/** down arrow key */
	SKB_KEY_DOWN,
	/** Home key */
	SKB_KEY_HOME,
	/** End key */
	SKB_KEY_END,
	/** Backspace key */
	SKB_KEY_BACKSPACE,
	/** Delete key */
	SKB_KEY_DELETE,
	/** Enter key */
	SKB_KEY_ENTER,
} skb_input_key_t;

/** Key modifiers. */
typedef enum {
	SKB_MOD_NONE = 0,
	SKB_MOD_SHIFT = 0x01,
	SKB_MOD_CONTROL = 0x02,
} skb_input_key_mod_t;

/**
 * Creates new input.
 * @param params parameters for the input.
 * @return newly create input.
 */
skb_input_t* skb_input_create(const skb_input_params_t* params);

/**
 * Sets change callback function.
 * @param input input to use.
 * @param callback pointer to the callback function
 * @param context context pointer that is passed to the callback function each time it is called.
 */
void skb_input_set_on_change_callback(skb_input_t* input, skb_input_on_change_func_t* callback, void* context);

/**
 * Destroys an input.
 * @param input pointer to the input to destroy.
 */
void skb_input_destroy(skb_input_t* input);

/**
 * Resets input to empty state.
 * @param input input to use
 * @param params new parameters, or NULL if previous paramters should be kept.
 */
void skb_input_reset(skb_input_t* input, const skb_input_params_t* params);

/**
 * Sets the text of the input from an utf-8 string.
 * @param input input to set.
 * @param temp_alloc temp allocator used while setting layouting the text.
 * @param utf8 pointer to the utf-8 string to set.
 * @param utf8_len length of the string, or -1 if nul terminated.
 */
void skb_input_set_text_utf8(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const char* utf8, int32_t utf8_len);

/**
 * Sets the text of the input from an utf-32 string.
 * @param input input to set.
 * @param temp_alloc temp allocator used while setting layouting the text.
 * @param utf32 pointer to the utf-32 string to set.
 * @param utf32_len length of the string, or -1 if nul terminated.
 */
void skb_input_set_text_utf32(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len);

/** @return length of the edited text as utf-8. */
int32_t skb_input_get_text_utf8_count(const skb_input_t* input);

/**
 * Gets the edited text as utf-8.
 * @param input input to use.
 * @param buf buffer where to store the text.
 * @param buf_cap capacity of the buffer.
 * @return total length of the string (can be larger than buf_cap).
 */
int32_t skb_input_get_text_utf8(const skb_input_t* input, char* buf, int32_t buf_cap);

/** @return length of the edited text as utf-32. */
int32_t skb_input_get_text_utf32_count(const skb_input_t* input);

/**
 * Gets the edited text as utf-32.
 * @param input input to use.
 * @param buf buffer where to store the text.
 * @param buf_cap capacity of the buffer.
 * @return total length of the string (can be larger than buf_cap).
 */
int32_t skb_input_get_text_utf32(const skb_input_t* input, uint32_t* buf, int32_t buf_cap);

/** @return number of paragraphs in the input. */
int32_t skb_input_get_paragraph_count(skb_input_t* input);
/** @return const pointer to specified paragraph. */
const skb_layout_t* skb_input_get_paragraph_layout(skb_input_t* input, int32_t index);
/** @return y-offset of the specified paragraph. */
float skb_input_get_paragraph_offset_y(skb_input_t* input, int32_t index);
/** @return text offset of specified paragraph. */
int32_t skb_input_get_paragraph_text_offset(skb_input_t* input, int32_t index);
/** @return the paramters used to create the input. */
const skb_input_params_t* skb_input_get_params(skb_input_t* input);

/** @return line number of specified text position. */
int32_t skb_input_get_line_index_at(const skb_input_t* input, skb_text_position_t pos);
/** @return column number of specified text position. */
int32_t skb_input_get_column_index_at(const skb_input_t* input, skb_text_position_t pos);
/** @return text offset of specified text position. */
int32_t skb_input_get_text_offset_at(const skb_input_t* input, skb_text_position_t pos);
/** @return true if the character at specified text position is right-to-left writing direction. */
bool skb_input_is_character_rtl_at(const skb_input_t* input, skb_text_position_t pos);
/** @return visual caret at specified text position. */
skb_visual_caret_t skb_input_get_visual_caret(const skb_input_t* input, skb_text_position_t pos);
/**
 * Hit tests the input, and returns text position of the nearest character.
 * @param input input to use.
 * @param type movement type (caret or selection).
 * @param hit_x hit test location x
 * @param hit_y hit test location y
 * @return text position under or nearest to the hit test location.
 */
skb_text_position_t skb_input_hit_test(const skb_input_t* input, skb_movement_type_t type, float hit_x, float hit_y);

/**
 * Sets the current selection of the input to all of the text.
 * @param input input to set.
 */
void skb_input_select_all(skb_input_t* input);

/**
 * Clears the current selection of the input.
 * @param input input to set.
 */
void skb_input_select_none(skb_input_t* input);

/**
 * Sets the current selection of the input to specific range.
 * @param input input to set.
 * @param selection new selection.
 */
void skb_input_select(skb_input_t* input, skb_text_selection_t selection);

/** @return current selection of the input. */
skb_text_selection_t skb_input_get_current_selection(skb_input_t* input);

/**
 * Returns validated text range of specified selection range.
 * @param input input to query.
 * @param selection selection range.
 * @return validated text range.
 */
skb_range_t skb_input_get_selection_text_offset_range(const skb_input_t* input, skb_text_selection_t selection);

/**
 * Returns number of codepoints in the selection.
 * @param input input to query.
 * @param selection selection
 * @return number of codepoints in the selection.
 */
int32_t skb_input_get_selection_count(const skb_input_t* input, skb_text_selection_t selection);


/**
 * Returns set of rectangles that represent the specified selection.
 * Due to bidirectional text the selection in logical order can span across multiple visual rectangles.
 * @param input input to query.
 * @param selection selection to get.
 * @param callback callback to call on each rectangle
 * @param context context passed to the callback.
 */
void skb_input_get_selection_bounds(const skb_input_t* input, skb_text_selection_t selection, skb_selection_rect_func_t* callback, void* context);

/** @return return selection text utf-8 length. */
int32_t skb_input_get_selection_text_utf8_count(const skb_input_t* input, skb_text_selection_t selection);

/**
 * Gets the selection text as utf-8.
 * @param input input to query.
 * @param selection range of text to get.
 * @param buf buffer where to store the text.
 * @param buf_cap capacity of the buffer.
 * @return total length of the selected string (can be larger than buf_cap).
 */
int32_t skb_input_get_selection_text_utf8(const skb_input_t* input, skb_text_selection_t selection, char* buf, int32_t buf_cap);

/** @return return selection text utf-32 length. */
int32_t skb_input_get_selection_text_utf32_count(const skb_input_t* input, skb_text_selection_t selection);

/**
 * Gets the selection text as utf-32.
 * @param input input to query.
 * @param selection range of text to get.
 * @param buf buffer where to store the text.
 * @param buf_cap capacity of the buffer.
 * @return total length of the selected string (can be larger than buf_cap).
 */
int32_t skb_input_get_selection_text_utf32(const skb_input_t* input, skb_text_selection_t selection, uint32_t* buf, int32_t buf_cap);

/**
 * Processed mouse click, and updates internal state.
 * @param input input to update.
 * @param x mouse x location.
 * @param y mouse y location.
 * @param mods key modifiers.
 * @param time time stamp in seconds (used to detect double and triple clicks).
 */
void skb_input_mouse_click(skb_input_t* input, float x, float y, uint32_t mods, double time);

/**
 * Processes mouse drag.
 * @param input input to update.
 * @param x mouse x location
 * @param y mouse y location
 */
void skb_input_mouse_drag(skb_input_t* input, float x, float y);

/**
 * Processes key press.
 * @param input input to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param key key pressed.
 * @param mods key modifiers.
 */
void skb_input_key_pressed(skb_input_t* input, skb_temp_alloc_t* temp_alloc, skb_input_key_t key, uint32_t mods);

/**
 * Inserts codepoint to the text at current caret position.
 * @param input input to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param codepoint codepoint to insert.
 */
void skb_input_insert_codepoint(skb_input_t* input, skb_temp_alloc_t* temp_alloc, uint32_t codepoint);

/**
 * Paste utf-8 text to the current caret position.
 * @param input input to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param utf8 pointer to utf-8 string to paste
 * @param utf8_len length of the string, or -1 if nul terminated.
 */
void skb_input_paste_utf8(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const char* utf8, int32_t utf8_len);

/**
 * Paste utf-32 text to the current caret position.
 * @param input input to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param utf32 pointer to utf-32 string to paste
 * @param utf32_len length of the string, or -1 if nul terminated.
 */
void skb_input_paste_utf32(skb_input_t* input, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len);

/**
 * Deletes current selection.
 * @param input input to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 */
void skb_input_cut(skb_input_t* input, skb_temp_alloc_t* temp_alloc);

/** @} */

#endif // SKB_INPUT_H
