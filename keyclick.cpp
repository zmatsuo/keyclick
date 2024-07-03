
#include <stdio.h>
#include <windows.h>

#include "resource.h"

#define TIMEOUT_MS	500

#define	WM_APP_TRAY_CLICK	WM_USER

typedef struct {
	HWND hWnd;
	bool mouse_button_pushed[5];
	enum MODIFY_STATE {
		MODIFY_STATE_RELEASE,			// 離されている
		MODIFY_STATE_DOWN_OR_MODIFY,	// 押された or 修飾状態
		MODIFY_STATE_MODIFY,			// 修飾状態
	} modify_state;						// 修飾キー状態
	DWORD modify_start_tick;			// RELEASE から DOWN_OR_MODIFY になった時間
	HHOOK hLowLevelKeyboardProc;
} work_t;
static work_t work;

#if 0
#define dprintf(...)
#else
static void dprintf(const char *fmt, ...)
{
	char tmp[1024];
	va_list arg;
	va_start(arg, fmt);
	_vsnprintf_s(tmp, sizeof(tmp), _TRUNCATE, fmt, arg);
	va_end(arg);
	OutputDebugStringA(tmp);
}
#endif

static void AddTrayIcon(HWND hWnd)
{
	NOTIFYICONDATAW notify_icondata = {};
	NOTIFYICONDATAW *p = &notify_icondata;
	p->cbSize = sizeof(*p);
	p->hWnd = hWnd;
	p->uFlags = NIF_TIP | NIF_MESSAGE;
	p->uCallbackMessage = WM_APP_TRAY_CLICK;
	wcscpy_s(p->szTip, L"keyclick");

	Shell_NotifyIconW(NIM_ADD, p);
}

static void DeleteTrayIcon(HWND hWnd)
{
	NOTIFYICONDATAW notify_icondata = {};
	NOTIFYICONDATAW *p = &notify_icondata;
	p->cbSize = sizeof(*p);
	p->hWnd = hWnd;

	Shell_NotifyIconW(NIM_DELETE, p);
}

/**
 *	アイコンをセットする
 *	@param	icon_no		0	通常
 *						1	シフト or リリース(入力)待ち
 *						2	シフト状態
 */
static void SetIcon(HWND hWnd, int icon_no)
{
	WORD icon_id =
		icon_no == 0 ? IDI_MAIN_ICON :
		icon_no == 1 ? IDI_RED_ICON : IDI_GREEN_ICON;
	const HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
	int cx = GetSystemMetrics(SM_CXSMICON);
	int cy = GetSystemMetrics(SM_CYSMICON);
	HICON hIcon =
		(HICON)LoadImageW(hInst,
						  MAKEINTRESOURCEW(icon_id), IMAGE_ICON,
						  cx, cy, LR_DEFAULTCOLOR);

	NOTIFYICONDATAW notify_icondata = {};
	NOTIFYICONDATAW *p = &notify_icondata;
	p->cbSize = sizeof(*p);
	p->hWnd = hWnd;
	p->uFlags = NIF_ICON;
	p->hIcon = hIcon;

	Shell_NotifyIconW(NIM_MODIFY, p);
}

static void SetMouseInput(INPUT *input, DWORD dwFlags, DWORD time, DWORD mouseData)
{
	input->type = INPUT_MOUSE;
	input->mi.dx = 0;
	input->mi.dy = 0;
	input->mi.mouseData = mouseData;
	input->mi.dwFlags = dwFlags;
	input->mi.time = time;
	input->mi.dwExtraInfo = 0;
}

static void SetKeyInput(INPUT* input, WORD vkCode, DWORD dwFlags, DWORD time)
{
	switch (vkCode)
	{
	case VK_LWIN:
	case VK_CANCEL:
	case VK_PRIOR:
	case VK_NEXT:
	case VK_END:
	case VK_HOME:
	case VK_LEFT:
	case VK_UP:
	case VK_RIGHT:
	case VK_DOWN:
	case VK_SNAPSHOT:
	case VK_INSERT:
	case VK_DELETE:
	case VK_DIVIDE:
	case VK_NUMLOCK:
	case VK_RSHIFT:
	case VK_RCONTROL:
	case VK_RMENU:
		dwFlags |= KEYEVENTF_EXTENDEDKEY;
		break;
	}

	input->type = INPUT_KEYBOARD;
	input->ki.dwFlags = dwFlags;
	input->ki.wVk = vkCode;
	input->ki.wScan = (WORD)MapVirtualKey(vkCode, 0);
	input->ki.time = time;
	input->ki.dwExtraInfo = 0;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
	work_t *w = &work;
	if (code == HC_ACTION) {
		KBDLLHOOKSTRUCT *pkbd = (KBDLLHOOKSTRUCT *)lParam;

		switch (pkbd->vkCode) {
		case VK_NONCONVERT:		// 無変換キー
		//case VK_TAB:			// TABキー
		{
			// 修飾キー
			DWORD flags = pkbd->flags;
			if ((flags & LLKHF_INJECTED) == 0) {
				DWORD time = pkbd->time;
				switch (wParam) {
				case WM_KEYDOWN:
					// リピート時、 WM_KEYDOWN が連続して発生する
					if (w->modify_state == work_t::MODIFY_STATE_RELEASE) {
						w->modify_state = work_t::MODIFY_STATE_DOWN_OR_MODIFY;
						w->modify_start_tick = time;
						SetIcon(w->hWnd, 1);
						SetTimer(w->hWnd, 1, TIMEOUT_MS, NULL);
						dprintf("SetTimer()\n");
					}
					return TRUE;
				case WM_KEYUP: {
					w->modify_state = work_t::MODIFY_STATE_RELEASE;
					SetIcon(w->hWnd, 0);
					if (time - w->modify_start_tick < TIMEOUT_MS) {
						// 修飾キーが押されて離す、が 500ms 未満の場合、修飾キーを1度入力する
						const WORD key = (WORD)pkbd->vkCode;
						INPUT input[2] = {};
						SetKeyInput(&input[0], key, 0, time);
						SetKeyInput(&input[1], key, KEYEVENTF_KEYUP, time);
						SendInput(2, input, sizeof(input[0]));
					}
					return TRUE;
				}
				default:{
					dprintf("k 0x%02llx\n", wParam);
					break;
				}
				}
			}
			break;
		}
		case 'A':
		case 'S':
		case 'D':
		case 'F':
		case 'G':
		{
			int button_index =
				pkbd->vkCode == 'A' ? 0 :
				pkbd->vkCode == 'S' ? 1 :
				pkbd->vkCode == 'D' ? 2 :
				pkbd->vkCode == 'F' ? 3 : 4;
			static const DWORD button_downs[] = {
				MOUSEEVENTF_XDOWN,
				MOUSEEVENTF_LEFTDOWN,
				MOUSEEVENTF_RIGHTDOWN,
				MOUSEEVENTF_MIDDLEDOWN,
				MOUSEEVENTF_XDOWN,
			};
			static const DWORD button_ups[] = {
				MOUSEEVENTF_XUP,
				MOUSEEVENTF_LEFTUP,
				MOUSEEVENTF_RIGHTUP,
				MOUSEEVENTF_MIDDLEUP,
				MOUSEEVENTF_XUP,
			};
			static const DWORD mouse_datas[] = {
				XBUTTON1,
				0,
				0,
				0,
				XBUTTON2,
			};
			DWORD time = pkbd->time;
			switch (wParam) {
			case WM_KEYDOWN:
				if (w->modify_state != work_t::MODIFY_STATE_RELEASE) {
					if (w->mouse_button_pushed[button_index] == false) {
						INPUT input[1] = {};
						SetMouseInput(input, button_downs[button_index], time, mouse_datas[button_index]);
						SendInput(1, input, sizeof(INPUT));
						w->mouse_button_pushed[button_index] = true;

						if (w->modify_state == work_t::MODIFY_STATE_DOWN_OR_MODIFY) {
							w->modify_state = work_t::MODIFY_STATE_MODIFY;
							SetIcon(w->hWnd, 2);
						}
					}
					return TRUE;
				}
				break;
			case WM_KEYUP: {
				if (w->mouse_button_pushed[button_index] == true) {
					INPUT input[1] = {};
					SetMouseInput(input, button_ups[button_index], time, mouse_datas[button_index]);
					SendInput(1, input, sizeof(INPUT));
					w->mouse_button_pushed[button_index] = false;
					return TRUE;
				}
			}
			break;
			}
		}
		}
	}
	return CallNextHookEx(w->hLowLevelKeyboardProc, code, wParam, lParam);
}

static INT_PTR CALLBACK AboutDlg(HWND Dialog, UINT Message, WPARAM wParam, LPARAM)
{
	switch (Message) {
	case WM_INITDIALOG: {
		char buf[128];

		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "key click\nVersion 0.1");
		SetDlgItemTextA(Dialog, IDC_VERSION, buf);

		return TRUE;
	}

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			EndDialog(Dialog, 1);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static HMENU CreateAppPopupMenu()
{
	HMENU popup_menu = CreatePopupMenu();

	MENUITEMINFOW mi = {};
	mi.cbSize = sizeof(mi);
	mi.fMask = MIIM_TYPE | MIIM_ID;
	mi.fType = MFT_STRING;
	mi.dwTypeData = L"&Version";
	mi.wID = 1;
	InsertMenuItemW(popup_menu, 0, TRUE, &mi);
	mi.dwTypeData = L"&Quit";
	mi.wID = 2;
	InsertMenuItemW(popup_menu, 1, TRUE, &mi);

	return popup_menu;
}

/**
 *	BG Window proc
 */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static const UINT WM_TASKBER_CREATED = RegisterWindowMessage("TaskbarCreated");

	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_TIMER: {
		dprintf("WM_TIMER\n");
		work_t *w = &work;
		KillTimer(hWnd, 1);
		switch (w->modify_state) {
		case work_t::MODIFY_STATE_RELEASE:
			break;
		case work_t::MODIFY_STATE_DOWN_OR_MODIFY:
			w->modify_state = work_t::MODIFY_STATE_MODIFY;
			SetIcon(w->hWnd, 2);
			break;
		case work_t::MODIFY_STATE_MODIFY:
			break;
		}
	}

	case WM_APP_TRAY_CLICK: {
		switch (lParam) {
		case WM_RBUTTONDOWN:
		case WM_LBUTTONDOWN:
			POINT point;
			GetCursorPos(&point);
			HMENU menu = CreateAppPopupMenu();
			SetForegroundWindow(hWnd);
			UINT id = TrackPopupMenu(menu,
									 TPM_RETURNCMD,
									 point.x, point.y,
									 0, hWnd, NULL);
			DestroyMenu(menu);
			switch (id) {
			case 1: {
				const HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
				DialogBoxA(hInst, MAKEINTRESOURCEA(IDD_ABOUTBOX), NULL, AboutDlg);
				break;
			}
			case 2:
				DestroyWindow(hWnd);
				break;
			}
			break;
		}
		break;
	}

	default: {
		if (msg == WM_TASKBER_CREATED) {
			// タスクバーが再起動した
			DeleteTrayIcon(hWnd);
			AddTrayIcon(hWnd);
			SetIcon(hWnd, 0);
		}
	}
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(void)nCmdShow;
	(void)lpCmdLine;
	(void)hPrevInstance;
	work_t *w = &work;

	HANDLE mutex = CreateMutexW(NULL, FALSE, L"key_click_shift_mutex");
	if (mutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
		// 2重起動防止
		ExitProcess(1);
	}

	SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

	w->hLowLevelKeyboardProc = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);
	if (w->hLowLevelKeyboardProc == NULL) {
		//ERROR
		ExitProcess(1);
	}

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = (WNDPROC)WndProc;
	wc.hInstance = hInst;
	wc.lpszClassName = L"Key_Click";

	HWND hWnd = NULL;
	if (RegisterClassExW(&wc)) {
		hWnd = CreateWindowExW(0, wc.lpszClassName, NULL, 0, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
	}
	if (hWnd == NULL) {
		//ERROR
		ExitProcess(1);
	}
	w->hWnd = hWnd;

	// アイコンをセットする
	AddTrayIcon(hWnd);
	SetIcon(hWnd, 0);

	BOOL ret;
	MSG msg;
	while ((ret = GetMessageW(&msg, NULL, 0, 0))) {
		if (ret == -1) {
			msg.wParam = 0;
			break;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	DeleteTrayIcon(hWnd);

	if (w->hLowLevelKeyboardProc != NULL) {
		ret = UnhookWindowsHookEx(w->hLowLevelKeyboardProc);
		if (ret == 0) {
			//ERROR
		}
	}

	return (int)msg.wParam;
}
