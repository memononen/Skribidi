// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#ifndef SKB_EDITOR_H
#define SKB_EDITOR_H

#include <stdint.h>
#include "skb_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup editor Text Editor
 * The Text editor provides the logic to handle text editing. It takes mouse movement and key presses as input and
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
 * An user interface with a lot of text fields can usually have just one text editor. Each text field is rendered
 * using a layout until the user focuses on the field, in which case the text editor is filled with the text, and takes over.
 *
 * @{
 */

// Forward declarations
typedef struct skb_rich_text_t skb_rich_text_t;
typedef struct skb_rich_layout_t skb_rich_layout_t;

/** Opaque type for the text editor. Use skb_editor_create() to create. */
typedef struct skb_editor_t skb_editor_t;

/**
 * Signature of text editor change function.
 * @param editor editor that changed.
 * @param context context pointer that was passed to skb_editor_set_on_change_callback().
 */
typedef void skb_editor_on_change_func_t(skb_editor_t* editor, void* context);

/**
 * Signature of input filter function.
 * The input filter is called when text is being input to the editor, but before it is actually placed.
 * The filter function can adjust the input_text as it sees fit.
 * Not called during undo, or when the editor text is reset using set text.
 * @param editor editor that changed.
 * @param input_text text that is being input (mutable).
 * @param selection Location of text that will be replaced with the input text.
 * @param context context pointer that was passed to skb_editor_set_input_filter_callback().
 */
typedef void skb_editor_input_filter_func_t(skb_editor_t* editor, skb_rich_text_t* input_text, skb_text_selection_t selection, void* context);

/** Enum describing the caret movement mode. */
typedef enum {
	/** Skribidi mode, the caret is move in logical order, but the caret makes extra stop when the writing direction
	 * changes to make it easier to place the caret at the start and end of the words. */
	SKB_CARET_MODE_SKRIBIDI = 0,
	/** Simple mode, similar to Windows, the caret moves in logical order, always one grapheme at a time. */
	SKB_CARET_MODE_SIMPLE,
} skb_editor_caret_mode_t;

/** Enum describing the behavior mode for editor operations. */
typedef enum {
	/** Default mode, standard behavior. */
	SKB_BEHAVIOR_DEFAULT = 0,
	/** MacOS mode, option+arrow and command+arrow follow MacOS text editing conventions. */
	SKB_BEHAVIOR_MACOS,
} skb_editor_behavior_t;

/** Struct describing parameters for the text editor. */
typedef struct skb_editor_params_t {
	/** Pointer to font collection to use. */
	skb_font_collection_t* font_collection;
	/** Pointer to the icon collection to use. */
	skb_icon_collection_t* icon_collection;
	/** Pointer to the attribute collection to use. */
	skb_attribute_collection_t* attribute_collection;
	/** Layout box width. Used for alignment, wrapping, and overflow (will be passed to layout width) */
	float editor_width;
	/** Attributes to apply for the layout. Text attributes, and attributes from attributed text are added on top. */
	skb_attribute_set_t layout_attributes;
	/** Attributes to apply for all the text. */
	skb_attribute_set_t paragraph_attributes;
	/** Attributes added for the IME composition text. */
	skb_attribute_set_t composition_attributes;
	/** Care movement mode */
	skb_editor_caret_mode_t caret_mode;
	/** Behavior mode for editor operations (default vs macOS style). This includes how keyboard
	 * navigation works in the text editor. */
	skb_editor_behavior_t editor_behavior;
	/** Maximum number of undo levels, if zero, set to default undo levels, if < 0 undo is disabled. */
	int32_t max_undo_levels;
} skb_editor_params_t;

/** Keys handled by the editor */
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
} skb_editor_key_t;

/** Key modifiers. */
typedef enum {
	SKB_MOD_NONE = 0,
	SKB_MOD_SHIFT = 0x01,
	SKB_MOD_CONTROL = 0x02,
	SKB_MOD_OPTION = 0x04,
	SKB_MOD_COMMAND = 0x08,
} skb_editor_key_mod_t;

/**
 * Creates new text editor.
 * @param params parameters for the editor.
 * @return newly create editor.
 */
skb_editor_t* skb_editor_create(const skb_editor_params_t* params);

/**
 * Sets change callback function.
 * @param editor editor to change.
 * @param on_change_func pointer to the on change callback function
 * @param context context pointer that is passed to the callback function each time it is called.
 */
void skb_editor_set_on_change_callback(skb_editor_t* editor, skb_editor_on_change_func_t* on_change_func, void* context);

/**
 * Sets input filter function.
 * The function is called when text is input to the editor. The filter function can change the text as needed.
 * @param editor editor to change
 * @param filter_func pointer to the filter functions
 * @param context context pointer that is passed to the callback function each time it is called.
 */
void skb_editor_set_input_filter_callback(skb_editor_t* editor, skb_editor_input_filter_func_t* filter_func, void* context);

/**
 * Destroys a text editor.
 * @param editor pointer to the editor to destroy.
 */
void skb_editor_destroy(skb_editor_t* editor);

/**
 * Resets text editor to empty state.
 * @param editor editor to change
 * @param params new parameters, or NULL if previous paramters should be kept.
 */
void skb_editor_reset(skb_editor_t* editor, const skb_editor_params_t* params);

/**
 * Sets the text of the editor from an utf-8 string.
 * @param editor editor to change.
 * @param temp_alloc temp allocator used while setting layouting the text.
 * @param utf8 pointer to the utf-8 string to set.
 * @param utf8_len length of the string, or -1 if nul terminated.
 */
void skb_editor_set_text_utf8(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const char* utf8, int32_t utf8_len);

/**
 * Sets the text of the editor from an utf-32 string.
 * @param editor editor to change.
 * @param temp_alloc temp allocator used while setting layouting the text.
 * @param utf32 pointer to the utf-32 string to set.
 * @param utf32_len length of the string, or -1 if nul terminated.
 */
void skb_editor_set_text_utf32(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len);

/**
 * Sets the text of the editor from an attributed text.
 * @param editor editor to change.
 * @param temp_alloc temp allocator used while setting layouting the text.
 * @param text pointer to the attributed text to set.
 */
void skb_editor_set_text(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_text_t* text);

/** @return length of the edited text as utf-8. */
int32_t skb_editor_get_text_utf8_count(const skb_editor_t* editor);

/**
 * Gets the edited text as utf-8.
 * @param editor editor to query.
 * @param utf8 buffer where to store the text.
 * @param utf8_cap capacity of the buffer.
 * @return total length of the string (can be larger than buf_cap).
 */
int32_t skb_editor_get_text_utf8(const skb_editor_t* editor, char* utf8, int32_t utf8_cap);

/** @return length of the edited text as utf-32. */
int32_t skb_editor_get_text_utf32_count(const skb_editor_t* editor);

/**
 * Gets the edited text as utf-32.
 * @param editor editor to query.
 * @param utf32 buffer where to store the text.
 * @param utf32_cap capacity of the buffer.
 * @return total length of the string (can be larger than buf_cap).
 */
int32_t skb_editor_get_text_utf32(const skb_editor_t* editor, uint32_t* utf32, int32_t utf32_cap);

/**
 * Gets the edited rich text.
 * @param editor editor to query
 * @return const pointer to the rich text being edited.
 */
const skb_rich_text_t* skb_editor_get_rich_text(const skb_editor_t* editor);

/**
 * Gets the rich layout of the edited text.
 * @param editor editor to query
 * @return const pointer to the rich layout of edited text.
 */
const skb_rich_layout_t* skb_editor_get_rich_layout(const skb_editor_t* editor);

/** @return number of paragraphs in the editor. */
int32_t skb_editor_get_paragraph_count(const skb_editor_t* editor);

/** @return const pointer to layout of specified paragraph. */
const skb_layout_t* skb_editor_get_paragraph_layout(const skb_editor_t* editor, int32_t paragraph_idx);

/** @return y-offset of the specified paragraph. */
float skb_editor_get_paragraph_offset_y(const skb_editor_t* editor, int32_t paragraph_idx);

/** @return y-advance (advance to the start of next paragraph) of the specified paragraph. */
float skb_editor_get_paragraph_advance_y(const skb_editor_t* editor, int32_t paragraph_idx);

/** @return const pointer to the text of the specified paragraph. */
const skb_text_t* skb_editor_get_paragraph_text(const skb_editor_t* editor, int32_t paragraph_idx);

/** @return text count of specified paragraph. */
int32_t skb_editor_get_paragraph_text_count(const skb_editor_t* editor, int32_t paragraph_idx);

/** @return attribute set applied to specified paragraph. */
skb_attribute_set_t skb_editor_get_paragraph_attributes(const skb_editor_t* editor, int32_t paragraph_idx);

/** @return global text offset of specified paragraph. */
int32_t skb_editor_get_paragraph_global_text_offset(const skb_editor_t* editor, int32_t paragraph_idx);

/** @return the parameters used to create the editor. */
const skb_editor_params_t* skb_editor_get_params(const skb_editor_t* editor);

/** @return line number of specified text position. */
int32_t skb_editor_get_line_index_at(const skb_editor_t* editor, skb_text_position_t pos);

/** @return column number of specified text position. */
int32_t skb_editor_get_column_index_at(const skb_editor_t* editor, skb_text_position_t pos);

/** @return text offset of specified text position. */
int32_t skb_editor_text_position_to_text_offset(const skb_editor_t* editor, skb_text_position_t pos);

/**
 * Returns paragraph position based on text position.
 * Paragraph position contains more information how the text position relates to the paragraph at the text position.
 * @param editor editor to query.
 * @param pos the text position to convert
 * @return paragraph position info of the specified text position.
 */
skb_paragraph_position_t skb_editor_text_position_to_paragraph_position(const skb_editor_t* editor, skb_text_position_t pos);

/** @return text direction at specified text position. */
skb_text_direction_t skb_editor_get_text_direction_at(const skb_editor_t* editor, skb_text_position_t pos);

/** @return visual caret at specified text position. */
skb_visual_caret_t skb_editor_get_visual_caret(const skb_editor_t* editor, skb_text_position_t pos);

/**
 * Hit tests the editor, and returns text position of the nearest character.
 * @param editor editor to query.
 * @param type movement type (caret or selection).
 * @param hit_x hit test location x
 * @param hit_y hit test location y
 * @return text position under or nearest to the hit test location.
 */
skb_text_position_t skb_editor_hit_test(const skb_editor_t* editor, skb_movement_type_t type, float hit_x, float hit_y);

/**
 * Sets the current selection of the editor to all the text.
 * @param editor editor to change.
 */
void skb_editor_select_all(skb_editor_t* editor);

/**
 * Clears the current selection of the editor.
 * @param editor editor to change.
 */
void skb_editor_select_none(skb_editor_t* editor);

/**
 * Sets the current selection of the editor to specific range.
 * @param editor editor to change.
 * @param selection new selection.
 */
void skb_editor_select(skb_editor_t* editor, skb_text_selection_t selection);

/** @return current selection of the editor. */
skb_text_selection_t skb_editor_get_current_selection(const skb_editor_t* editor);

/**
 * Returns validated text range of specified selection range.
 * @param editor editor to query.
 * @param selection selection range.
 * @return validated text range.
 */
skb_range_t skb_editor_get_selection_text_offset_range(const skb_editor_t* editor, skb_text_selection_t selection);

/**
 * Returns number of codepoints in the selection.
 * @param editor editor to query.
 * @param selection selection
 * @return number of codepoints in the selection.
 */
int32_t skb_editor_get_selection_count(const skb_editor_t* editor, skb_text_selection_t selection);

/**
 * Returns the range of paragraphs the selection overlaps.
 * @param editor editor to qeury
 * @param selection selection
 * @return range of paragraphs the selection overlaps.
 */
skb_range_t skb_editor_get_selection_paragraphs_range(const skb_editor_t* editor, skb_text_selection_t selection);

/**
 * Returns set of rectangles that represent the specified selection.
 * Due to bidirectional text, the selection in logical order can span across multiple visual rectangles.
 * @param editor editor to query.
 * @param selection selection to get.
 * @param callback callback to call on each rectangle
 * @param context context passed to the callback.
 */
void skb_editor_get_selection_bounds(const skb_editor_t* editor, skb_text_selection_t selection, skb_selection_rect_func_t* callback, void* context);

/** @return return the text length in utf-8 of specified selection. */
int32_t skb_editor_get_selection_text_utf8_count(const skb_editor_t* editor, skb_text_selection_t selection);

/**
 * Gets the text of the specified selection text as utf-8.
 * @param editor editor to query.
 * @param selection range of text to get.
 * @param utf8 buffer where to store the selected text.
 * @param utf8_cap capacity of the buffer.
 * @return total length of the selected string (can be larger than buf_cap).
 */
int32_t skb_editor_get_selection_text_utf8(const skb_editor_t* editor, skb_text_selection_t selection, char* utf8, int32_t utf8_cap);

/** @return return the text length in utf-32 of specified selection. */
int32_t skb_editor_get_selection_text_utf32_count(const skb_editor_t* editor, skb_text_selection_t selection);

/**
 * Gets the text of the specified selection text as utf-32.
 * @param editor editor to query.
 * @param selection range of text to get.
 * @param utf32 buffer where to store the selected text.
 * @param utf32_cap capacity of the buffer.
 * @return total length of the selected string (can be larger than buf_cap).
 */
int32_t skb_editor_get_selection_text_utf32(const skb_editor_t* editor, skb_text_selection_t selection, uint32_t* utf32, int32_t utf32_cap);

/**
 * Gets the text of the specified selection attributed text.
 * @param editor editor to query.
 * @param selection range of text to get.
 * @param rich_text rich text where to store the selected text.
 */
void skb_editor_get_selection_rich_text(const skb_editor_t* editor, skb_text_selection_t selection, skb_rich_text_t* rich_text);

/**
 * Processes mouse click, and updates internal state.
 * @param editor editor to update.
 * @param x mouse x location.
 * @param y mouse y location.
 * @param mods key modifiers.
 * @param time time stamp in seconds (used to detect double and triple clicks).
 */
void skb_editor_process_mouse_click(skb_editor_t* editor, float x, float y, uint32_t mods, double time);

/**
 * Processes mouse drag.
 * @param editor editor to update.
 * @param x mouse x location
 * @param y mouse y location
 */
void skb_editor_process_mouse_drag(skb_editor_t* editor, float x, float y);

/**
 * Processes key press.
 * @param editor editor to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param key key pressed.
 * @param mods key modifiers.
 */
void skb_editor_process_key_pressed(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_editor_key_t key, uint32_t mods);

/**
 * Inserts new paragraph at the current selection location. This is equivalent of pressing enter at the current caret position.
 * @param editor editor to update
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param paragraph_attribute attribute to set on the new paragraph, empty attribute will be ignored.
 */
void skb_editor_insert_paragraph(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_attribute_t paragraph_attribute);

/**
 * Inserts codepoint to the text at current caret position.
 * @param editor editor to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param codepoint codepoint to insert.
 */
void skb_editor_insert_codepoint(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, uint32_t codepoint);

/**
 * Paste utf-8 text to the current caret position.
 * @param editor editor to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param utf8 pointer to utf-8 string to paste
 * @param utf8_len length of the string, or -1 if nul terminated.
 */
void skb_editor_paste_utf8(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const char* utf8, int32_t utf8_len);

/**
 * Paste utf-32 text to the current caret position.
 * @param editor editor to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param utf32 pointer to utf-32 string to paste
 * @param utf32_len length of the string, or -1 if nul terminated.
 */
void skb_editor_paste_utf32(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len);

/**
 * Paste attributed text to the current caret position.
 * @param editor editor to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param text attributed text to paste
 */
void skb_editor_paste_text(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_text_t* text);

/**
 * Paste attributed text to the current caret position.
 * @param editor editor to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param rich_text rich text to paste
 */
void skb_editor_paste_rich_text(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const skb_rich_text_t* rich_text);

/**
 * Deletes current selection.
 * @param editor editor to update.
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 */
void skb_editor_cut(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc);

/**
 * Toggles attribute for the current selection.
 * If the whole current selection range has the specified attribute, the attribute is cleared, else the attribute is set.
 * If the current selection is empty, the active attributes will be changed instead. Active attributes define what style is applied to the next text that is inserted.
 * This is useful for attributes like bold, italic, and underline.
 * @param editor editor to update
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param attribute attribute to toggle.
 */
void skb_editor_toggle_attribute(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_attribute_t attribute);

/**
 * Applies attribute for the current selection.
 * Sets attribute the whole current selection range, overriding any attribute of same kind.
 * If the current selection is empty, the active attributes will be changed instead. Active attributes define what style is applied to the next text that is inserted.
 * This is useful for attributes like font size or color.
 * @param editor editor to update
 * @param temp_alloc temp alloc to use for text modifications and relayout.
 * @param attribute attribute to apply.
 */
void skb_editor_apply_attribute(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_attribute_t attribute);

/**
 * Sets attribute for specified range, overriding any attribute of same kind.
 * @param editor editor to update
 * @param temp_alloc temp alloc to use for text modifications and relayout
 * @param selection range of text to change
 * @param attribute attribute to set.
 */
void skb_editor_set_attribute(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_text_selection_t selection, skb_attribute_t attribute);

/**
 * Clears attribute for specified selection range.
 * @param editor editor to update
 * @param temp_alloc temp alloc to use for text modifications and relayout
 * @param selection range of text to change
 * @param attribute attribute to clear.
 */
void skb_editor_clear_attribute(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_text_selection_t selection, skb_attribute_t attribute);

/**
 * Clears all attributes for specified selection range.
 * @param editor editor to update
 * @param temp_alloc temp alloc to use for text modifications and relayout
 * @param selection range of text to change
 */
void skb_editor_clear_all_attributes(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_text_selection_t selection);

/**
 * Sets attribute for paragraphs in specified selection range, overriding any attribute of same kind.
 * @param editor editor to update
 * @param temp_alloc temp alloc to use for text modifications and relayout
 * @param selection range of text to change
 * @param attribute attribute to set.
 */
void skb_editor_set_paragraph_attribute(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_text_selection_t selection, skb_attribute_t attribute);

/**
 * Applies delta change to attribute for paragraphs in specified selection range.
 * Note: currently only indent level attribute is supported.
 * @param editor editor to update
 * @param temp_alloc temp alloc to use for text modifications and relayout
 * @param selection range of text to change
 * @param attribute attribute delta to apply.
 */
void skb_editor_apply_paragraph_attribute_delta(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, skb_text_selection_t selection, skb_attribute_t attribute);

/**
 * Counts number of codepoints the attribute is applied in the selection range.
 * @param editor editor to update
 * @param selection range of text to query
 * @param attribute_kind attribute to query
 * @return number of codepoints the attribute is applied to
 */
int32_t skb_editor_get_attribute_count(const skb_editor_t* editor, skb_text_selection_t selection, uint32_t attribute_kind);

/** @return number of active attributes. Active attributes define what style is applied to the next text that is inserted. */
int32_t skb_editor_get_active_attributes_count(const skb_editor_t* editor);

/** @return constpointer to active attributes. Active attributes define what style is applied to the next text that is inserted. */
const skb_attribute_t* skb_editor_get_active_attributes(const skb_editor_t* editor);

/**
 * Begins undo transaction. All changes done within an transaction will be undo or redo as one change.
 * @param editor editor to update.
 * @return transaction id.
 */
int32_t skb_editor_undo_transaction_begin(skb_editor_t* editor);

/**
 * Ends undo transaction. All changes done within an transaction will be undo or redo as one change.
 * @param editor editor to update.
 * @param transaction_id transaction id from matching skb_editor_undo_transaction_begin().
 */
void skb_editor_undo_transaction_end(skb_editor_t* editor, int32_t transaction_id);

/** @return True, if the last change can be undone. */
bool skb_editor_can_undo(skb_editor_t* editor);

/**
 * Undo the last change.
 * @param editor editor to update.
 * @param temp_alloc temp allocator used to relayout the text.
 */
void skb_editor_undo(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc);

/** @return True, if the last undone change can be redone. */
bool skb_editor_can_redo(skb_editor_t* editor);

/**
 * Redo the last undone change.
 * @param editor editor to update.
 * @param temp_alloc temp allocator used to relayout the text.
 */
void skb_editor_redo(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc);

/**
 * Sets temporary IME composition text as utf-32. The text will be laid out at the current cursor location.
 * The function can be called multiple times during while the user composes the input.
 * Use skb_editor_commit_composition_utf32() to commit or skb_editor_clear_composition() to clear the composition text.
 * @param editor editor to update.
 * @param temp_alloc temp allocator used for updating the editor text.
 * @param utf32 pointer to utf-32 string to set.
 * @param utf32_len length of the string, or -1 if nul terminated.
 * @param caret_position caret position whitin the text. Zero is in front of the first character, and utf32_len is after the last character.
 */
void skb_editor_set_composition_utf32(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len, int32_t caret_position);

/**
 * Commits the specified string and clears composition text.
 * @param editor editor to update.
 * @param temp_alloc temp allocator used for updating the editor text.
 * @param utf32 pointer to utf-32 string to commit, if NULL previous text set with skb_editor_set_composition_utf32 will be used.
 * @param utf32_len length of the string, or -1 if nul terminated.
 */
void skb_editor_commit_composition_utf32(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc, const uint32_t* utf32, int32_t utf32_len);

/**
 * Clears composition text.
 * @param editor editor to update.
 * @param temp_alloc temp allocator used for updating the editor text.
 */
void skb_editor_clear_composition(skb_editor_t* editor, skb_temp_alloc_t* temp_alloc);



/** @} */

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // SKB_EDITOR_H
