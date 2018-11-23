#include "Window.h"
#include "Platform.h"
#include "Input.h"
#include "Event.h"
#include "ErrorHandler.h"
#include "Funcs.h"

/*########################################################################################################################*
*------------------------------------------------------Win32 window-------------------------------------------------------*
*#########################################################################################################################*/
#ifdef CC_BUILD_WIN
#define WIN32_LEAN_AND_MEAN
#define NOSERVICE
#define NOMCX
#define NOIME
#define _WIN32_IE    0x0400
#define WINVER       0x0500
#define _WIN32_WINNT 0x0500
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>

#define win_Style WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN
#define win_ClassName TEXT("ClassiCube_Window")
#define Rect_Width(rect)  (rect.right  - rect.left)
#define Rect_Height(rect) (rect.bottom - rect.top)

static HINSTANCE win_instance;
static HWND win_handle;
static HDC win_DC;
static int win_state;
static bool invisible_since_creation; /* Set by WindowsMessage.CREATE and consumed by Visible = true (calls BringWindowToFront) */
static int suppress_resize; /* Used in WindowBorder and WindowState in order to avoid rapid, consecutive resize events */
static Rect2D prev_bounds; /* Used to restore previous size when leaving fullscreen mode */


/*########################################################################################################################*
*-----------------------------------------------------Private details-----------------------------------------------------*
*#########################################################################################################################*/
static Key Window_MapKey(WPARAM key) {
	if (key >= VK_F1 && key <= VK_F24) { return Key_F1 + (key - VK_F1); }
	if (key >= '0' && key <= '9') { return Key_0 + (key - '0'); }
	if (key >= 'A' && key <= 'Z') { return Key_A + (key - 'A'); }

	if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) { 
		return Key_Keypad0 + (key - VK_NUMPAD0); 
	}

	switch (key) {
	case VK_ESCAPE: return Key_Escape;
	case VK_TAB: return Key_Tab;
	case VK_CAPITAL: return Key_CapsLock;
	case VK_LCONTROL: return Key_ControlLeft;
	case VK_LSHIFT: return Key_ShiftLeft;
	case VK_LWIN: return Key_WinLeft;
	case VK_LMENU: return Key_AltLeft;
	case VK_SPACE: return Key_Space;
	case VK_RMENU: return Key_AltRight;
	case VK_RWIN: return Key_WinRight;
	case VK_APPS: return Key_Menu;
	case VK_RCONTROL: return Key_ControlRight;
	case VK_RSHIFT: return Key_ShiftRight;
	case VK_RETURN: return Key_Enter;
	case VK_BACK: return Key_BackSpace;

	case VK_OEM_1: return Key_Semicolon;      /* Varies by keyboard: return ;: on Win2K/US */
	case VK_OEM_2: return Key_Slash;          /* Varies by keyboard: return /? on Win2K/US */
	case VK_OEM_3: return Key_Tilde;          /* Varies by keyboard: return `~ on Win2K/US */
	case VK_OEM_4: return Key_BracketLeft;    /* Varies by keyboard: return [{ on Win2K/US */
	case VK_OEM_5: return Key_BackSlash;      /* Varies by keyboard: return \| on Win2K/US */
	case VK_OEM_6: return Key_BracketRight;   /* Varies by keyboard: return ]} on Win2K/US */
	case VK_OEM_7: return Key_Quote;          /* Varies by keyboard: return '" on Win2K/US */
	case VK_OEM_PLUS: return Key_Plus;        /* Invariant: +							   */
	case VK_OEM_COMMA: return Key_Comma;      /* Invariant: : return					   */
	case VK_OEM_MINUS: return Key_Minus;      /* Invariant: -							   */
	case VK_OEM_PERIOD: return Key_Period;    /* Invariant: .							   */

	case VK_HOME: return Key_Home;
	case VK_END: return Key_End;
	case VK_DELETE: return Key_Delete;
	case VK_PRIOR: return Key_PageUp;
	case VK_NEXT: return Key_PageDown;
	case VK_PRINT: return Key_PrintScreen;
	case VK_PAUSE: return Key_Pause;
	case VK_NUMLOCK: return Key_NumLock;

	case VK_SCROLL: return Key_ScrollLock;
	case VK_SNAPSHOT: return Key_PrintScreen;
	case VK_INSERT: return Key_Insert;

	case VK_DECIMAL: return Key_KeypadDecimal;
	case VK_ADD: return Key_KeypadAdd;
	case VK_SUBTRACT: return Key_KeypadSubtract;
	case VK_DIVIDE: return Key_KeypadDivide;
	case VK_MULTIPLY: return Key_KeypadMultiply;

	case VK_UP: return Key_Up;
	case VK_DOWN: return Key_Down;
	case VK_LEFT: return Key_Left;
	case VK_RIGHT: return Key_Right;
	}
	return Key_None;
}

static void Window_Destroy(void) {
	if (!Window_Exists) return;
	DestroyWindow(win_handle);
	Window_Exists = false;
}

static void Window_ResetWindowState(void) {
	suppress_resize++;
	Window_SetWindowState(WINDOW_STATE_NORMAL);
	Window_ProcessEvents();
	suppress_resize--;
}

static bool win_hiddenBorder;
static void Window_DoSetHiddenBorder(bool value) {
	bool wasVisible;
	RECT rect;
	if (win_hiddenBorder == value) return;

	/* We wish to avoid making an invisible window visible just to change the border.
	However, it's a good idea to make a visible window invisible temporarily, to
	avoid garbage caused by the border change. */
	wasVisible = Window_GetVisible();

	/* To ensure maximized/minimized windows work correctly, reset state to normal,
	change the border, then go back to maximized/minimized. */
	int state = win_state;
	Window_ResetWindowState();
	DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	style |= (value ? WS_POPUP : WS_OVERLAPPEDWINDOW);

	/* Make sure client size doesn't change when changing the border style.*/
	rect.left = Window_Bounds.X; rect.top = Window_Bounds.Y;
	rect.right  = rect.left + Window_Bounds.Width;
	rect.bottom = rect.top  + Window_Bounds.Height;
	AdjustWindowRect(&rect, style, false);

	/* This avoids leaving garbage on the background window. */
	if (wasVisible) Window_SetVisible(false);

	SetWindowLong(win_handle, GWL_STYLE, style);
	SetWindowPos(win_handle, NULL, 0, 0, Rect_Width(rect), Rect_Height(rect),
		SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

	/* Force window to redraw update its borders, but only if it's
	already visible (invisible windows will change borders when
	they become visible, so no need to make them visiable prematurely).*/
	if (wasVisible) Window_SetVisible(true);

	Window_SetWindowState(state);
}

static void Window_SetHiddenBorder(bool hidden) {
	suppress_resize++;
	Window_DoSetHiddenBorder(hidden);
	Window_ProcessEvents();
	suppress_resize--;
}

static void Window_UpdateClientSize(HWND handle) {
	RECT rect;
	GetClientRect(handle, &rect);
	Window_ClientSize.Width  = Rect_Width(rect);
	Window_ClientSize.Height = Rect_Height(rect);
}

static LRESULT CALLBACK Window_Procedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
	char keyChar;
	bool wasFocused;
	float wheelDelta;

	switch (message) {
	case WM_ACTIVATE:
		wasFocused     = Window_Focused;
		Window_Focused = LOWORD(wParam) != 0;

		if (Window_Focused != wasFocused) {
			Event_RaiseVoid(&WindowEvents_FocusChanged);
		}
		break;

	case WM_ENTERMENULOOP:
	case WM_ENTERSIZEMOVE:
	case WM_EXITMENULOOP:
	case WM_EXITSIZEMOVE:
		break;

	case WM_ERASEBKGND:
		Event_RaiseVoid(&WindowEvents_Redraw);
		return 1;

	case WM_WINDOWPOSCHANGED:
	{
		WINDOWPOS* pos = (WINDOWPOS*)lParam;
		if (pos->hwnd != win_handle) break;

		if (pos->x != Window_Bounds.X || pos->y != Window_Bounds.Y) {
			Window_Bounds.X = pos->x; Window_Bounds.Y = pos->y;
			Event_RaiseVoid(&WindowEvents_Moved);
		}

		if (pos->cx != Window_Bounds.Width || pos->cy != Window_Bounds.Height) {
			Window_Bounds.Width = pos->cx; Window_Bounds.Height = pos->cy;
			Window_UpdateClientSize(handle);

			SetWindowPos(win_handle, NULL,
				Window_Bounds.X, Window_Bounds.Y, Window_Bounds.Width, Window_Bounds.Height,
				SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);

			if (suppress_resize <= 0) {
				Event_RaiseVoid(&WindowEvents_Resized);
			}
		}
	} break;

	case WM_STYLECHANGED:
		if (wParam == GWL_STYLE) {
			DWORD style = ((STYLESTRUCT*)lParam)->styleNew;
			if (style & WS_POPUP) {
				win_hiddenBorder = true;
			} else if (style & WS_THICKFRAME) {
				win_hiddenBorder = false;
			}
		}
		break;

	case WM_SIZE:
	{
		int new_state = win_state;
		switch (wParam) {
		case SIZE_RESTORED:  new_state = WINDOW_STATE_NORMAL; break;
		case SIZE_MINIMIZED: new_state = WINDOW_STATE_MINIMISED; break;
		case SIZE_MAXIMIZED: new_state = win_hiddenBorder ? WINDOW_STATE_FULLSCREEN : WINDOW_STATE_MAXIMISED; break;
		}

		if (new_state != win_state) {
			win_state = new_state;
			Event_RaiseVoid(&WindowEvents_StateChanged);
		}
	} break;


	case WM_CHAR:
		if (Convert_TryUnicodeToCP437((Codepoint)wParam, &keyChar)) {
			Event_RaiseInt(&KeyEvents_Press, keyChar);
		}
		break;

	case WM_MOUSEMOVE:
		/* set before position change, in case mouse buttons changed when outside window */
		Mouse_SetPressed(MouseButton_Left,   (wParam & 0x01) != 0);
		Mouse_SetPressed(MouseButton_Right,  (wParam & 0x02) != 0);
		Mouse_SetPressed(MouseButton_Middle, (wParam & 0x10) != 0);
		/* TODO: do we need to set XBUTTON1/XBUTTON2 here */
		Mouse_SetPosition(LOWORD(lParam), HIWORD(lParam));
		break;

	case WM_MOUSEWHEEL:
		wheelDelta = ((short)HIWORD(wParam)) / (float)WHEEL_DELTA;
		Mouse_SetWheel(Mouse_Wheel + wheelDelta);
		return 0;

	case WM_LBUTTONDOWN:
		Mouse_SetPressed(MouseButton_Left, true);
		break;
	case WM_MBUTTONDOWN:
		Mouse_SetPressed(MouseButton_Middle, true);
		break;
	case WM_RBUTTONDOWN:
		Mouse_SetPressed(MouseButton_Right, true);
		break;
	case WM_XBUTTONDOWN:
		Key_SetPressed(HIWORD(wParam) == 1 ? Key_XButton1 : Key_XButton2, true);
		break;
	case WM_LBUTTONUP:
		Mouse_SetPressed(MouseButton_Left, false);
		break;
	case WM_MBUTTONUP:
		Mouse_SetPressed(MouseButton_Middle, false);
		break;
	case WM_RBUTTONUP:
		Mouse_SetPressed(MouseButton_Right, false);
		break;
	case WM_XBUTTONUP:
		Key_SetPressed(HIWORD(wParam) == 1 ? Key_XButton1 : Key_XButton2, false);
		break;

	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	{
		bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
		/* Shift/Control/Alt behave strangely when e.g. ShiftRight is held down and ShiftLeft is pressed
		and released. It looks like neither key is released in this case, or that the wrong key is
		released in the case of Control and Alt.
		To combat this, we are going to release both keys when either is released. Hacky, but should work.
		Win95 does not distinguish left/right key constants (GetAsyncKeyState returns 0).
		In this case, both keys will be reported as pressed.	*/
		bool extended = (lParam & (1UL << 24)) != 0;

		bool lShiftDown, rShiftDown;
		Key mappedKey;
		switch (wParam)
		{
		case VK_SHIFT:
			/* The behavior of this key is very strange. Unlike Control and Alt, there is no extended bit
			to distinguish between left and right keys. Moreover, pressing both keys and releasing one
			may result in both keys being held down (but not always).*/
			lShiftDown = ((USHORT)GetKeyState(VK_LSHIFT)) >> 15;
			rShiftDown = ((USHORT)GetKeyState(VK_RSHIFT)) >> 15;

			if (!pressed || lShiftDown != rShiftDown) {
				Key_SetPressed(Key_ShiftLeft, lShiftDown);
				Key_SetPressed(Key_ShiftRight, rShiftDown);
			}
			return 0;

		case VK_CONTROL:
			if (extended) {
				Key_SetPressed(Key_ControlRight, pressed);
			} else {
				Key_SetPressed(Key_ControlLeft, pressed);
			}
			return 0;

		case VK_MENU:
			if (extended) {
				Key_SetPressed(Key_AltRight, pressed);
			} else {
				Key_SetPressed(Key_AltLeft, pressed);
			}
			return 0;

		case VK_RETURN:
			if (extended) {
				Key_SetPressed(Key_KeypadEnter, pressed);
			} else {
				Key_SetPressed(Key_Enter, pressed);
			}
			return 0;

		default:
			mappedKey = Window_MapKey(wParam);
			if (mappedKey != Key_None) {
				Key_SetPressed(mappedKey, pressed);
			}
			return 0;
		}
	} break;

	case WM_SYSCHAR:
		return 0;

	case WM_KILLFOCUS:
		Key_Clear();
		break;


	case WM_CREATE:
	{
		CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
		if (!cs->hwndParent) {
			Window_Bounds.X = cs->x; Window_Bounds.Width  = cs->cx;		
			Window_Bounds.Y = cs->y; Window_Bounds.Height = cs->cy;
			Window_UpdateClientSize(handle);
			invisible_since_creation = true;
		}
	} break;

	case WM_CLOSE:
		Event_RaiseVoid(&WindowEvents_Closing);
		Window_Destroy();
		break;

	case WM_DESTROY:
		Window_Exists = false;
		UnregisterClass(win_ClassName, win_instance);
		if (win_DC) ReleaseDC(win_handle, win_DC);
		Event_RaiseVoid(&WindowEvents_Closed);
		break;
	}
	return DefWindowProc(handle, message, wParam, lParam);
}


/*########################################################################################################################*
*--------------------------------------------------Public implementation--------------------------------------------------*
*#########################################################################################################################*/
void Window_Create(int x, int y, int width, int height, struct GraphicsMode* mode) {
	win_instance = GetModuleHandle(NULL);
	/* TODO: UngroupFromTaskbar(); */

	/* Find out the final window rectangle, after the WM has added its chrome (titlebar, sidebars etc). */
	RECT rect = { x, y, x + width, y + height };
	AdjustWindowRect(&rect, win_Style, false);

	WNDCLASSEX wc = { 0 };
	wc.cbSize    = sizeof(WNDCLASSEX);
	wc.style     = CS_OWNDC;
	wc.hInstance = win_instance;
	wc.lpfnWndProc   = Window_Procedure;
	wc.lpszClassName = win_ClassName;
	/* TODO: Set window icons here */
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	ATOM atom = RegisterClassEx(&wc);
	if (!atom) ErrorHandler_Fail2(GetLastError(), "Failed to register window class");

	win_handle = CreateWindowEx(0, atom, NULL, win_Style,
		rect.left, rect.top, Rect_Width(rect), Rect_Height(rect),
		NULL, NULL, win_instance, NULL);
	if (!win_handle) ErrorHandler_Fail2(GetLastError(), "Failed to create window");

	win_DC = GetDC(win_handle);
	if (!win_DC) ErrorHandler_Fail2(GetLastError(), "Failed to get device context");
	Window_Exists = true;
}

void Window_SetTitle(const String* title) {
	TCHAR str[300]; 
	Platform_ConvertString(str, title);
	SetWindowText(win_handle, str);
}

void Window_GetClipboardText(String* value) {
	/* retry up to 50 times*/
	int i;
	value->length = 0;

	for (i = 0; i < 50; i++) {
		if (!OpenClipboard(win_handle)) {
			Thread_Sleep(10);
			continue;
		}

		bool isUnicode = true;
		HANDLE hGlobal = GetClipboardData(CF_UNICODETEXT);
		if (!hGlobal) {
			hGlobal = GetClipboardData(CF_TEXT);
			isUnicode = false;
		}
		if (!hGlobal) { CloseClipboard(); return; }
		LPVOID src = GlobalLock(hGlobal);

		char c;
		if (isUnicode) {
			Codepoint* text = (Codepoint*)src;
			for (; *text; text++) {
				if (Convert_TryUnicodeToCP437(*text, &c)) String_Append(value, c);
			}
		} else {
			char* text = (char*)src;
			for (; *text; text++) {
				if (Convert_TryUnicodeToCP437(*text, &c)) String_Append(value, c);
			}
		}

		GlobalUnlock(hGlobal);
		CloseClipboard();
		return;
	}
}

void Window_SetClipboardText(const String* value) {
	/* retry up to 10 times */
	int i;
	for (i = 0; i < 10; i++) {
		if (!OpenClipboard(win_handle)) {
			Thread_Sleep(100);
			continue;
		}

		HANDLE hGlobal = GlobalAlloc(GMEM_MOVEABLE, (value->length + 1) * 2);
		if (!hGlobal) { CloseClipboard(); return; }

		Codepoint* text = GlobalLock(hGlobal);
		for (i = 0; i < value->length; i++, text++) {
			*text = Convert_CP437ToUnicode(value->buffer[i]);
		}
		*text = '\0';

		GlobalUnlock(hGlobal);
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hGlobal);
		CloseClipboard();
		return;
	}
}


void Window_SetBounds(Rect2D rect) {
	/* Note: the bounds variable is updated when the resize/move message arrives.*/
	SetWindowPos(win_handle, NULL, rect.X, rect.Y, rect.Width, rect.Height, 0);
}

void Window_SetLocation(int x, int y) {
	SetWindowPos(win_handle, NULL, x, y, 0, 0, SWP_NOSIZE);
}

void Window_SetSize(int width, int height) {
	SetWindowPos(win_handle, NULL, 0, 0, width, height, SWP_NOMOVE);
}

void Window_SetClientSize(int width, int height) {
	DWORD style = GetWindowLong(win_handle, GWL_STYLE);
	RECT rect = { 0, 0, width, height };

	AdjustWindowRect(&rect, style, false);
	Window_SetSize(Rect_Width(rect), Rect_Height(rect));
}

void* Window_GetWindowHandle(void) { return win_handle; }

bool Window_GetVisible(void) { return IsWindowVisible(win_handle); }
void Window_SetVisible(bool visible) {
	if (visible) {
		ShowWindow(win_handle, SW_SHOW);
		if (invisible_since_creation) {
			BringWindowToTop(win_handle);
			SetForegroundWindow(win_handle);
		}
	} else {
		ShowWindow(win_handle, SW_HIDE);
	}
}


void Window_Close(void) {
	PostMessage(win_handle, WM_CLOSE, 0, 0);
}

int Window_GetWindowState(void) { return win_state; }
void Window_SetWindowState(int state) {
	if (win_state == state) return;

	DWORD command = 0;
	bool exiting_fullscreen = false;

	switch (state) {
	case WINDOW_STATE_NORMAL:
		command = SW_RESTORE;

		/* If we are leaving fullscreen mode we need to restore the border. */
		if (win_state == WINDOW_STATE_FULLSCREEN)
			exiting_fullscreen = true;
		break;

	case WINDOW_STATE_MAXIMISED:
		/* Reset state to avoid strange interactions with fullscreen/minimized windows. */
		Window_ResetWindowState();
		command = SW_MAXIMIZE;
		break;

	case WINDOW_STATE_MINIMISED:
		command = SW_MINIMIZE;
		break;

	case WINDOW_STATE_FULLSCREEN:
		/* We achieve fullscreen by hiding the window border and sending the MAXIMIZE command.
		We cannot use the WindowState.Maximized directly, as that will not send the MAXIMIZE
		command for windows with hidden borders. */

		/* Reset state to avoid strange side-effects from maximized/minimized windows. */
		Window_ResetWindowState();
		prev_bounds = Window_Bounds;
		Window_SetHiddenBorder(true);

		command = SW_MAXIMIZE;
		SetForegroundWindow(win_handle);
		break;
	}

	if (command != 0) ShowWindow(win_handle, command);

	/* Restore previous window border or apply pending border change when leaving fullscreen mode. */
	if (exiting_fullscreen) Window_SetHiddenBorder(false);

	/* Restore previous window size/location if necessary */
	if (command == SW_RESTORE && (prev_bounds.Width || prev_bounds.Height)) {
		Window_SetBounds(prev_bounds);
		prev_bounds.Width = 0; prev_bounds.Height = 0;
	}
}

Point2D Window_PointToClient(int x, int y) {
	Point2D point = { x, y };
	if (!ScreenToClient(win_handle, &point)) {
		ErrorHandler_Fail2(GetLastError(), "Converting point from client to screen coordinates");
	}
	return point;
}

Point2D Window_PointToScreen(int x, int y) {
	Point2D point = { x, y };
	if (!ClientToScreen(win_handle, &point)) {
		ErrorHandler_Fail2(GetLastError(), "Converting point from screen to client coordinates");
	}
	return point;
}

void Window_ProcessEvents(void) {
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, 1)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	HWND foreground = GetForegroundWindow();
	if (foreground) {
		Window_Focused = foreground == win_handle;
	}
}

Point2D Window_GetScreenCursorPos(void) {
	POINT point; GetCursorPos(&point);
	Point2D p = { point.x, point.y }; return p;
}
void Window_SetScreenCursorPos(int x, int y) {
	SetCursorPos(x, y);
}

static bool win_cursorVisible = true;
bool Window_GetCursorVisible(void) { return win_cursorVisible; }
void Window_SetCursorVisible(bool visible) {
	win_cursorVisible = visible;
	ShowCursor(visible ? 1 : 0);
}


/*########################################################################################################################*
*-----------------------------------------------------OpenGL context------------------------------------------------------*
*#########################################################################################################################*/
#ifndef CC_BUILD_D3D9
void GLContext_SelectGraphicsMode(struct GraphicsMode* mode) {
	PIXELFORMATDESCRIPTOR pfd = { 0 };
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
	/* TODO: PFD_SUPPORT_COMPOSITION FLAG? CHECK IF IT WORKS ON XP */
	pfd.cColorBits = mode->R + mode->G + mode->B;

	pfd.iPixelType = mode->IsIndexed ? PFD_TYPE_COLORINDEX : PFD_TYPE_RGBA;
	pfd.cRedBits   = mode->R;
	pfd.cGreenBits = mode->G;
	pfd.cBlueBits  = mode->B;
	pfd.cAlphaBits = mode->A;

	pfd.cDepthBits   = mode->DepthBits;
	pfd.cStencilBits = mode->StencilBits;
	if (mode->DepthBits <= 0) pfd.dwFlags |= PFD_DEPTH_DONTCARE;
	if (mode->Buffers > 1)    pfd.dwFlags |= PFD_DOUBLEBUFFER;

	int modeIndex = ChoosePixelFormat(win_DC, &pfd);
	if (modeIndex == 0) { ErrorHandler_Fail("Requested graphics mode not available"); }

	Mem_Set(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;

	DescribePixelFormat(win_DC, modeIndex, pfd.nSize, &pfd);
	if (!SetPixelFormat(win_DC, modeIndex, &pfd)) {
		ErrorHandler_Fail2(GetLastError(), "SetPixelFormat failed");
	}
}

static HGLRC ctx_Handle;
static HDC ctx_DC;
typedef BOOL (WINAPI *FN_WGLSWAPINTERVAL)(int interval);
static FN_WGLSWAPINTERVAL wglSwapIntervalEXT;
static bool ctx_supports_vSync;

void GLContext_Init(struct GraphicsMode* mode) {
	GLContext_SelectGraphicsMode(mode);
	ctx_Handle = wglCreateContext(win_DC);
	if (!ctx_Handle) {
		ctx_Handle = wglCreateContext(win_DC);
	}
	if (!ctx_Handle) {
		ErrorHandler_Fail2(GetLastError(), "Failed to create OpenGL context");
	}

	if (!wglMakeCurrent(win_DC, ctx_Handle)) {
		ErrorHandler_Fail2(GetLastError(), "Failed to make OpenGL context current");
	}

	ctx_DC = wglGetCurrentDC();
	wglSwapIntervalEXT = (FN_WGLSWAPINTERVAL)GLContext_GetAddress("wglSwapIntervalEXT");
	ctx_supports_vSync = wglSwapIntervalEXT != NULL;
}

void GLContext_Update(void) { }
void GLContext_Free(void) {
	if (!wglDeleteContext(ctx_Handle)) {
		ErrorHandler_Fail2(GetLastError(), "Failed to destroy OpenGL context");
	}
	ctx_Handle = NULL;
}

void* GLContext_GetAddress(const char* function) {
	void* address = wglGetProcAddress(function);
	return GLContext_IsInvalidAddress(address) ? NULL : address;
}

void GLContext_SwapBuffers(void) {
	if (!SwapBuffers(ctx_DC)) {
		ErrorHandler_Fail2(GetLastError(), "Failed to swap buffers");
	}
}

void GLContext_SetVSync(bool enabled) {
	if (ctx_supports_vSync) wglSwapIntervalEXT(enabled);
}
#endif
#endif


/*########################################################################################################################*
*-------------------------------------------------------X11 window--------------------------------------------------------*
*#########################################################################################################################*/
#ifdef CC_BUILD_X11
#include <X11/Xlib.h>
#include <GL/glx.h>

#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD    1
#define _NET_WM_STATE_TOGGLE 2

static Display* win_display;
static int win_screen;
static Window win_rootWin;

static Window win_handle;
static XVisualInfo win_visual;
static int borderLeft, borderRight, borderTop, borderBottom;
static bool win_isExiting;
 
static Atom wm_destroy, net_wm_state;
static Atom net_wm_state_minimized;
static Atom net_wm_state_fullscreen;
static Atom net_wm_state_maximized_horizontal;
static Atom net_wm_state_maximized_vertical;
static Atom net_wm_icon, net_frame_extents;

static Atom xa_clipboard, xa_targets, xa_utf8_string, xa_data_sel;
static Atom xa_atom = 4, xa_cardinal = 6;
static long win_eventMask;


/*########################################################################################################################*
*-----------------------------------------------------Private details-----------------------------------------------------*
*#########################################################################################################################*/
static Key Window_MapKey(KeySym key) {
	if (key >= XK_F1 && key <= XK_F35) { return Key_F1 + (key - XK_F1); }
	if (key >= XK_0 && key <= XK_9) { return Key_0 + (key - XK_0); }
	if (key >= XK_A && key <= XK_Z) { return Key_A + (key - XK_A); }
	if (key >= XK_a && key <= XK_z) { return Key_A + (key - XK_a); }

	if (key >= XK_KP_0 && key <= XK_KP_9) {
		return Key_Keypad0 + (key - XK_KP_0);
	}

	switch (key) {
		case XK_Escape: return Key_Escape;
		case XK_Return: return Key_Enter;
		case XK_space: return Key_Space;
		case XK_BackSpace: return Key_BackSpace;

		case XK_Shift_L: return Key_ShiftLeft;
		case XK_Shift_R: return Key_ShiftRight;
		case XK_Alt_L: return Key_AltLeft;
		case XK_Alt_R: return Key_AltRight;
		case XK_Control_L: return Key_ControlLeft;
		case XK_Control_R: return Key_ControlRight;
		case XK_Super_L: return Key_WinLeft;
		case XK_Super_R: return Key_WinRight;
		case XK_Meta_L: return Key_WinLeft;
		case XK_Meta_R: return Key_WinRight;

		case XK_Menu: return Key_Menu;
		case XK_Tab: return Key_Tab;
		case XK_minus: return Key_Minus;
		case XK_plus: return Key_Plus;
		case XK_equal: return Key_Plus;

		case XK_Caps_Lock: return Key_CapsLock;
		case XK_Num_Lock: return Key_NumLock;

		case XK_Pause: return Key_Pause;
		case XK_Break: return Key_Pause;
		case XK_Scroll_Lock: return Key_ScrollLock;
		case XK_Insert: return Key_Insert;
		case XK_Print: return Key_PrintScreen;
		case XK_Sys_Req: return Key_PrintScreen;

		case XK_backslash: return Key_BackSlash;
		case XK_bar: return Key_BackSlash;
		case XK_braceleft: return Key_BracketLeft;
		case XK_bracketleft: return Key_BracketLeft;
		case XK_braceright: return Key_BracketRight;
		case XK_bracketright: return Key_BracketRight;
		case XK_colon: return Key_Semicolon;
		case XK_semicolon: return Key_Semicolon;
		case XK_quoteright: return Key_Quote;
		case XK_quotedbl: return Key_Quote;
		case XK_quoteleft: return Key_Tilde;
		case XK_asciitilde: return Key_Tilde;

		case XK_comma: return Key_Comma;
		case XK_less: return Key_Comma;
		case XK_period: return Key_Period;
		case XK_greater: return Key_Period;
		case XK_slash: return Key_Slash;
		case XK_question: return Key_Slash;

		case XK_Left: return Key_Left;
		case XK_Down: return Key_Down;
		case XK_Right: return Key_Right;
		case XK_Up: return Key_Up;

		case XK_Delete: return Key_Delete;
		case XK_Home: return Key_Home;
		case XK_End: return Key_End;
		case XK_Page_Up: return Key_PageUp;
		case XK_Page_Down: return Key_PageDown;

		case XK_KP_Add: return Key_KeypadAdd;
		case XK_KP_Subtract: return Key_KeypadSubtract;
		case XK_KP_Multiply: return Key_KeypadMultiply;
		case XK_KP_Divide: return Key_KeypadDivide;
		case XK_KP_Decimal: return Key_KeypadDecimal;
		case XK_KP_Insert: return Key_Keypad0;
		case XK_KP_End: return Key_Keypad1;
		case XK_KP_Down: return Key_Keypad2;
		case XK_KP_Page_Down: return Key_Keypad3;
		case XK_KP_Left: return Key_Keypad4;
		case XK_KP_Begin: return Key_Keypad5;
		case XK_KP_Right: return Key_Keypad6;
		case XK_KP_Home: return Key_Keypad7;
		case XK_KP_Up: return Key_Keypad8;
		case XK_KP_Page_Up: return Key_Keypad9;
		case XK_KP_Delete: return Key_KeypadDecimal;
		case XK_KP_Enter: return Key_KeypadEnter;
	}
	return Key_None;
}

static void Window_RegisterAtoms(void) {
	Display* display = win_display;
	wm_destroy = XInternAtom(display, "WM_DELETE_WINDOW", true);
	net_wm_state = XInternAtom(display, "_NET_WM_STATE", false);
	net_wm_state_minimized  = XInternAtom(display, "_NET_WM_STATE_MINIMIZED",  false);
	net_wm_state_fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", false);
	net_wm_state_maximized_horizontal = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", false);
	net_wm_state_maximized_vertical   = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", false);
	net_wm_icon = XInternAtom(display, "_NEW_WM_ICON", false);
	net_frame_extents = XInternAtom(display, "_NET_FRAME_EXTENTS", false);

	xa_clipboard   = XInternAtom(display, "CLIPBOARD",   false);
	xa_targets     = XInternAtom(display, "TARGETS",     false);
	xa_utf8_string = XInternAtom(display, "UTF8_STRING", false);
	xa_data_sel    = XInternAtom(display, "CC_SEL_DATA", false);
}

static void Window_RefreshBorders(void) {
	Atom prop_type;
	int prop_format;
	unsigned long items, after;
	long* borders = NULL;
	
	XGetWindowProperty(win_display, win_handle, net_frame_extents, 0, 16, false,
		xa_cardinal, &prop_type, &prop_format, &items, &after, &borders);

	if (!borders) return;
	if (items == 4) {
		borderLeft = borders[0]; borderRight = borders[1];
		borderTop = borders[2]; borderBottom = borders[3];
	}
	XFree(borders);
}

static void Window_RefreshBounds(XEvent* e) {
	Point2D loc;
	Size2D size;
	Window_RefreshBorders();
	
	loc.X = e->xconfigure.x - borderLeft;
	loc.Y = e->xconfigure.y - borderTop;

	if (loc.X != Window_Bounds.X || loc.Y != Window_Bounds.Y) {
		Window_Bounds.X = loc.X; Window_Bounds.Y = loc.Y;
		Event_RaiseVoid(&WindowEvents_Moved);
	}

	/* Note: width and height denote the internal (client) size.
	   To get the external (window) size, we need to add the border size. */	
	size.Width  = e->xconfigure.width  + borderLeft + borderRight;
	size.Height = e->xconfigure.height + borderTop  + borderBottom;

	if (size.Width != Window_Bounds.Width || size.Height != Window_Bounds.Height) {		 
		Window_ClientSize.Width  = e->xconfigure.width;  Window_Bounds.Width  = size.Width;
		Window_ClientSize.Height = e->xconfigure.height; Window_Bounds.Height = size.Height;
		Event_RaiseVoid(&WindowEvents_Resized);
	}
}


/*########################################################################################################################*
*--------------------------------------------------Public implementation--------------------------------------------------*
*#########################################################################################################################*/
static XVisualInfo GLContext_SelectVisual(struct GraphicsMode* mode);
void Window_Create(int x, int y, int width, int height, struct GraphicsMode* mode) {
	XSetWindowAttributes attributes = { 0 };
	XSizeHints hints = { 0 };
	uintptr_t addr;
	bool supported;

	win_display = DisplayDevice_Meta;
	win_screen  = DefaultScreen(win_display);
	win_rootWin = RootWindow(win_display, win_screen);

	/* Open a display connection to the X server, and obtain the screen and root window */
	addr = (uintptr_t)win_display;
	Platform_Log3("Display: %x, Screen %i, Root window: %h", &addr, &win_screen, &win_rootWin);
	Window_RegisterAtoms();

	win_eventMask = StructureNotifyMask /*| SubstructureNotifyMask*/ | ExposureMask |
		KeyReleaseMask  | KeyPressMask    | KeymapStateMask   | PointerMotionMask |
		FocusChangeMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask |
		LeaveWindowMask | PropertyChangeMask;
	win_visual = GLContext_SelectVisual(mode);

	Platform_LogConst("Opening render window... ");
	attributes.colormap   = XCreateColormap(win_display, win_rootWin, win_visual.visual, AllocNone);
	attributes.event_mask = win_eventMask;

	win_handle = XCreateWindow(win_display, win_rootWin, x, y, width, height,
		0, win_visual.depth /* CopyFromParent*/, InputOutput, win_visual.visual, 
		CWColormap | CWEventMask | CWBackPixel | CWBorderPixel, &attributes);
	if (!win_handle) ErrorHandler_Fail("XCreateWindow call failed");

	hints.base_width  = width;
	hints.base_height = height;
	hints.flags = PSize | PPosition;
	XSetWMNormalHints(win_display, win_handle, &hints);

	/* Register for window destroy notification */
	Atom atoms[1] = { wm_destroy };
	XSetWMProtocols(win_display, win_handle, atoms, 1);

	/* Set the initial window size to ensure X, Y, Width, Height and the rest
	   return the correct values inside the constructor and the Load event. */
	XEvent e = { 0 };
	e.xconfigure.x = x;
	e.xconfigure.y = y;
	e.xconfigure.width = width;
	e.xconfigure.height = height;
	Window_RefreshBounds(&e);

	/* Request that auto-repeat is only set on devices that support it physically.
	   This typically means that it's turned off for keyboards (which is what we want).
	   We prefer this method over XAutoRepeatOff/On, because the latter needs to
	   be reset before the program exits. */
	XkbSetDetectableAutoRepeat(win_display, true, &supported);
	Window_Exists = true;
}

void Window_SetTitle(const String* title) {
	char str[600]; 
	Platform_ConvertString(str, title);
	XStoreName(win_display, win_handle, str);
}

static char clipboard_copy_buffer[256];
static char clipboard_paste_buffer[256];
static String clipboard_copy_text  = String_FromArray(clipboard_copy_buffer);
static String clipboard_paste_text = String_FromArray(clipboard_paste_buffer);

void Window_GetClipboardText(String* value) {
	Window owner = XGetSelectionOwner(win_display, xa_clipboard);
	int i;
	if (!owner) return; /* no window owner */

	XConvertSelection(win_display, xa_clipboard, xa_utf8_string, xa_data_sel, win_handle, 0);
	clipboard_paste_text.length = 0;

	/* wait up to 1 second for SelectionNotify event to arrive */
	for (i = 0; i < 100; i++) {
		Window_ProcessEvents();
		if (clipboard_paste_text.length) {
			String_Copy(value, &clipboard_paste_text);
			return;
		} else {
			Thread_Sleep(10);
		}
	}
}

void Window_SetClipboardText(const String* value) {
	String_Copy(&clipboard_copy_text, value);
	XSetSelectionOwner(win_display, xa_clipboard, win_handle, 0);
}

static bool win_visible;
bool Window_GetVisible(void) { return win_visible; }

void Window_SetVisible(bool visible) {
	if (visible == win_visible) return;
	if (visible) {
		XMapWindow(win_display, win_handle);
	} else {
		XUnmapWindow(win_display, win_handle);
	}
}

void* Window_GetWindowHandle(void) { return win_handle; }

int Window_GetWindowState(void) {
	Atom prop_type;
	unsigned long items, after;
	int prop_format;
	Atom* data = NULL;

	XGetWindowProperty(win_display, win_handle,
		net_wm_state, 0, 256, false, xa_atom, &prop_type,
		&prop_format, &items, &after, &data);

	bool fullscreen = false, minimised = false;
	int maximised = 0, i;

	/* TODO: Check this works right */
	if (data && items) {
		for (i = 0; i < items; i++) {
			Atom atom = data[i];

			if (atom == net_wm_state_maximized_horizontal ||
				atom == net_wm_state_maximized_vertical) {
				maximised++;
			} else if (atom == net_wm_state_minimized) {
				minimised = true;
			} else if (atom == net_wm_state_fullscreen) {
				fullscreen = true;
			}
		}
	}
	if (data) XFree(data);

	if (minimised)      return WINDOW_STATE_MINIMISED;
	if (maximised == 2) return WINDOW_STATE_MAXIMISED;
	if (fullscreen)     return WINDOW_STATE_FULLSCREEN;
	return WINDOW_STATE_NORMAL;
}

void Window_SendNetWMState(long op, Atom a1, Atom a2) {
	XEvent ev = { 0 };
	ev.xclient.type = ClientMessage;
	ev.xclient.send_event = true;
	ev.xclient.window = win_handle;
	ev.xclient.message_type = net_wm_state;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = op;
	ev.xclient.data.l[1] = a1;
	ev.xclient.data.l[2] = a2;

	XSendEvent(win_display, win_rootWin, false,
		SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

void Window_SetWindowState(int state) {
	int current_state = Window_GetWindowState();
	if (current_state == state) return;

	/* Reset the current window state */
	if (current_state == WINDOW_STATE_MINIMISED) {
		XMapWindow(win_display, win_handle);
	} else if (current_state == WINDOW_STATE_FULLSCREEN) {
		Window_SendNetWMState(_NET_WM_STATE_REMOVE, net_wm_state_fullscreen, 0);
	} else if (current_state == WINDOW_STATE_MAXIMISED) {
		Window_SendNetWMState(_NET_WM_STATE_TOGGLE, net_wm_state_maximized_horizontal, 
			net_wm_state_maximized_vertical);
	}

	XSync(win_display, false);

	switch (state) {
	case WINDOW_STATE_NORMAL:
		XRaiseWindow(win_display, win_handle);
		break;

	case WINDOW_STATE_MAXIMISED:
		Window_SendNetWMState(_NET_WM_STATE_ADD, net_wm_state_maximized_horizontal,
			net_wm_state_maximized_vertical);
		XRaiseWindow(win_display, win_handle);
		break;

	case WINDOW_STATE_MINIMISED:
		/* TODO: multiscreen support */
		XIconifyWindow(win_display, win_handle, win_screen);
		break;

	case WINDOW_STATE_FULLSCREEN:
		Window_SendNetWMState(_NET_WM_STATE_ADD, net_wm_state_fullscreen, 0);
		XRaiseWindow(win_display, win_handle);
		break;
	}
	Window_ProcessEvents();
}

void Window_SetBounds(Rect2D rect) {
	int width  = rect.Width  - borderLeft - borderRight;
	int height = rect.Height - borderTop  - borderBottom;
	XMoveResizeWindow(win_display, win_handle, rect.X, rect.Y,
		max(width, 1), max(height, 1));
	Window_ProcessEvents();
}

void Window_SetLocation(int x, int y) {
	XMoveWindow(win_display, win_handle, x, y);
	Window_ProcessEvents();
}

void Window_SetSize(int width, int height) {
	int adjWidth  = width  - borderLeft - borderRight;
	int adjHeight = height - borderTop  - borderBottom;
	XResizeWindow(win_display, win_handle, adjWidth, adjHeight);
	Window_ProcessEvents();
}

void Window_SetClientSize(int width, int height) {
	XResizeWindow(win_display, win_handle, width, height);
	Window_ProcessEvents();
}

void Window_Close(void) {
	XEvent ev = { 0 };
	ev.type = ClientMessage;
	ev.xclient.format  = 32;
	ev.xclient.display = win_display;
	ev.xclient.window  = win_handle;
	ev.xclient.data.l[0] = wm_destroy;

	XSendEvent(win_display, win_handle, false, 0, &ev);
	XFlush(win_display);
}

void Window_Destroy(void) {
	XSync(win_display, true);
	XDestroyWindow(win_display, win_handle);
	Window_Exists = false;
}

void Window_ToggleKey(XKeyEvent* keyEvent, bool pressed) {
	KeySym keysym1 = XLookupKeysym(keyEvent, 0);
	KeySym keysym2 = XLookupKeysym(keyEvent, 1);

	Key key = Window_MapKey(keysym1);
	if (key == Key_None) key = Window_MapKey(keysym2);
	if (key != Key_None) Key_SetPressed(key, pressed);
}

Atom Window_GetSelectionProperty(XEvent* e) {
	Atom prop = e->xselectionrequest.property;
	if (prop) return prop;

	/* For obsolete clients. See ICCCM spec, selections chapter for reasoning. */
	return e->xselectionrequest.target;
}

bool Window_GetPendingEvent(XEvent* e) {
	return XCheckWindowEvent(win_display,   win_handle, win_eventMask, e) ||
		XCheckTypedWindowEvent(win_display, win_handle, ClientMessage, e) ||
		XCheckTypedWindowEvent(win_display, win_handle, SelectionNotify, e) ||
		XCheckTypedWindowEvent(win_display, win_handle, SelectionRequest, e);
}

void Window_ProcessEvents(void) {
	XEvent e;
	bool wasVisible, wasFocused;

	while (Window_Exists) {
		if (!Window_GetPendingEvent(&e)) break;

		switch (e.type) {
		case MapNotify:
		case UnmapNotify:
			wasVisible  = win_visible;
			win_visible = e.type == MapNotify;

			if (win_visible != wasVisible) {
				Event_RaiseVoid(&WindowEvents_VisibilityChanged);
			}
			break;

		case ClientMessage:
			if (!win_isExiting && e.xclient.data.l[0] == wm_destroy) {
				Platform_LogConst("Exit message received.");
				Event_RaiseVoid(&WindowEvents_Closing);

				win_isExiting = true;
				Window_Destroy();
				Event_RaiseVoid(&WindowEvents_Closed);
			} break;

		case DestroyNotify:
			Platform_LogConst("Window destroyed");
			Window_Exists = false;
			break;

		case ConfigureNotify:
			Window_RefreshBounds(&e);
			break;

		case Expose:
			if (e.xexpose.count == 0) {
				Event_RaiseVoid(&WindowEvents_Redraw);
			}
			break;

		case KeyPress:
		{
			Window_ToggleKey(&e.xkey, true);
			char data[16];
			int status = XLookupString(&e.xkey, data, Array_Elems(data), NULL, NULL);

			/* TODO: Does this work for every non-english layout? works for latin keys (e.g. finnish) */
			char raw; int i;
			for (i = 0; i < status; i++) {
				if (!Convert_TryUnicodeToCP437((uint8_t)data[i], &raw)) continue;
				Event_RaiseInt(&KeyEvents_Press, raw);
			}
		} break;

		case KeyRelease:
			/* TODO: raise KeyPress event. Use code from */
			/* http://anonsvn.mono-project.com/viewvc/trunk/mcs/class/Managed.Windows.Forms/System.Windows.Forms/X11Keyboard.cs?view=markup */
			Window_ToggleKey(&e.xkey, false);
			break;

		case ButtonPress:
			if (e.xbutton.button == 1)      Mouse_SetPressed(MouseButton_Left,   true);
			else if (e.xbutton.button == 2) Mouse_SetPressed(MouseButton_Middle, true);
			else if (e.xbutton.button == 3) Mouse_SetPressed(MouseButton_Right,  true);
			else if (e.xbutton.button == 4) Mouse_SetWheel(Mouse_Wheel + 1);
			else if (e.xbutton.button == 5) Mouse_SetWheel(Mouse_Wheel - 1);
			else if (e.xbutton.button == 6) Key_SetPressed(Key_XButton1, true);
			else if (e.xbutton.button == 7) Key_SetPressed(Key_XButton2, true);
			break;

		case ButtonRelease:
			if (e.xbutton.button == 1)      Mouse_SetPressed(MouseButton_Left, false);
			else if (e.xbutton.button == 2) Mouse_SetPressed(MouseButton_Middle, false);
			else if (e.xbutton.button == 3) Mouse_SetPressed(MouseButton_Right,  false);
			else if (e.xbutton.button == 6) Key_SetPressed(Key_XButton1, false);
			else if (e.xbutton.button == 7) Key_SetPressed(Key_XButton2, false);
			break;

		case MotionNotify:
			Mouse_SetPosition(e.xmotion.x, e.xmotion.y);
			break;

		case FocusIn:
		case FocusOut:
			/* Don't lose focus when another app grabs key or mouse */
			if (e.xfocus.mode == NotifyGrab || e.xfocus.mode == NotifyUngrab) break;
			wasFocused     = Window_Focused;
			Window_Focused = e.type == FocusIn;

			if (Window_Focused != wasFocused) {
				Event_RaiseVoid(&WindowEvents_FocusChanged);
			}
			break;

		case MappingNotify:
			if (e.xmapping.request == MappingModifier || e.xmapping.request == MappingKeyboard) {
				Platform_LogConst("keybard mapping refreshed");
				XRefreshKeyboardMapping(&e.xmapping);
			}
			break;

		case PropertyNotify:
			if (e.xproperty.atom == net_wm_state) {
				Event_RaiseVoid(&WindowEvents_StateChanged);
			}

			/*if (e.xproperty.atom == net_frame_extents) {
			     RefreshWindowBorders();
			}*/
			break;

		case SelectionNotify:
			clipboard_paste_text.length = 0;

			if (e.xselection.selection == xa_clipboard && e.xselection.target == xa_utf8_string && e.xselection.property == xa_data_sel) {
				Atom prop_type;
				int prop_format;
				unsigned long items, after;
				uint8_t* data = NULL;

				XGetWindowProperty(win_display, win_handle, xa_data_sel, 0, 1024, false, 0,
					&prop_type, &prop_format, &items, &after, &data);
				XDeleteProperty(win_display, win_handle, xa_data_sel);

				if (data && items && prop_type == xa_utf8_string) {
					clipboard_paste_text.length = 0;
					String_DecodeUtf8(&clipboard_paste_text, data, items);
				}
				if (data) XFree(data);
			}
			break;

		case SelectionRequest:
		{
			XEvent reply = { 0 };
			reply.xselection.type = SelectionNotify;
			reply.xselection.send_event = true;
			reply.xselection.display = win_display;
			reply.xselection.requestor = e.xselectionrequest.requestor;
			reply.xselection.selection = e.xselectionrequest.selection;
			reply.xselection.target = e.xselectionrequest.target;
			reply.xselection.property = NULL;
			reply.xselection.time = e.xselectionrequest.time;

			if (e.xselectionrequest.selection == xa_clipboard && e.xselectionrequest.target == xa_utf8_string && clipboard_copy_text.length) {
				reply.xselection.property = Window_GetSelectionProperty(&e);
				char str[800];
				int len = Platform_ConvertString(str, &clipboard_copy_text);

				XChangeProperty(win_display, reply.xselection.requestor, reply.xselection.property, xa_utf8_string, 8,
					PropModeReplace, str, len);
			} else if (e.xselectionrequest.selection == xa_clipboard && e.xselectionrequest.target == xa_targets) {
				reply.xselection.property = Window_GetSelectionProperty(&e);

				Atom data[2] = { xa_utf8_string, xa_targets };
				XChangeProperty(win_display, reply.xselection.requestor, reply.xselection.property, xa_atom, 32,
					PropModeReplace, data, 2);
			}
			XSendEvent(win_display, e.xselectionrequest.requestor, true, 0, &reply);
		} break;
		}
	}
}

Point2D Window_PointToClient(int x, int y) {
	Point2D p;
	Window child;
	XTranslateCoordinates(win_display, win_rootWin, win_handle, x, y, &p.X, &p.Y, &child);
	return p;
}

Point2D Window_PointToScreen(int x, int y) {
	Point2D p;
	Window child;
	XTranslateCoordinates(win_display, win_handle, win_rootWin, x, y, &p.X, &p.Y, &child);
	return p;
}

Point2D Window_GetScreenCursorPos(void) {
	Window rootW, childW;
	Point2D root, child;
	unsigned int mask;

	XQueryPointer(win_display, win_rootWin, &rootW, &childW, &root.X, &root.Y, &child.X, &child.Y, &mask);
	return root;
}

void Window_SetScreenCursorPos(int x, int y) {
	XWarpPointer(win_display, None, win_rootWin, 0, 0, 0, 0, x, y);
	XFlush(win_display); /* TODO: not sure if XFlush call is necessary */
}

static Cursor win_blankCursor;
static bool win_cursorVisible = true;
bool Window_GetCursorVisible(void) { return win_cursorVisible; }
void Window_SetCursorVisible(bool visible) {
	win_cursorVisible = visible;
	if (visible) {
		XUndefineCursor(win_display, win_handle);
	} else {
		if (!win_blankCursor) {
			char data  = 0;
			XColor col = { 0 };
			Pixmap pixmap   = XCreateBitmapFromData(win_display, win_handle, &data, 1, 1);
			win_blankCursor = XCreatePixmapCursor(win_display, pixmap, pixmap, &col, &col, 0, 0);
			XFreePixmap(win_display, pixmap);
		}
		XDefineCursor(win_display, win_handle, win_blankCursor);
	}
}


/*########################################################################################################################*
*-----------------------------------------------------OpenGL context------------------------------------------------------*
*#########################################################################################################################*/
static GLXContext ctx_Handle;
typedef int (*FN_GLXSWAPINTERVAL)(int interval);
static FN_GLXSWAPINTERVAL swapIntervalMESA, swapIntervalSGI;
static bool ctx_supports_vSync;

void GLContext_Init(struct GraphicsMode* mode) {
	static String ext_mesa = String_FromConst("GLX_MESA_swap_control");
	static String ext_sgi  = String_FromConst("GLX_SGI_swap_control");

	const char* raw_exts;
	String exts;
	ctx_Handle = glXCreateContext(win_display, &win_visual, NULL, true);

	if (!ctx_Handle) {
		Platform_LogConst("Context create failed. Trying indirect...");
		ctx_Handle = glXCreateContext(win_display, &win_visual, NULL, false);
	}
	if (!ctx_Handle) ErrorHandler_Fail("Failed to create context");

	if (!glXIsDirect(win_display, ctx_Handle)) {
		Platform_LogConst("== WARNING: Context is not direct ==");
	}
	if (!glXMakeCurrent(win_display, win_handle, ctx_Handle)) {
		ErrorHandler_Fail("Failed to make context current.");
	}

	/* GLX may return non-null function pointers that don't actually work */
	/* So we need to manually check the extensions string for support */
	raw_exts = glXQueryExtensionsString(win_display, win_screen);
	exts = String_FromReadonly(raw_exts);

	if (String_CaselessContains(&exts, &ext_mesa)) {
		swapIntervalMESA = (FN_GLXSWAPINTERVAL)GLContext_GetAddress("glXSwapIntervalMESA");
	}
	if (String_CaselessContains(&exts, &ext_sgi)) {
		swapIntervalSGI  = (FN_GLXSWAPINTERVAL)GLContext_GetAddress("glXSwapIntervalSGI");
	}
	ctx_supports_vSync = swapIntervalMESA || swapIntervalSGI;
}

void GLContext_Update(void) { }
void GLContext_Free(void) {
	if (!ctx_Handle) return;

	if (glXGetCurrentContext() == ctx_Handle) {
		glXMakeCurrent(win_display, None, NULL);
	}
	glXDestroyContext(win_display, ctx_Handle);
	ctx_Handle = NULL;
}

void* GLContext_GetAddress(const char* function) {
	void* address = glXGetProcAddress(function);
	return GLContext_IsInvalidAddress(address) ? NULL : address;
}

void GLContext_SwapBuffers(void) {
	glXSwapBuffers(win_display, win_handle);
}

void GLContext_SetVSync(bool enabled) {
	int res;
	if (!ctx_supports_vSync) return;

	if (swapIntervalMESA) {
		res = swapIntervalMESA(enabled);
	} else {
		res = swapIntervalSGI(enabled);
	}
	if (res) Platform_Log1("Set VSync failed, error: %i", &res);
}

static void GLContext_GetAttribs(struct GraphicsMode* mode, int* attribs) {
	int i = 0;
	/* See http://www-01.ibm.com/support/knowledgecenter/ssw_aix_61/com.ibm.aix.opengl/doc/openglrf/glXChooseFBConfig.htm%23glxchoosefbconfig */
	/* See http://www-01.ibm.com/support/knowledgecenter/ssw_aix_71/com.ibm.aix.opengl/doc/openglrf/glXChooseVisual.htm%23b5c84be452rree */
	/* for the attribute declarations. Note that the attributes are different than those used in glxChooseVisual */

	if (!mode->IsIndexed) { attribs[i++] = GLX_RGBA; }
	attribs[i++] = GLX_RED_SIZE;   attribs[i++] = mode->R;
	attribs[i++] = GLX_GREEN_SIZE; attribs[i++] = mode->G;
	attribs[i++] = GLX_BLUE_SIZE;  attribs[i++] = mode->B;
	attribs[i++] = GLX_ALPHA_SIZE; attribs[i++] = mode->A;

	if (mode->DepthBits) {
		attribs[i++] = GLX_DEPTH_SIZE;   attribs[i++] = mode->DepthBits;
	}
	if (mode->StencilBits) {
		attribs[i++] = GLX_STENCIL_SIZE; attribs[i++] = mode->StencilBits;
	}
	if (mode->Buffers > 1) { attribs[i++] = GLX_DOUBLEBUFFER; }

	attribs[i++] = 0;
}

static XVisualInfo GLContext_SelectVisual(struct GraphicsMode* mode) {
	int attribs[20];
	int major, minor;
	XVisualInfo* visual = NULL;

	int fbcount;
	GLXFBConfig* fbconfigs;
	XVisualInfo info;

	GLContext_GetAttribs(mode, attribs);	
	if (!glXQueryVersion(win_display, &major, &minor)) {
		ErrorHandler_Fail("glXQueryVersion failed");
	}

	if (major >= 1 && minor >= 3) {
		/* ChooseFBConfig returns an array of GLXFBConfig opaque structures */
		fbconfigs = glXChooseFBConfig(win_display, win_screen, attribs, &fbcount);
		if (fbconfigs && fbcount) {
			/* Use the first GLXFBConfig from the fbconfigs array (best match) */
			visual = glXGetVisualFromFBConfig(win_display, *fbconfigs);
			XFree(fbconfigs);
		}
	}

	if (!visual) {
		Platform_LogConst("Falling back to glXChooseVisual.");
		visual = glXChooseVisual(win_display, win_screen, attribs);
	}
	if (!visual) {
		ErrorHandler_Fail("Requested GraphicsMode not available.");
	}

	info = *visual;
	XFree(visual);
	return info;
}
#endif


/*########################################################################################################################*
*------------------------------------------------------Carbon window------------------------------------------------------*
*#########################################################################################################################*/
#ifdef CC_BUILD_OSX
#include <AGL/agl.h>

static WindowRef win_handle;
static int title_height;
static int win_state;
/* Hacks for fullscreen */
static bool ctx_pendingWindowed, ctx_pendingFullscreen;

#define Rect_Width(rect)  (rect.right  - rect.left)
#define Rect_Height(rect) (rect.bottom - rect.top)

/*########################################################################################################################*
*-----------------------------------------------------Private details-----------------------------------------------------*
*#########################################################################################################################*/
static Key Window_MapKey(UInt32 key) {
	/* Sourced from https://www.meandmark.com/keycodes.html */
	switch (key) {
		case 0x00: return Key_A;
		case 0x01: return Key_S;
		case 0x02: return Key_D;
		case 0x03: return Key_F;
		case 0x04: return Key_H;
		case 0x05: return Key_G;
		case 0x06: return Key_Z;
		case 0x07: return Key_X;
		case 0x08: return Key_C;
		case 0x09: return Key_V;
		case 0x0B: return Key_B;
		case 0x0C: return Key_Q;
		case 0x0D: return Key_W;
		case 0x0E: return Key_E;
		case 0x0F: return Key_R;
			
		case 0x10: return Key_Y;
		case 0x11: return Key_T;
		case 0x12: return Key_1;
		case 0x13: return Key_2;
		case 0x14: return Key_3;
		case 0x15: return Key_4;
		case 0x16: return Key_6;
		case 0x17: return Key_5;
		case 0x18: return Key_Plus;
		case 0x19: return Key_9;
		case 0x1A: return Key_7;
		case 0x1B: return Key_Minus;
		case 0x1C: return Key_8;
		case 0x1D: return Key_0;
		case 0x1E: return Key_BracketRight;
		case 0x1F: return Key_O;
			
		case 0x20: return Key_U;
		case 0x21: return Key_BracketLeft;
		case 0x22: return Key_I;
		case 0x23: return Key_P;
		case 0x24: return Key_Enter;
		case 0x25: return Key_L;
		case 0x26: return Key_J;
		case 0x27: return Key_Quote;
		case 0x28: return Key_K;
		case 0x29: return Key_Semicolon;
		case 0x2A: return Key_BackSlash;
		case 0x2B: return Key_Comma;
		case 0x2C: return Key_Slash;
		case 0x2D: return Key_N;
		case 0x2E: return Key_M;
		case 0x2F: return Key_Period;
			
		case 0x30: return Key_Tab;
		case 0x31: return Key_Space;
		case 0x32: return Key_Tilde;
		case 0x33: return Key_BackSpace;
		case 0x35: return Key_Escape;
		/*case 0x37: return Key_WinLeft; */
		/*case 0x38: return Key_ShiftLeft; */
		case 0x39: return Key_CapsLock;
		/*case 0x3A: return Key_AltLeft; */
		/*case 0x3B: return Key_ControlLeft; */
			
		case 0x41: return Key_KeypadDecimal;
		case 0x43: return Key_KeypadMultiply;
		case 0x45: return Key_KeypadAdd;
		case 0x4B: return Key_KeypadDivide;
		case 0x4C: return Key_KeypadEnter;
		case 0x4E: return Key_KeypadSubtract;
			
		case 0x51: return Key_KeypadEnter;
		case 0x52: return Key_Keypad0;
		case 0x53: return Key_Keypad1;
		case 0x54: return Key_Keypad2;
		case 0x55: return Key_Keypad3;
		case 0x56: return Key_Keypad4;
		case 0x57: return Key_Keypad5;
		case 0x58: return Key_Keypad6;
		case 0x59: return Key_Keypad7;
		case 0x5B: return Key_Keypad8;
		case 0x5C: return Key_Keypad9;
		case 0x5D: return Key_N;
		case 0x5E: return Key_M;
		case 0x5F: return Key_Period;
			
		case 0x60: return Key_F5;
		case 0x61: return Key_F6;
		case 0x62: return Key_F7;
		case 0x63: return Key_F3;
		case 0x64: return Key_F8;
		case 0x65: return Key_F9;
		case 0x67: return Key_F11;
		case 0x69: return Key_F13;
		case 0x6B: return Key_F14;
		case 0x6D: return Key_F10;
		case 0x6F: return Key_F12;
			
		case 0x70: return Key_U;
		case 0x71: return Key_F15;
		case 0x72: return Key_Insert;
		case 0x73: return Key_Home;
		case 0x74: return Key_PageUp;
		case 0x75: return Key_Delete;
		case 0x76: return Key_F4;
		case 0x77: return Key_End;
		case 0x78: return Key_F2;
		case 0x79: return Key_PageDown;
		case 0x7A: return Key_F1;
		case 0x7B: return Key_Left;
		case 0x7C: return Key_Right;
		case 0x7D: return Key_Down;
		case 0x7E: return Key_Up;
	}
	return Key_None;
	/* TODO: Verify these differences */
	/*
	 Backspace = 51,  (0x33, Key_Delete according to that link)
	 Return = 52,     (0x34, ??? according to that link)
	 Menu = 110,      (0x6E, ??? according to that ink)
	 */
}

static void Window_Destroy(void) {
	if (!Window_Exists) return;
	DisposeWindow(win_handle);
	Window_Exists = false;
}

static void Window_UpdateSize(void) {
	Rect r;
	OSStatus res;
	if (win_state == WINDOW_STATE_FULLSCREEN) return;
	
	res = GetWindowBounds(win_handle, kWindowStructureRgn, &r);
	if (res) ErrorHandler_Fail2(res, "Getting window bounds");
	Window_Bounds.X = r.left;
	Window_Bounds.Y = r.top;
	Window_Bounds.Width  = Rect_Width(r);
	Window_Bounds.Height = Rect_Height(r);
	
	res = GetWindowBounds(win_handle, kWindowGlobalPortRgn, &r);
	if (res) ErrorHandler_Fail2(res, "Getting window clientsize");
	Window_ClientSize.Width  = Rect_Width(r);
	Window_ClientSize.Height = Rect_Height(r);
}

static void Window_UpdateWindowState(void) {
	Point idealSize;
	OSStatus res;

	switch (win_state) {
	case WINDOW_STATE_FULLSCREEN:
		ctx_pendingFullscreen = true;
		break;

	case WINDOW_STATE_MAXIMISED:
		/* Hack because OSX has no concept of maximised. Instead windows are "zoomed", 
		meaning they are maximised up to their reported ideal size. So report a large ideal size. */
		idealSize.v = 9000; idealSize.h = 9000;
		res = ZoomWindowIdeal(win_handle, inZoomOut, &idealSize);
		if (res) ErrorHandler_Fail2(res, "Maximising window");
		break;

	case WINDOW_STATE_NORMAL:
		if (Window_GetWindowState() == WINDOW_STATE_MAXIMISED) {
			idealSize.v = 0; idealSize.h = 0;
			res = ZoomWindowIdeal(win_handle, inZoomIn, &idealSize);
			if (res) ErrorHandler_Fail2(res, "Un-maximising window");
		}
		break;

	case WINDOW_STATE_MINIMISED:
		res = CollapseWindow(win_handle, true);
		if (res) ErrorHandler_Fail2(res, "Minimising window");
		break;
	}

	Event_RaiseVoid(&WindowEvents_StateChanged);
	Window_UpdateSize();
	Event_RaiseVoid(&WindowEvents_Resized);
}

OSStatus Window_ProcessKeyboardEvent(EventHandlerCallRef inCaller, EventRef inEvent, void* userData) {
	UInt32 kind, code;
	Key key;
	char charCode, raw;
	bool repeat;
	OSStatus res;
	
	kind = GetEventKind(inEvent);
	switch (kind) {
		case kEventRawKeyDown:
		case kEventRawKeyRepeat:
		case kEventRawKeyUp:
			res = GetEventParameter(inEvent, kEventParamKeyCode, typeUInt32, 
									NULL, sizeof(UInt32), NULL, &code);
			if (res) ErrorHandler_Fail2(res, "Getting key button");
			
			res = GetEventParameter(inEvent, kEventParamKeyMacCharCodes, typeChar, 
									NULL, sizeof(char), NULL, &charCode);
			if (res) ErrorHandler_Fail2(res, "Getting key char");
			
			key = Window_MapKey(code);
			if (key == Key_None) {
				Platform_Log1("Key %i not mapped, ignoring press.", &code);
				return 0;
			}
			break;
	}

	switch (kind) {
		/* TODO: Should we be messing with KeyRepeat in kEventRawKeyRepeat here? */
		/* Looking at documentation, probably not */
		case kEventRawKeyDown:
		case kEventRawKeyRepeat:
			Key_SetPressed(key, true);

			/* TODO: Should we be using kEventTextInputUnicodeForKeyEvent for this */
			/* Look at documentation for kEventRawKeyRepeat */
			if (!Convert_TryUnicodeToCP437((uint8_t)charCode, &raw)) return 0;
			Event_RaiseInt(&KeyEvents_Press, raw);
			return 0;
			
		case kEventRawKeyUp:
			Key_SetPressed(key, false);
			return 0;
			
		case kEventRawKeyModifiersChanged:
			res = GetEventParameter(inEvent, kEventParamKeyModifiers, typeUInt32, 
									NULL, sizeof(UInt32), NULL, &code);
			if (res) ErrorHandler_Fail2(res, "Getting key modifiers");
			
			/* TODO: Is this even needed */
			repeat = Key_KeyRepeat; 
			Key_KeyRepeat = false;
			
			Key_SetPressed(Key_ControlLeft, (code & 0x1000) != 0);
			Key_SetPressed(Key_AltLeft,     (code & 0x0800) != 0);
			Key_SetPressed(Key_ShiftLeft,   (code & 0x0200) != 0);
			Key_SetPressed(Key_WinLeft,     (code & 0x0100) != 0);			
			Key_SetPressed(Key_CapsLock,    (code & 0x0400) != 0);
			
			Key_KeyRepeat = repeat;
			return 0;
	}
	return eventNotHandledErr;
}

OSStatus Window_ProcessWindowEvent(EventHandlerCallRef inCaller, EventRef inEvent, void* userData) {
	int width, height;
	
	switch (GetEventKind(inEvent)) {
		case kEventWindowClose:
			Event_RaiseVoid(&WindowEvents_Closing);
			return eventNotHandledErr;
			
		case kEventWindowClosed:
			Window_Exists = false;
			Event_RaiseVoid(&WindowEvents_Closed);
			return 0;
			
		case kEventWindowBoundsChanged:
			width  = Window_ClientSize.Width;
			height = Window_ClientSize.Height;
			Window_UpdateSize();
			
			if (width != Window_ClientSize.Width || height != Window_ClientSize.Height) {
				Event_RaiseVoid(&WindowEvents_Resized);
			}
			return eventNotHandledErr;
			
		case kEventWindowActivated:
			Window_Focused = true;
			Event_RaiseVoid(&WindowEvents_FocusChanged);
			return eventNotHandledErr;
			
		case kEventWindowDeactivated:
			Window_Focused = false;
			Event_RaiseVoid(&WindowEvents_FocusChanged);
			return eventNotHandledErr;
	}
	return eventNotHandledErr;
}

OSStatus Window_ProcessMouseEvent(EventHandlerCallRef inCaller, EventRef inEvent, void* userData) {
	HIPoint pt;
	Point2D mousePos;
	UInt32 kind;
	bool down;
	EventMouseButton button;
	SInt32 delta;
	OSStatus res;	
	
	if (win_state == WINDOW_STATE_FULLSCREEN) {
		res = GetEventParameter(inEvent, kEventParamMouseLocation, typeHIPoint,
								NULL, sizeof(HIPoint), NULL, &pt);
	} else {
		res = GetEventParameter(inEvent, kEventParamWindowMouseLocation, typeHIPoint, 
								NULL, sizeof(HIPoint), NULL, &pt);
	}
	
	/* this error comes up from the application event handler */
	if (res && res != eventParameterNotFoundErr) {
		ErrorHandler_Fail2(res, "Getting mouse position");
	}
	
	mousePos.X = (int)pt.x; mousePos.Y = (int)pt.y;
	/* Location is relative to structure (i.e. external size) of window */
	if (win_state != WINDOW_STATE_FULLSCREEN) {
		mousePos.Y -= title_height;
	}
	
	kind = GetEventKind(inEvent);
	switch (kind) {
		case kEventMouseDown:
		case kEventMouseUp:
			down = kind == kEventMouseDown;
			res  = GetEventParameter(inEvent, kEventParamMouseButton, typeMouseButton, 
									 NULL, sizeof(EventMouseButton), NULL, &button);
			if (res) ErrorHandler_Fail2(res, "Getting mouse button");
			
			switch (button) {
				case kEventMouseButtonPrimary:
					Mouse_SetPressed(MouseButton_Left, down); break;
				case kEventMouseButtonSecondary:
					Mouse_SetPressed(MouseButton_Right, down); break;
				case kEventMouseButtonTertiary:
					Mouse_SetPressed(MouseButton_Middle, down); break;
			}
			return 0;
			
		case kEventMouseWheelMoved:
			res = GetEventParameter(inEvent, kEventParamMouseWheelDelta, typeSInt32,
									NULL, sizeof(SInt32), NULL, &delta);
			if (res) ErrorHandler_Fail2(res, "Getting mouse wheel delta");
			Mouse_SetWheel(Mouse_Wheel + delta);
			return 0;
			
		case kEventMouseMoved:
		case kEventMouseDragged:
			if (win_state != WINDOW_STATE_FULLSCREEN) {
				/* Ignore clicks in the title bar */
				if (pt.y < 0) return eventNotHandledErr;
			}
			
			if (mousePos.X != Mouse_X || mousePos.Y != Mouse_Y) {
				Mouse_SetPosition(mousePos.X, mousePos.Y);
			}
			return eventNotHandledErr;
	}
	return eventNotHandledErr;
}

static OSStatus Window_EventHandler(EventHandlerCallRef inCaller, EventRef inEvent, void* userData) {
	EventRecord record;
	
	switch (GetEventClass(inEvent)) {
		case kEventClassAppleEvent:
			/* Only event here is the apple event. */
			Platform_LogConst("Processing apple event.");
			ConvertEventRefToEventRecord(inEvent, &record);
			AEProcessAppleEvent(&record);
			break;
			
		case kEventClassKeyboard:
			return Window_ProcessKeyboardEvent(inCaller, inEvent, userData);
		case kEventClassMouse:
			return Window_ProcessMouseEvent(inCaller, inEvent, userData);
		case kEventClassWindow:
			return Window_ProcessWindowEvent(inCaller, inEvent, userData);
	}
	return eventNotHandledErr;
}

static void Window_ConnectEvents(void) {
	static EventTypeSpec eventTypes[] = {
		{ kEventClassApplication, kEventAppActivated },
		{ kEventClassApplication, kEventAppDeactivated },
		{ kEventClassApplication, kEventAppQuit },
		
		{ kEventClassMouse, kEventMouseDown },
		{ kEventClassMouse, kEventMouseUp },
		{ kEventClassMouse, kEventMouseMoved },
		{ kEventClassMouse, kEventMouseDragged },
		{ kEventClassMouse, kEventMouseEntered},
		{ kEventClassMouse, kEventMouseExited },
		{ kEventClassMouse, kEventMouseWheelMoved },
		
		{ kEventClassKeyboard, kEventRawKeyDown },
		{ kEventClassKeyboard, kEventRawKeyRepeat },
		{ kEventClassKeyboard, kEventRawKeyUp },
		{ kEventClassKeyboard, kEventRawKeyModifiersChanged },
		
		{ kEventClassWindow, kEventWindowClose },
		{ kEventClassWindow, kEventWindowClosed },
		{ kEventClassWindow, kEventWindowBoundsChanged },
		{ kEventClassWindow, kEventWindowActivated },
		{ kEventClassWindow, kEventWindowDeactivated },
		
		{ kEventClassAppleEvent, kEventAppleEvent }
	};
	EventTargetRef target;
	OSStatus res;
	
	target = GetApplicationEventTarget();
	/* TODO: Use EventTargetRef target = GetWindowEventTarget(windowRef); instead?? */
	res = InstallEventHandler(target, NewEventHandlerUPP(Window_EventHandler),
							  Array_Elems(eventTypes), eventTypes, NULL, NULL);
	if (res) ErrorHandler_Fail2(res, "Connecting events");
}


/*########################################################################################################################*
 *--------------------------------------------------Public implementation--------------------------------------------------*
 *#########################################################################################################################*/
void Window_Create(int x, int y, int width, int height, struct GraphicsMode* mode) {
	Rect r;
	OSStatus res;
	ProcessSerialNumber psn;
	
	r.left = x; r.right  = x + width; 
	r.top  = y; r.bottom = y + height;
	res = CreateNewWindow(kDocumentWindowClass,
						  kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute |
						  kWindowInWindowMenuAttribute | kWindowLiveResizeAttribute,
						  &r, &win_handle);
	if (res) ErrorHandler_Fail2(res, "Failed to create window");

	Window_SetLocation(r.left, r.right);
	Window_SetSize(Rect_Width(r), Rect_Height(r));
	Window_UpdateSize();
	
	res = GetWindowBounds(win_handle, kWindowTitleBarRgn, &r);
	if (res) ErrorHandler_Fail2(res, "Failed to get titlebar size");
	title_height = Rect_Height(r);
	AcquireRootMenu();
	
	/* TODO: Apparently GetCurrentProcess is needed */
	GetCurrentProcess(&psn);
	/* NOTE: TransformProcessType is OSX 10.3 or later */
	TransformProcessType(&psn, kProcessTransformToForegroundApplication);
	SetFrontProcess(&psn);
	
	/* TODO: Use BringWindowToFront instead.. (look in the file which has RepositionWindow in it) !!!! */
	Window_ConnectEvents();
	Window_Exists = true;
}

void Window_SetTitle(const String* title) {
	char str[600];
	CFStringRef titleCF;
	int len;
	
	/* TODO: This leaks memory, old title isn't released */
	len     = Platform_ConvertString(str, title);
	titleCF = CFStringCreateWithBytes(kCFAllocatorDefault, str, len, kCFStringEncodingUTF8, false);
	SetWindowTitleWithCFString(win_handle, titleCF);
}

/* NOTE: All Pasteboard functions are OSX 10.3 or later */
PasteboardRef Window_GetPasteboard(void) {
	PasteboardRef pbRef;
	OSStatus err = PasteboardCreate(kPasteboardClipboard, &pbRef);
	
	if (err) ErrorHandler_Fail2(err, "Creating Pasteboard reference");
	PasteboardSynchronize(pbRef);
	return pbRef;
}
#define FMT_UTF8  CFSTR("public.utf8-plain-text")
#define FMT_UTF16 CFSTR("public.utf16-plain-text")

void Window_GetClipboardText(String* value) {
	PasteboardRef pbRef;
	ItemCount itemCount;
	PasteboardItemID itemID;
	CFDataRef outData;
	const UInt8* ptr;
	OSStatus err;
	
	pbRef = Window_GetPasteboard();
	
	err = PasteboardGetItemCount(pbRef, &itemCount);
	if (err) ErrorHandler_Fail2(err, "Getting item count from Pasteboard");
	if (itemCount < 1) return;
	
	err = PasteboardGetItemIdentifier(pbRef, 1, &itemID);
	if (err) ErrorHandler_Fail2(err, "Getting item identifier from Pasteboard");
	
	if (!(err = PasteboardCopyItemFlavorData(pbRef, itemID, FMT_UTF16, &outData))) {
		ptr = CFDataGetBytePtr(outData);
		if (!ptr) ErrorHandler_Fail("CFDataGetBytePtr() returned null pointer");
		return Marshal.PtrToStringUni(ptr);
	} else if (!(err = PasteboardCopyItemFlavorData(pbRef, itemID, FMT_UTF8, &outData))) {
		ptr = CFDataGetBytePtr(outData);
		if (!ptr) ErrorHandler_Fail("CFDataGetBytePtr() returned null pointer");
		return GetUTF8(ptr);
	}
}

void Window_SetClipboardText(const String* value) {
	PasteboardRef pbRef;
	CFDataRef cfData;
	char str[800];
	int len;
	OSStatus err;

	pbRef = Window_GetPasteboard();
	err   = PasteboardClear(pbRef);
	if (err) ErrorHandler_Fail2(err, "Clearing Pasteboard");
	PasteboardSynchronize(pbRef);

	len = Platform_ConvertString(str, value);
	CFDataCreate(NULL, str, len);
	if (!cfData) ErrorHandler_Fail("CFDataCreate() returned null pointer");

	PasteboardPutItemFlavor(pbRef, 1, FMT_UTF8, cfData, 0);
}
/* TODO: IMPLEMENT void Window_SetIcon(Bitmap* bmp); */

bool Window_GetVisible(void) { return IsWindowVisible(win_handle); }
void Window_SetVisible(bool visible) {
	if (visible == Window_GetVisible()) return;
	
	if (visible) {
		ShowWindow(win_handle);
		RepositionWindow(win_handle, NULL, kWindowCenterOnMainScreen);
		SelectWindow(win_handle);
	} else {
		HideWindow(win_handle);
	}
}

void* Window_GetWindowHandle(void) { return win_handle; }

int Window_GetWindowState(void) {
	if (win_state == WINDOW_STATE_FULLSCREEN)
		return WINDOW_STATE_FULLSCREEN;
	if (IsWindowCollapsed(win_handle))
		return WINDOW_STATE_MINIMISED;
	if (IsWindowInStandardState(win_handle, NULL, NULL))
		return WINDOW_STATE_MAXIMISED;
	return WINDOW_STATE_NORMAL;
}
void Window_SetWindowState(int state) {
	int old_state = Window_GetWindowState();
	OSStatus err;

	if (state == old_state) return;
	win_state = state;

	if (old_state == WINDOW_STATE_FULLSCREEN) {
		ctx_pendingWindowed = true;
		/* When returning from full screen, wait until the context is updated to actually do the work. */
		return;
	}
	if (old_state == WINDOW_STATE_MINIMISED) {
		err = CollapseWindow(win_handle, false);
		if (err) ErrorHandler_Fail2(err, "Un-minimising window");
	}
	Window_UpdateWindowState();
}

void Window_SetBounds(Rect2D rect) {
	Window_SetLocation(rect.X, rect.Y);
	Window_SetSize(rect.Width, rect.Height);
}

void Window_SetLocation(int x, int y) {
	MoveWindow(win_handle, x, y, false);
}

void Window_SetSize(int width, int height) {
	/* SizeWindow works in client size */
	/* But SetSize is window size, so reduce it */
	width  -= (Window_Bounds.Width  - Window_ClientSize.Width);
	height -= (Window_Bounds.Height - Window_ClientSize.Height);
	
	SizeWindow(win_handle, width, height, true);
}

void Window_SetClientSize(int width, int height) {
	SizeWindow(win_handle, width, height, true);
}

void Window_Close(void) {
	Event_RaiseVoid(&WindowEvents_Closed);
	/* TODO: Does this raise the event twice? */
	Window_Destroy();
}

void Window_ProcessEvents(void) {
	EventRef theEvent;
	EventTargetRef target = GetEventDispatcherTarget();
	OSStatus res;
	
	for (;;) {
		res = ReceiveNextEvent(0, NULL, 0.0, true, &theEvent);
		if (res == eventLoopTimedOutErr) break;
		
		if (res) {
			Platform_Log1("Message Loop status: %i", &res); break;
		}
		if (!theEvent) break;
		
		SendEventToEventTarget(theEvent, target);
		ReleaseEvent(theEvent);
	}
}

Point2D Window_PointToClient(int x, int y) {
	Rect r;
	Point2D p;
	GetWindowBounds(win_handle, kWindowContentRgn, &r);
	
	p.X = x - r.left; p.Y = y - r.top;
	return p;
}

Point2D Window_PointToScreen(int x, int y) {
	Rect r;
	Point2D p;
	GetWindowBounds(win_handle, kWindowContentRgn, &r);
	
	p.X = x + r.left; p.Y = y + r.top;
	return p;
}

Point2D Window_GetScreenCursorPos(void) {
	HIPoint point;
	Point2D p;
	/* NOTE: HIGetMousePosition is OSX 10.5 or later */
	/* TODO: Use GetGlobalMouse instead!!!! */
	HIGetMousePosition(kHICoordSpaceScreenPixel, NULL, &point);
	
	p.X = (int)point.x; p.Y = (int)point.y;
	return p;
}

void Window_SetScreenCursorPos(int x, int y) {
	CGPoint point;
	point.x = x; point.y = y;
	
	CGAssociateMouseAndMouseCursorPosition(0);
	CGDisplayMoveCursorToPoint(CGMainDisplayID(), point);
	CGAssociateMouseAndMouseCursorPosition(1);
}

static bool win_cursorVisible;
bool Window_GetCursorVisible(void) { return win_cursorVisible; }

void Window_SetCursorVisible(bool visible) {
	win_cursorVisible = visible;
	if (visible) {
		CGDisplayShowCursor(CGMainDisplayID());
	} else {
		CGDisplayHideCursor(CGMainDisplayID());
	}
}


/*########################################################################################################################*
*-----------------------------------------------------OpenGL context------------------------------------------------------*
*#########################################################################################################################*/
static AGLContext ctx_handle;
static bool ctx_fullscreen, ctx_firstFullscreen;
static Rect2D ctx_windowedBounds;

static void GLContext_Check(int code, const char* place) {
	ReturnCode res;
	if (code) return;

	res = aglGetError();
	if (res) ErrorHandler_Fail2(res, place);
}

static void GLContext_MakeCurrent(void) {
	int code = aglSetCurrentContext(ctx_handle);
	GLContext_Check(code, "Setting GL context");
}

static void GLContext_SetDrawable(void) {
	CGrafPtr windowPort = GetWindowPort(win_handle);
	int code = aglSetDrawable(ctx_handle, windowPort);
	GLContext_Check(code, "Attaching GL context");
}

static void GLContext_GetAttribs(struct GraphicsMode* mode, int* attribs, bool fullscreen) {
	int i = 0;

	if (!mode->IsIndexed) { attribs[i++] = AGL_RGBA; }
	attribs[i++] = AGL_RED_SIZE;   attribs[i++] = mode->R;
	attribs[i++] = AGL_GREEN_SIZE; attribs[i++] = mode->G;
	attribs[i++] = AGL_BLUE_SIZE;  attribs[i++] = mode->B;
	attribs[i++] = AGL_ALPHA_SIZE; attribs[i++] = mode->A;

	if (mode->DepthBits) {
		attribs[i++] = AGL_DEPTH_SIZE;   attribs[i++] = mode->DepthBits;
	}
	if (mode->StencilBits) {
		attribs[i++] = AGL_STENCIL_SIZE; attribs[i++] = mode->StencilBits;
	}

	if (mode->Buffers > 1) { attribs[i++] = AGL_DOUBLEBUFFER; }
	if (fullscreen)        { attribs[i++] = AGL_FULLSCREEN; }

	attribs[i++] = 0;
}

static void GLContext_UnsetFullscreen(void) {
	int code;
	Platform_LogConst("Unsetting AGL fullscreen.");

	code = aglSetDrawable(ctx_handle, NULL);
	GLContext_Check(code, "Unattaching GL context");
	code = aglUpdateContext(ctx_handle);
	GLContext_Check(code, "Updating GL context (from Fullscreen)");

	CGDisplayRelease(CGMainDisplayID());
	GLContext_SetDrawable();

	ctx_fullscreen = false;
	Window_UpdateWindowState();
	Window_SetSize(ctx_windowedBounds.Width, ctx_windowedBounds.Height);
}

static void GLContext_SetFullscreen(void) {
	int displayWidth  = DisplayDevice_Default.Bounds.Width;
	int displayHeight = DisplayDevice_Default.Bounds.Height;
	int code;

	Platform_LogConst("Switching to AGL fullscreen");
	CGDisplayCapture(CGMainDisplayID());

	code = aglSetFullScreen(ctx_handle, displayWidth, displayHeight, 0, 0);
	GLContext_Check(code, "aglSetFullScreen");
	GLContext_MakeCurrent();

	/* This is a weird hack to workaround a bug where the first time a context */
	/* is made fullscreen, we just end up with a blank screen.  So we undo it as fullscreen */
	/* and redo it as fullscreen. */
	if (!ctx_firstFullscreen) {
		ctx_firstFullscreen = true;
		GLContext_UnsetFullscreen();
		GLContext_SetFullscreen();
		return;
	}

	ctx_fullscreen     = true;
	ctx_windowedBounds = Window_Bounds;

	Window_ClientSize.Width = displayWidth;
	Window_ClientSize.Width = displayHeight;

	Window_Bounds = DisplayDevice_Default.Bounds;
	win_state     = WINDOW_STATE_FULLSCREEN;
}

void GLContext_Init(struct GraphicsMode* mode) {
	int attribs[20];
	AGLPixelFormat fmt;
	GDHandle gdevice;
	OSStatus res;

	/* Initially try creating fullscreen compatible context */	
	res = DMGetGDeviceByDisplayID(CGMainDisplayID(), &gdevice, false);
	if (res) ErrorHandler_Fail2(res, "Getting display device failed");

	GLContext_GetAttribs(mode, attribs, true);
	fmt = aglChoosePixelFormat(&gdevice, 1, attribs);
	res = aglGetError();

	/* Try again with non-compatible context if that fails */
	if (!fmt || res == AGL_BAD_PIXELFMT) {
		Platform_LogConst("Failed to create full screen pixel format.");
		Platform_LogConst("Trying again to create a non-fullscreen pixel format.");

		GLContext_GetAttribs(mode, attribs, false);
		fmt = aglChoosePixelFormat(NULL, 0, attribs);
		res = aglGetError();
	}
	if (res) ErrorHandler_Fail2(res, "Choosing pixel format");

	ctx_handle = aglCreateContext(fmt, NULL);
	GLContext_Check(0, "Creating GL context");

	aglDestroyPixelFormat(fmt);
	GLContext_Check(0, "Destroying pixel format");

	GLContext_SetDrawable();
	GLContext_Update();
	GLContext_MakeCurrent();
}

void GLContext_Update(void) {
	if (ctx_pendingFullscreen) {
		ctx_pendingFullscreen = false;
		GLContext_SetFullscreen();
		return;
	} else if (ctx_pendingWindowed) {
		ctx_pendingWindowed = false;
		GLContext_UnsetFullscreen();
	}

	if (ctx_fullscreen) return;
	GLContext_SetDrawable();
	aglUpdateContext(ctx_handle);
}

void GLContext_Free(void) {
	int code;
	if (!ctx_handle) return;

	code = aglSetCurrentContext(NULL);
	GLContext_Check(code, "Unsetting GL context");

	code = aglDestroyContext(ctx_handle);
	GLContext_Check(code, "Destroying GL context");
	ctx_handle = NULL;
}

void* GLContext_GetAddress(const char* function) {
	/* TODO: Apparently we don't need this for OSX */
	return NULL;
}

void GLContext_SwapBuffers(void) {
	aglSwapBuffers(ctx_handle);
	GLContext_Check(0, "Swapping buffers");
}

void GLContext_SetVSync(bool enabled) {
	int value = enabled ? 1 : 0;
	aglSetInteger(ctx_handle, AGL_SWAP_INTERVAL, &value);
}
#endif
