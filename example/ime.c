// SPDX-FileCopyrightText: 2025 Mikko Mononen
// SPDX-License-Identifier: MIT

#include "ime.h"
#include "skb_common.h"
#include <stddef.h>
#include <stdio.h>

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <commctrl.h>
#include <imm.h>

typedef struct ime_context_t {
	HWND hwnd;

	// Preedit text
	uint32_t* preedit_text;
	int32_t preedit_text_cap;
	int32_t preedit_text_count;

	// Position of the caret whitin the text.
	int32_t caret_position;

	// Text input rectangle.
	skb_rect2i_t input_rect;

	// True if composition is happening.
	bool is_composing;

	// Handler to call on IME events.
	ime_event_handler_func_t* handler;
	void* handler_context;

} ime_context_t;

static ime_context_t g_context = {0};


void ime_set_handler(ime_event_handler_func_t* handler, void* context)
{
	g_context.handler = handler;
	g_context.handler_context = context;
}

static const WCHAR* ime__next_utf32(const WCHAR* str, const WCHAR* end, uint32_t* cp)
{
	uint32_t c = *str++;

	if (!(c >= 0xD800u && c <= 0xDFFFu)) {
		*cp = c;
		return str;
	}

	if (c <= 0xDBFFu && str != end) {
		// High-surrogate in c
		uint32_t l = *str;
		if (l >= 0xDC00u && l <= 0xDFFFu) {
			// Low-surrogate in l
			*cp = (c << 10) + l - ((0xD800u << 10) - 0x10000u + 0xDC00u);
			str++;
			return str;
		}
	}

	// Lonely / out-of-order surrogate.
	*cp = 0;
	return str;

}

static int32_t wchar_to_utf32(const WCHAR* str, int32_t str_count,uint32_t* utf32, int32_t utf32_cap)
{
	const WCHAR* end = str + str_count;
	int32_t count = 0;
	while (str != end) {
		uint32_t cp = 0;
		str = ime__next_utf32(str, end, &cp);
		if (utf32 && count < utf32_cap)
			utf32[count] = cp;
		count++;
	}

	return count;
}

static int32_t wchar_position_to_utf32(const WCHAR* str, int32_t str_count, int32_t position)
{
	const WCHAR* end = str + str_count;
	const WCHAR* pos = str + position;
	int32_t count = 0;
	while (str != end) {
		if (str == pos)
			return count;
		uint32_t cp = 0;
		str = ime__next_utf32(str, end, &cp);
		count++;
	}

	return count;
}

static bool ime__get_string(HIMC himc, int32_t type, LPARAM lparam, int32_t* caret_position)
{
	if (!(lparam & type))
		return false;
	int32_t string_size = ImmGetCompositionStringW(himc, type, NULL, 0);
	if (string_size <= 0)
		return false;

	int32_t string_count = string_size / (int32_t)sizeof(WCHAR);

	WCHAR* string = malloc(string_size);
	ImmGetCompositionStringW(himc, type, string, string_size);

	// Convert to utf-32
	int32_t text_required = wchar_to_utf32(string, string_count, NULL, 0);
	SKB_ARRAY_RESERVE(g_context.preedit_text, text_required + 1);
	g_context.preedit_text_count = wchar_to_utf32(string, string_count, g_context.preedit_text, text_required);
	g_context.preedit_text[text_required] = 0;

	if (caret_position) {
		int32_t caret = ImmGetCompositionStringW(himc, GCS_CURSORPOS, NULL, 0);
		*caret_position = wchar_position_to_utf32(string, string_count, caret);
	}

	free(string);

	return true;
}

static void ime__on_composition(UINT umsg, WPARAM wparam, LPARAM lparam)
{
	HIMC himc = ImmGetContext(g_context.hwnd);
	if (!himc)
		return;

	// Get result
	if (ime__get_string(himc, GCS_RESULTSTR, lparam, NULL)) {
		// Call commit
		if (g_context.handler)
			g_context.handler(IME_EVENT_COMMIT, g_context.preedit_text, g_context.preedit_text_count, g_context.caret_position, g_context.handler_context);

		// Reset
		g_context.is_composing = false;
	}

	// Get composition
	int32_t caret_position = 0;
	if (ime__get_string(himc, GCS_COMPSTR, lparam, &caret_position)) {
		if (!(lparam & CS_NOMOVECARET) && (lparam & GCS_CURSORPOS))
			g_context.caret_position = caret_position;
		else
			g_context.caret_position = 0;

		g_context.is_composing = true;

		// Call set composition
		if (g_context.handler)
			g_context.handler(IME_EVENT_COMPOSITION, g_context.preedit_text, g_context.preedit_text_count, g_context.caret_position, g_context.handler_context);
	} else {
		// Call cancel
		if (g_context.handler)
			g_context.handler(IME_EVENT_CANCEL, NULL, 0, 0, g_context.handler_context);
		g_context.is_composing = false;
	}

	ImmReleaseContext(g_context.hwnd, himc);
}

void ime_cancel(void)
{
	if (!IsWindow(g_context.hwnd))
		return;
	HIMC himc = ImmGetContext(g_context.hwnd);
	if (himc) {
		ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
		ImmReleaseContext(g_context.hwnd, himc);
		g_context.is_composing = false;
	}
}

void ime_set_input_rect(skb_rect2i_t rect)
{
	if (!IsWindow(g_context.hwnd))
		return;

	g_context.input_rect = rect;

	HIMC himc = ImmGetContext(g_context.hwnd);
	if (himc) {
		CANDIDATEFORM excludeRect = {
			0,
			CFS_EXCLUDE,
			{rect.x, rect.y},
			{rect.x, rect.y, rect.x + rect.width, rect.y+rect.height}
		};
		ImmSetCandidateWindow(himc, &excludeRect);
		ImmReleaseContext(g_context.hwnd, himc);
	}
}

static LRESULT sub_class_proc(HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam, UINT_PTR uidsubclass, DWORD_PTR dwrefdata)
{
	switch (umsg) {
	case WM_DESTROY:
		RemoveWindowSubclass(hwnd, sub_class_proc, 1);
		break;
	case WM_IME_COMPOSITION:
		ime__on_composition(umsg, wparam, lparam);
		return 0;
	case WM_IME_ENDCOMPOSITION:
		g_context.is_composing = false;
		return 0;
	}

	return DefSubclassProc(hwnd, umsg, wparam, lparam);
}

bool ime_init(GLFWwindow* window)
{
	HWND hwnd = glfwGetWin32Window(window);
	if (!IsWindow(hwnd))
		return false;

	// Subclass the window to be able to handle IMM messages.
	SetWindowSubclass(hwnd, sub_class_proc, 1, 0);

	g_context.hwnd = hwnd;

	return true;
}

void ime_terminate(void)
{
	skb_free(g_context.preedit_text);
	memset(&g_context, 0, sizeof(g_context));
}

#elif defined(__linux__)

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <gtk/gtk.h>

#if GTK_MAJOR_VERSION == 3 && defined(GDK_WINDOWING_X11)
#  include <gdk/gdkx.h>
#  include <X11/Xlib.h>
#endif

typedef struct ime_context_t {
	GtkIMContext      *im;
	uint32_t          *preedit_text;
	int32_t            preedit_text_cap;
	int32_t            preedit_text_count;
	int32_t            caret_position;
	skb_rect2i_t       input_rect;
	bool               is_composing;
	ime_event_handler_func_t *handler;
	void              *handler_context;
} ime_context_t;

static ime_context_t g_context = {0};

// Helper: copy UTF-8 string into UTF-32 buffer
static void ime__copy_utf8(const char* utf8)
{
	int32_t len8 = (int32_t)strlen(utf8);
	int32_t need32 = skb_utf8_to_utf32_count(utf8, len8);
	SKB_ARRAY_RESERVE(g_context.preedit_text, need32+1);
	g_context.preedit_text_count = skb_utf8_to_utf32(utf8, len8, g_context.preedit_text, need32);
	g_context.preedit_text[need32] = 0;
}

// Signal: commit finalized text
static void on_commit(GtkIMContext *ctx, const char *str, gpointer user)
{
	(void)ctx; (void)user;
	ime__copy_utf8(str);
	g_context.is_composing = false;
	if (g_context.handler)
		g_context.handler(IME_EVENT_COMMIT,
						  g_context.preedit_text,
						  g_context.preedit_text_count,
						  g_context.caret_position,
						  g_context.handler_context);
}

// Signal: preedit (composition) changed
static void on_preedit_changed(GtkIMContext *ctx, gpointer user)
{
	(void)ctx; (void)user;
	char *pre = NULL;
	int cursor8 = 0;
	gtk_im_context_get_preedit_string(ctx, &pre, NULL, &cursor8);

	ime__copy_utf8(pre);
	// convert cursor offset UTF-8->UTF-32
	g_context.caret_position = skb_utf8_to_utf32_count(pre, cursor8);
	g_context.is_composing = true;

	if (g_context.handler)
		g_context.handler(IME_EVENT_COMPOSITION,
						  g_context.preedit_text,
						  g_context.preedit_text_count,
						  g_context.caret_position,
						  g_context.handler_context);
	g_free(pre);
}

// Signals: start & end composition
static void on_preedit_start(GtkIMContext *ctx, gpointer user)  { (void)ctx; (void)user; g_context.is_composing = true; }
static void on_preedit_end  (GtkIMContext *ctx, gpointer user)
{
	(void)ctx; (void)user;
	g_context.is_composing = false;
	if (g_context.handler)
		g_context.handler(IME_EVENT_CANCEL, NULL, 0, 0, g_context.handler_context);
}

bool ime_init(GLFWwindow* window)
{
	// Initialise GTK once, if the host app hasn't done so
	static bool gtk_ok = false;
	static bool gtk_checked = false;
	if (!gtk_checked) {
		gtk_ok = gtk_init_check(NULL, NULL);   // returns FALSE if no DISPLAY
		gtk_checked = true;
	}
	if (!gtk_ok)
		return false;

	g_context.im = gtk_im_multicontext_new();
	if (!g_context.im)
		return false;

  // On X11 we need to bind the IM context to the GLFW window's GdkWindow
#if defined(GDK_WINDOWING_X11)
	Window xid = glfwGetX11Window(window);
	GdkDisplay* disp = gdk_display_get_default();
	GdkWindow* gw = gdk_x11_window_foreign_new_for_display(disp, xid);
	gtk_im_context_set_client_window(g_context.im, gw);

	/* --- debug: print --- */
	g_print("Binding IM context to GdkWindow %p for XID 0x%lx\n", (void*)gw, xid);
	gint gw_x, gw_y, gw_width, gw_height;
	gdk_window_get_geometry(gw,
							&gw_x, &gw_y,
							&gw_width, &gw_height);
	g_print("GdkWindow %p (XID 0x%lx) geometry: x=%d, y=%d, w=%d, h=%d\n",
			(void*)gw, xid,
			gw_x, gw_y,
			gw_width, gw_height);
#endif

	g_signal_connect(g_context.im, "commit", G_CALLBACK(on_commit), NULL);
	g_signal_connect(g_context.im, "preedit-changed", G_CALLBACK(on_preedit_changed), NULL);
	g_signal_connect(g_context.im, "preedit-start", G_CALLBACK(on_preedit_start), NULL);
	g_signal_connect(g_context.im, "preedit-end", G_CALLBACK(on_preedit_end), NULL);

	return true;
}

void ime_set_handler(ime_event_handler_func_t* handler, void* context)
{
	g_context.handler = handler;
	g_context.handler_context = context;
}

void ime_set_input_rect(skb_rect2i_t rect)
{
	printf("ime_set_input_rect %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
	g_context.input_rect = rect;
	if (g_context.im) {
		GdkRectangle r = { rect.x, rect.y, rect.width, rect.height };
		gtk_im_context_set_cursor_location(g_context.im, &r);
	}
}

void ime_cancel(void)
{
	if (g_context.im)
		gtk_im_context_reset(g_context.im);
	g_context.is_composing = false;
}

void ime_terminate(void)
{
	if (g_context.im)
		g_object_unref(g_context.im);
	skb_free(g_context.preedit_text);
	memset(&g_context, 0, sizeof(g_context));
}

#else

bool ime_init(GLFWwindow* window)
{
	return true;
}

void ime_set_handler(ime_event_handler_func_t* handler, void* context)
{
}

void ime_set_input_rect(skb_rect2i_t rect)
{
}

void ime_cancel(void)
{
}

void ime_terminate(void)
{
}

#endif
