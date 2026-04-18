#include <windows.h>
#include <iostream>
//#include <string>
#include <dwmapi.h>
#include <unordered_map>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "dwmapi.lib")

enum WWActions {
	WW_Open,
	WW_Close,
	WW_Focus,
	WW_Hide,
	WW_Show,
	WW_MinEnd, // about to be restored
	WW_MinStart, // about to be minimized
	WW_MoveSizeEnd, // window has finished resizing
	WW_MoveSizeStart // window is being resized
};

struct WindowInfo {
	bool minimized = false;
};

std::unordered_map<HWND,WindowInfo> openWindows;

bool HasExStyle(HWND hwnd, LONG_PTR flag) {
	return (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & flag) != 0;
}

std::string GetWindowClass(HWND hwnd) {
	char cls[256] = {};
	GetClassNameA(hwnd, cls, sizeof(cls));
	return cls;
}

std::string GetProcessPathFromHwnd(HWND hwnd) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (!pid) return "";

	HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!hProc) return "";

	char path[MAX_PATH * 4] = {};
	DWORD size = (DWORD)sizeof(path);
	std::string out;

	if (QueryFullProcessImageNameA(hProc, 0, path, &size)) {
		out = path;
	}

	CloseHandle(hProc);
	return out;
}

bool IsShellSurfaceJunk(HWND hwnd) {
	if (HasExStyle(hwnd, WS_EX_NOACTIVATE)) {
		return true;
	}

	std::string cls = GetWindowClass(hwnd);
	std::string proc = GetProcessPathFromHwnd(hwnd);

	// Fill these in from your logs on your machine.
	// Process names are usually more stable than titles.
	if (proc.find("StartMenuExperienceHost.exe") != std::string::npos) return true;
	if (proc.find("SearchHost.exe") != std::string::npos) return true;
	if (proc.find("ShellExperienceHost.exe") != std::string::npos) return true;

	// Optional class-based exclusions once you log them:
	// if (cls == "Windows.UI.Core.CoreWindow") return true;
	// if (cls == "XamlExplorerHostIslandWindow") return true;

	return false;
}

bool IsAltTabLikeWindow(HWND hwnd) {
	LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

	if (ex & WS_EX_TOOLWINDOW) return false;

	int cloaked = 0;
	if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
		return false;

	// Classic Alt+Tab representative test
	HWND walk = GetAncestor(hwnd, GA_ROOTOWNER);
	while (true) {
		HWND tryHwnd = GetLastActivePopup(walk);
		if (tryHwnd == walk) break;
		if (IsWindowVisible(tryHwnd)) {
			walk = tryHwnd;
			break;
		}
		walk = tryHwnd;
	}
	if (walk != hwnd) return false;
	
	if (IsShellSurfaceJunk(hwnd)) return false;
	
	return true;
}

//bool IsCurrentlyResizable(HWND hwnd) {
//	return (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0;
//}

void WindowCallback(HWND hwnd, WWActions action) {
	char title[256];
	GetWindowTextA(hwnd, title, sizeof(title));
	
	if (strlen(title) == 0) return;
	
	if (!IsAltTabLikeWindow(hwnd)) return;
	
	bool requiredForOpen = IsWindowVisible(hwnd);
	
	if (action == WW_Open || action == WW_Show) {
		if (!requiredForOpen) return;
		
		auto it = openWindows.find(hwnd);
		if (it == openWindows.end()) {
			WindowInfo info = {false};
			openWindows.insert({hwnd, info});
			std::cout << "Opened: " << title << std::endl;
		}
	}
	
	auto it = openWindows.find(hwnd);
	if (it == openWindows.end()) {
		return;
	}
	
	if (action == WW_Close || action == WW_Hide) {
		openWindows.erase(hwnd);
		std::cout << "Closed: " << title << std::endl;
	}else if (action == WW_MinEnd) {
		it->second.minimized = false;
		std::cout << "Restrd: " << title << std::endl;
	}else if (action == WW_MinStart) {
		it->second.minimized = true;
		std::cout << "Minmzd: " << title << std::endl;
	}else if (action == WW_MoveSizeStart) {
//		it->second.minimized = true;
		std::cout << "Moving: " << title << std::endl;
	}else if (action == WW_MoveSizeEnd) {
//		it->second.minimized = true;
		std::cout << "Moved : " << title << std::endl;
	}
}

BOOL CALLBACK EnumExistingWindows(HWND hwnd, LPARAM lParam) {
	WindowCallback(hwnd, WW_Open);
	return TRUE;
}

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
	if (hwnd == NULL || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

	if (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_UNCLOAKED) {
		WindowCallback(hwnd, WW_Open);
	}else if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_CLOAKED) {
		WindowCallback(hwnd, WW_Close);
	}else if (event == EVENT_OBJECT_HIDE) {
		WindowCallback(hwnd, WW_Hide);
	}else if (event == EVENT_OBJECT_SHOW) {
		WindowCallback(hwnd, WW_Show);
	}else if (event == EVENT_SYSTEM_MINIMIZEEND) {
		WindowCallback(hwnd, WW_MinEnd);
	}else if (event == EVENT_SYSTEM_MINIMIZESTART) {
		WindowCallback(hwnd, WW_MinStart);
	}else if (event == EVENT_SYSTEM_MOVESIZEEND) {
		WindowCallback(hwnd, WW_MoveSizeEnd);
	}else if (event == EVENT_SYSTEM_MOVESIZESTART) {
		WindowCallback(hwnd, WW_MoveSizeStart);
	}else if (event == EVENT_OBJECT_FOCUS) {
		WindowCallback(hwnd, WW_Focus);
	}
}

int main() {
	std::cout << "--- Enumerating Existing Windows ---" << std::endl;
	EnumWindows(EnumExistingWindows, 0);

	HWINEVENTHOOK hook = SetWinEventHook(
		EVENT_MIN, EVENT_MAX, 
		NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT
	);

	if (!hook) {
		std::cerr << "Failed to install hook!" << std::endl;
		return 1;
	}

	std::cout << "\n--- Monitoring Live Events (Press Ctrl+C to stop) ---" << std::endl;

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	UnhookWinEvent(hook);
	return 0;
}