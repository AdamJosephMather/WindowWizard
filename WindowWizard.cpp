#include <string>
#include <windows.h>
#include <iostream>
#include <dwmapi.h>
#include <unordered_map>
#include <dbt.h>
#include <initguid.h>
#include <algorithm>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Gdi32.lib")

#define VK_CUSTOM_153 0x99

static const GUID GUID_DEVINTERFACE_MONITOR = { 
	0xe6f07b5f, 0xee97, 0x4a90, { 0xb0, 0x76, 0x33, 0xf5, 0x7b, 0xf4, 0xea, 0xa7 } 
};

std::vector<char> numbers_in_char = {'1','2','3','4','5','6','7','8','9','0'};

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

struct MonitorInfo {
	int x;
	int y;
	int w;
	int h;
};

struct MonitorData {
	std::vector<MonitorInfo> foundMonitors = {};
};

struct WindowInfo {
	int monitor;
	int x;
	int y;
	int w;
	int h;
};

struct AreaInfo {
	double idealArea;
	HWND correlatesTo;
};

struct Box {
	double x;
	double y;
	double w;
	double h;
	HWND hwnd;
};

std::unordered_map<HWND,WindowInfo> openWindows;
std::unordered_map<int,MonitorInfo> monitors = {};
int MONITOR_ID = 0;
const int BORDER = 12;
const int HALF_BORDER = BORDER/2;
HWND focused = NULL;
HHOOK hhkLowLevelKybd;
bool still_setting_up = false;
HWND g_overlay = NULL;
std::vector<int> idx_to_monitor_id = {};

void DrawFocusedBorder(HWND hOverlay) {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hOverlay, &ps);
	
	RECT clientRect;
	GetClientRect(hOverlay, &clientRect);
	
	HBRUSH hClearBrush = CreateSolidBrush(RGB(0, 0, 0));
	FillRect(hdc, &clientRect, hClearBrush);
	DeleteObject(hClearBrush);
	
	if (!IsWindow(focused) || !IsWindowVisible(focused)) {
		EndPaint(hOverlay, &ps);
		return;
	}
	
	RECT rc;
	HRESULT hr = DwmGetWindowAttribute(
		focused,
		DWMWA_EXTENDED_FRAME_BOUNDS,
		&rc,
		sizeof(RECT)
	);
	
	int thickness = 5;
	
	if (SUCCEEDED(hr)) {
		SetWindowPos(
			hOverlay, 
			HWND_TOPMOST, 
			rc.left-thickness, rc.top-thickness, 
			rc.right - rc.left + thickness*2, rc.bottom - rc.top + thickness*2, 
			SWP_NOACTIVATE
		);
	}
	
	HBRUSH hBorderBrush = CreateSolidBrush(RGB(52, 155, 235)); // Red border
	
	RECT rTop    = { 0, 0, clientRect.right, thickness };
	RECT rBottom = { 0, clientRect.bottom - thickness, clientRect.right, clientRect.bottom };
	RECT rLeft   = { 0, 0, thickness, clientRect.bottom };
	RECT rRight  = { clientRect.right - thickness, 0, clientRect.right, clientRect.bottom };
	
	FillRect(hdc, &rTop, hBorderBrush);
	FillRect(hdc, &rBottom, hBorderBrush);
	FillRect(hdc, &rLeft, hBorderBrush);
	FillRect(hdc, &rRight, hBorderBrush);

	DeleteObject(hBorderBrush);
	
	EndPaint(hOverlay, &ps);
}

LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT: {
			DrawFocusedBorder(hwnd);
			return 0;
		}
		case WM_ERASEBKGND:
			return 1;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}

void repaint() {
	if (g_overlay) {
		InvalidateRect(g_overlay, NULL, TRUE);
		UpdateWindow(g_overlay);
	}
}

void focus(HWND hwnd) {
	if (hwnd == NULL) return;

	char title[256];
	GetWindowTextA(hwnd, title, sizeof(title));
	std::cout << "Focus on: " << title << "\n";

	DWORD currentThread    = GetCurrentThreadId();
	DWORD targetThread     = GetWindowThreadProcessId(hwnd, NULL);
	HWND  fgWnd            = GetForegroundWindow();
	DWORD foregroundThread = GetWindowThreadProcessId(fgWnd, NULL);

	// Bridge: current → foreground owner → target
	if (foregroundThread != currentThread)
		AttachThreadInput(currentThread, foregroundThread, TRUE);
	if (targetThread != foregroundThread)
		AttachThreadInput(foregroundThread, targetThread, TRUE);

	SetForegroundWindow(hwnd);
	BringWindowToTop(hwnd);

	if (targetThread != foregroundThread)
		AttachThreadInput(foregroundThread, targetThread, FALSE);
	if (foregroundThread != currentThread)
		AttachThreadInput(currentThread, foregroundThread, FALSE);
}

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
	
	if (proc.find("StartMenuExperienceHost.exe") != std::string::npos) return true;
	if (proc.find("SearchHost.exe") != std::string::npos) return true;
	if (proc.find("ShellExperienceHost.exe") != std::string::npos) return true;
	
	// Optional class-based exclusions once you log them:
	// if (cls == "Windows.UI.Core.CoreWindow") return true;
	// if (cls == "XamlExplorerHostIslandWindow") return true;
	
	if (proc.find("LockApp.exe") != std::string::npos) return true;
	if (proc.find("LogonUI.exe") != std::string::npos) return true;
	
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

double worst(std::vector<double> row, double w) {
	if (row.empty()) {
		return DBL_MAX;
	}
	double s = 0;
	double mx = row[0];
	double mn = row[0];
	
	for (auto r : row) {
		s += r;
		mx = max(r, mx);
		mn = min(r, mn);
	}
	
	return max((w*w*mx) / (s*s), (s*s) / (w*w*mn));
}

std::vector<Box> layout_row(std::vector<AreaInfo> row, double x, double y, double dx, double dy) {
	std::vector<Box> boxes = {};
	if (row.empty()) {
		return boxes;
	}
	double s = 0;
	for (auto ai : row) {
		s += ai.idealArea;
	}
	if (dx >= dy) {
		double col_w = s/dy;
		double cy = y;
		for (auto r : row) {
			Box b = {x, cy, col_w, r.idealArea/col_w, r.correlatesTo};
			boxes.push_back(b);
			cy += b.h;
		}
	}else {
		double row_h = s/dx;
		double cx = x;
		for (auto r : row) {
			Box b = {cx, y, r.idealArea/row_h, row_h, r.correlatesTo};
			boxes.push_back(b);
			cx += b.w;
		}
	}
	return boxes;
}

Box remaining_rect(std::vector<AreaInfo> row, double x, double y, double dx, double dy) {
	double s = 0;
	for (auto ai : row) {
		s += ai.idealArea;
	}
	if (dx >= dy) {
		double col_w = s/dy;
		return {x+col_w, y, dx-col_w, dy};
	}else{
		double row_h = s/dx;
		return {x, y+row_h, dx, dy-row_h};
	}
}

std::vector<Box> squarify(std::vector<AreaInfo> items, std::vector<AreaInfo> row, double x, double y, double dx, double dy) {
	if (items.empty()) {
		return layout_row(row, x, y, dx, dy);
	}
	
	double w = min(dx, dy);
	std::vector<double> row_values;
	for (auto v : row) {
		row_values.push_back(v.idealArea);
	}
	
	std::vector<double> new_row_valsk = row_values;
	new_row_valsk.push_back(items[0].idealArea);
	
	if (row.empty() || worst(row_values, w) >= worst(new_row_valsk, w)) {
		std::vector<AreaInfo> itemssubist(items.begin() + 1, items.end());
		std::vector<AreaInfo> new_row = row;
		new_row.push_back(items[0]);
		
		return squarify(itemssubist, new_row, x, y, dx, dy);
	}else{
		std::vector<Box> boxes = layout_row(row, x, y, dx, dy);
		Box nLoc = remaining_rect(row, x, y, dx, dy);
		std::vector<Box> newboxes = squarify(items, {}, nLoc.x, nLoc.y, nLoc.w, nLoc.h);
		boxes.insert(boxes.end(), newboxes.begin(), newboxes.end());
		return boxes;
	}
}

void ApplyManagedWindowRect(HWND hwnd, int x, int y, int w, int h) {
	if (!IsWindow(hwnd)) return;

	WINDOWPLACEMENT wp = {};
	wp.length = sizeof(wp);

	if (GetWindowPlacement(hwnd, &wp)) {
		wp.rcNormalPosition = { x, y, x + w, y + h };

		// Keep current show state unless minimized, where we want future restore
		if (wp.showCmd == SW_SHOWMINIMIZED) {
			wp.showCmd = SW_SHOWNORMAL;
		}

		SetWindowPlacement(hwnd, &wp);
	}

	SetWindowPos(
		hwnd,
		NULL,
		x, y, w, h,
		SWP_NOZORDER | SWP_NOACTIVATE
	);
}

void recalc(int mon, HWND change = NULL, double amount = 1) {
	if (still_setting_up) {
		return;
	}
	
	// this recalculates positioning. Very important.
	std::cout << "Recalc Monitor: " << mon << std::endl;
	
	auto it = monitors.find(mon);
	if (it == monitors.end()) {
		std::cout << "Something went very wrong, can't find monitor for recalc.\n";
	}
	
	// first we need ideal areas for each window we'll get that through callbacks because if a user resized a window, I want to keep that in mind
	
	std::vector<AreaInfo> areas = {};
	double totalArea = 0;
	
	for (const std::pair<HWND,WindowInfo> pr : openWindows) {
		if (pr.second.monitor == mon) {
			RECT rect;
			double idealArea = 500*400; // good enough if we fail to get the rect?
			if (GetWindowRect(pr.first, &rect)) {
				int width = rect.right - rect.left;
				int height = rect.bottom - rect.top;
				idealArea = width*height;
			}
			
			if (pr.first == change) {
				idealArea *= amount;
			}
			
			areas.push_back({idealArea, pr.first});
			totalArea += idealArea;
		}
	}
	
	if (areas.empty()) {
		repaint();
		return;
	}
	
	double allowedArea = it->second.w*it->second.h;
	for (AreaInfo& a : areas) {
		a.idealArea = (a.idealArea/totalArea)*allowedArea;
	}
	
	std::sort(areas.begin(), areas.end(), [](const AreaInfo& a, const AreaInfo& b){
		return a.idealArea > b.idealArea;
	});
	
	auto boxes_raw = squarify(areas, {}, 0, 0, it->second.w, it->second.h);
	
	for (auto b : boxes_raw) {
		int w;
		int h;
		
		if (b.x == 0) {
			b.x += BORDER;
			b.w -= BORDER;
		}else{
			b.x += HALF_BORDER;
			b.w -= HALF_BORDER;
		}
		if (b.y == 0) {
			b.y += BORDER;
			b.h -= BORDER;
		}else{
			b.y += HALF_BORDER;
			b.h -= HALF_BORDER;
		}
		
		if (b.x+b.w == it->second.w) {
			w = b.w-BORDER;
		}else{
			w = b.w-HALF_BORDER;
		}
		if (b.y+b.h == it->second.h) {
			h = b.h-BORDER;
		}else{
			h = b.h-HALF_BORDER;
		}
		
		auto itWin = openWindows.find(b.hwnd);
		if (itWin != openWindows.end()) {
			// we need to determine the offset required to turn GetWindowRect -> DWMWA_EXTENDED_FRAME_BOUNDS
			
			RECT believed;
			GetWindowRect(b.hwnd, &believed);
			
			RECT trueRect;
			HRESULT hr = DwmGetWindowAttribute(
				b.hwnd,
				DWMWA_EXTENDED_FRAME_BOUNDS,
				&trueRect,
				sizeof(RECT)
			);
			
			if (!hr) {
				trueRect = believed;
			}
			
			itWin->second.x = b.x + it->second.x - (trueRect.left-believed.left);
			itWin->second.y = b.y + it->second.y - (trueRect.top-believed.top);
			itWin->second.w = w - ((trueRect.right-trueRect.left)-(believed.right-believed.left));
			itWin->second.h = h - ((trueRect.bottom-trueRect.top)-(believed.bottom-believed.top));
			
			ApplyManagedWindowRect(
				b.hwnd,
				itWin->second.x,
				itWin->second.y,
				itWin->second.w,
				itWin->second.h
			);
		}
	}
	
	repaint(); // probably a good idea
}

void addToTracked(HWND hwnd) {
	if (!monitors.empty()) {
		int mon   = monitors.begin()->first;
		
		WindowInfo info = {
			mon,
			0,
			0,
			600,
			600
		};
		openWindows.insert({hwnd, info});
		
		char title[256];
		GetWindowTextA(hwnd, title, sizeof(title));
		std::cout << "Tracking Start: " << title << std::endl;
		recalc(mon);
	}
}

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
			addToTracked(hwnd);
			if (!still_setting_up) {
				focus(hwnd);
				focused = hwnd;
				repaint();
			}
		}
	}
	
	auto it = openWindows.find(hwnd);
	bool tracked = it != openWindows.end();
		
	if ((action == WW_Close || action == WW_Hide) && tracked) {
		auto monIt = monitors.find(it->second.monitor);
		bool needrecalc = false;
		int rclcMon = 0;
		int rclcGrp = 0;
		
		if (monIt != monitors.end()) {
			needrecalc = true;
			rclcMon = it->second.monitor;
		}
		
		openWindows.erase(hwnd);
		std::cout << "Closed: " << title << std::endl;
		
		if (needrecalc) {
			recalc(rclcMon);
		}
	}else if (action == WW_MinEnd) {
		std::cout << "Minimized end: " << title << std::endl;
		
		if (!tracked) {
			std::cout << "  Tracking: " << title << std::endl;
			
			ShowWindow(hwnd, SW_RESTORE);
			addToTracked(hwnd);
			if (!still_setting_up) {
				focus(hwnd);
				focused = hwnd;
				repaint();
			}
		}else{
			std::cout << "  Already tracking: " << title << std::endl;
			
			auto itMon = monitors.find(it->second.monitor);
			if (itMon == monitors.end()) {
				openWindows.erase(hwnd); // this window isn't on a monitor that we know of
			}
		}
	}else if (action == WW_MinStart && tracked) {
		auto monIt = monitors.find(it->second.monitor);
		
		openWindows.erase(hwnd);
		std::cout << "Tracking Stopped: " << title << std::endl;
		
		if (hwnd == focused) {
			focused = NULL;
			repaint();
		}
		
		if (monIt != monitors.end()) {
			recalc(monIt->first);
		}
	}else if (action == WW_MoveSizeStart && tracked) {
	}else if (action == WW_MoveSizeEnd && tracked) {
		// we will stop tracking if this window is no longer where we put it. And we will then recalc
		
		RECT rect;
		if (GetWindowRect(hwnd, &rect)) {
			int width = rect.right - rect.left;
			int height = rect.bottom - rect.top;
			
			if (rect.left != it->second.x || rect.top != it->second.y || height != it->second.h || width != it->second.w) {
				int mon = it->second.monitor;
				
				openWindows.erase(hwnd);
				std::cout << "Tracking Stopped: " << title << std::endl;
				recalc(mon);
			}
		}
	}else if (action == WW_Focus) {
		if (openWindows.count(hwnd)) {
			focused = hwnd;
		}else{
			focused = NULL;
		}
		
		repaint();
		std::cout << "Focus change to " << title << "\n";
	}
	
	if (action == WW_MoveSizeEnd && hwnd == focused) {
		if (!openWindows.count(focused)) {
			focused = NULL;
		}
		repaint();
	}
}

bool monitorMatches(MonitorInfo a, MonitorInfo b) {
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	auto* data = reinterpret_cast<MonitorData*>(dwData);
	
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	if (GetMonitorInfo(hMonitor, &monitorInfo)) {
		MonitorInfo mntr = {
			monitorInfo.rcWork.left,
			monitorInfo.rcWork.top,
			monitorInfo.rcWork.right-monitorInfo.rcWork.left,
			monitorInfo.rcWork.bottom-monitorInfo.rcWork.top,
		};
		
		if (mntr.w == 0 || mntr.h == 0) {
			return TRUE;
		}
		
		data->foundMonitors.push_back(mntr);
	}
	return TRUE;
}

void UpdateMonitorState() {
	MonitorData data;
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&data));
	
	// now determine which monitors have changed, and minimize all windows associated with old monitors that no longer exist
	
	std::cout << "Update monitor state\n";
	
	std::vector<int> toremove = {};
	for (const std::pair<int,MonitorInfo> pr : monitors) {
		int oldId = pr.first;
		MonitorInfo oldMon = pr.second;
		
		bool foundit = false;
		for (int i = data.foundMonitors.size()-1; i >= 0; i--) {
			MonitorInfo newMon = data.foundMonitors[i];
			if (monitorMatches(newMon, oldMon)) {
				foundit = true;
				break;
			}
		}
		
		if (!foundit) {
			toremove.push_back(oldId);
			std::cout << "Must destroy: " << oldId << " gotcha?\n";
		}else{
			std::cout << "Keeping: " << oldId << " mkay?\n";
		}
	}
	
	for (int id : toremove) {
		// time to remove all windows from that monitor, and then minimize them
		for (auto it = openWindows.begin(); it != openWindows.end(); ) {
			char title[256];
			GetWindowTextA(it->first, title, sizeof(title));
			std::cout << "Removing window: " << title << " because it's monitor was changed.\n";
			
			if (it->second.monitor == id) {
				HWND hTarget = it->first;
				it = openWindows.erase(it); // important to erase first, don't want to have our other logic looking at it
				ShowWindow(hTarget, SW_MINIMIZE);
			}else {
				++it; // only increment if we didn't erase
			}
		}
		
		std::cout << "Removing monitor: " << id << "\n";
		monitors.erase(id);
		for (int i = idx_to_monitor_id.size()-1; i >= 0; i--) {
			if (idx_to_monitor_id[i] == id) {
				idx_to_monitor_id.erase(idx_to_monitor_id.begin()+i);
				break;
			}
		}
	}
	
	// now we go through our new windows and only add them to the map if they are truly new
	for (int i = data.foundMonitors.size()-1; i >= 0; i--) {
		MonitorInfo newMon = data.foundMonitors[i];
		
		bool foundit = false;
		for (const std::pair<int,MonitorInfo> pr : monitors) {
			int oldId = pr.first;
			MonitorInfo oldMon = pr.second;
			if (monitorMatches(newMon, oldMon)) {
				foundit = true;
				break;
			}
		}
		
		if (!foundit) {
			MONITOR_ID += 1;
			std::cout << "Inserting monitor " << MONITOR_ID << " because it is new\n";
			monitors.insert({MONITOR_ID, newMon});
			idx_to_monitor_id.push_back(MONITOR_ID);
		}
	}
}

BOOL CALLBACK EnumExistingWindows(HWND hwnd, LPARAM lParam) {
	if (IsIconic(hwnd)) {
		return TRUE;
	}
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
	}else if (event == EVENT_OBJECT_FOCUS || event == EVENT_SYSTEM_FOREGROUND) {
		WindowCallback(hwnd, WW_Focus);
	}
}

LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_DISPLAYCHANGE:
			std::cout << "[Event] Display resolution/topology changed!" << std::endl;
			UpdateMonitorState();
			break;
		case WM_DEVICECHANGE:
			if (wParam == DBT_DEVICEARRIVAL) {
				std::cout << "[Event] Monitor plugged in." << std::endl;
				UpdateMonitorState();
			} else if (wParam == DBT_DEVICEREMOVECOMPLETE) {
				std::cout << "[Event] Monitor unplugged." << std::endl;
				UpdateMonitorState();
			}
			
			break;
			
		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	return 0;
}

HWND CreateOverlayWindow() {
	WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
	wc.lpfnWndProc = OverlayWindowProc; // Ensure this proc handles WM_PAINT
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = TEXT("MonitorOverlayClass");

	if (!RegisterClassEx(&wc)) return NULL;

	// Get the bounds of the entire virtual screen (all monitors combined)
	int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
	int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
	int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	// Create the window
	HWND hwnd = CreateWindowEx(
		WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
		TEXT("MonitorOverlayClass"),
		TEXT("MonitorOverlayWindow"),
		WS_POPUP | WS_VISIBLE,
		x, y, width, height,
		NULL, NULL, NULL, NULL
	);

	// Set the window to be transparent (e.g., make black fully transparent)
	// You can adjust this to make the window fully invisible except for your drawings
	SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

	return hwnd;
}

HDEVNOTIFY hDevNotify = NULL;

void RegisterMonitorNotifications(HWND hwnd) {
	DEV_BROADCAST_DEVICEINTERFACE notificationFilter = { 0 };
	notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_MONITOR;

	hDevNotify = RegisterDeviceNotification(
		hwnd, 
		&notificationFilter, 
		DEVICE_NOTIFY_WINDOW_HANDLE
	);
}

void changeSize(double amount) {
	auto it = openWindows.find(focused);
	if (it == openWindows.end()) {
		return;
	}
	
	recalc(it->second.monitor, focused, amount);
}

std::pair<int,int> getDesired(RECT rect, int dx, int dy) {
	int desired_x;
	if (dx == -1) {
		desired_x = rect.left;
	}else if (dx == 1){
		desired_x = rect.right;
	}else{
		desired_x = (rect.right+rect.left)/2;
	}
	
	int desired_y;
	if (dy == -1) {
		desired_y = rect.top;
	}else if (dy == 1){
		desired_y = rect.bottom;
	}else{
		desired_y = (rect.bottom+rect.top)/2;
	}
	
	return {desired_x, desired_y};
}

HWND findWindow(int dx, int dy) {
	if (focused == NULL) {
		for (std::pair<HWND, WindowInfo> pr : openWindows) {
			auto it = monitors.find(pr.second.monitor);
			if (it != monitors.end()) {
				return pr.first;
			}
		}
		return NULL;
	}
	
	RECT rect;
	if (!GetWindowRect(focused, &rect)) {
		return NULL;
	}
	
	std::pair<int,int> des_x_des_y = getDesired(rect, dx, dy);
	
	// now find window that is closest
	
	HWND closest = NULL;
	long long maxDst;
	
	for (std::pair<HWND, WindowInfo> pr : openWindows) {
		if (pr.first == focused) continue;
		
		auto it = monitors.find(pr.second.monitor);
		if (it == monitors.end()) continue;
		
		RECT rect;
		if (!GetWindowRect(pr.first, &rect)) continue;
		
		std::pair<int,int> clst_x_clst_y = getDesired(rect, -dx, -dy);
		
		long long dist = pow((long long)(clst_x_clst_y.first - des_x_des_y.first), 2) + pow((long long)(clst_x_clst_y.second - des_x_des_y.second), 2);
		
		if (closest == NULL || dist < maxDst) {
			maxDst = dist;
			closest = pr.first;
		}
	}
	
	return closest;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
			
			bool vk_down = GetAsyncKeyState(VK_CUSTOM_153) & 0x8000;
			bool isShiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
			
			if (vk_down) {
				KBDLLHOOKSTRUCT *pKbdStruct = (KBDLLHOOKSTRUCT *)lParam;
				if (pKbdStruct->vkCode == 'H') {
					std::cout << "H pressed" << std::endl;
					focus(findWindow(-1, 0));
				}else if (pKbdStruct->vkCode == 'J') {
					std::cout << "J pressed" << std::endl;
					focus(findWindow(0, 1));
				}else if (pKbdStruct->vkCode == 'K') {
					std::cout << "K pressed" << std::endl;
					focus(findWindow(0, -1));
				}else if (pKbdStruct->vkCode == 'L') {
					std::cout << "L pressed" << std::endl;
					focus(findWindow(1, 0));
				}else if (pKbdStruct->vkCode == 'I') {
					std::cout << "I pressed" << std::endl;
					if (isShiftPressed) {
						changeSize(1.3);
					}else{
						changeSize(1.1);
					}
				}else if (pKbdStruct->vkCode == 'M') {
					std::cout << "M pressed" << std::endl;
					if (isShiftPressed) {
						changeSize(1/1.3);
					}else{
						changeSize(1/1.1);
					}
				}else if (pKbdStruct->vkCode == 'W') {
					std::cout << "W pressed" << std::endl;
					ShowWindow(focused, SW_MINIMIZE);
				}else if (pKbdStruct->vkCode == 'Q') {
					std::cout << "W pressed" << std::endl;
					PostMessage(focused, WM_CLOSE, 0, 0);
				}
				
				auto it = openWindows.find(focused);
				if (it != openWindows.end()) {
					auto itMon = monitors.find(it->second.monitor);
					if (itMon != monitors.end()) {
						for (int i = 0; i < 10; i++) {
							if (pKbdStruct->vkCode != numbers_in_char[i] || i >= idx_to_monitor_id.size()) continue;
							
							int oldMon = it->second.monitor;
							
							int newMon = idx_to_monitor_id[i];
							auto itNewMon = monitors.find(newMon);
							if (itNewMon == monitors.end()) {
								continue;
							}
							
							it->second.monitor = newMon;
							
							recalc(oldMon);
							recalc(newMon);
							
							break;
						}
					}
				}
				
				return 1;
			}
		}
	}
	
	return CallNextHookEx(hhkLowLevelKybd, nCode, wParam, lParam);
}

HWND CreateMessageWindow() {
	WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
	wc.lpfnWndProc = MessageWindowProc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = TEXT("WWMessageWindowClass");

	RegisterClassEx(&wc);

	return CreateWindowEx(
		0,
		TEXT("WWMessageWindowClass"),
		TEXT("WWMessageWindow"),
		0,
		0, 0, 0, 0,
		NULL, NULL, wc.hInstance, NULL
	);
}

int main() {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	
	still_setting_up = true;
	
	HWND hMsgWnd = CreateMessageWindow();
	if (!hMsgWnd) {
		std::cerr << "Failed to create message window!" << std::endl;
		return 1;
	}
	
	g_overlay = CreateOverlayWindow();
	if (!g_overlay) {
		std::cerr << "Failed to create message window!" << std::endl;
		return 1;
	}
	
	RegisterMonitorNotifications(hMsgWnd);
	
	std::cout << "\n--- Enumerating Monitors ---" << std::endl;
	
	UpdateMonitorState();
	
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
	
	std::cout << "--- Creating Keyboard Hook On VK 153 ---" << std::endl;
	
	hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);

	if (hhkLowLevelKybd == NULL) {
		std::cerr << "Failed to install hook!" << std::endl;
		return 1;
	}
	
	still_setting_up = false;
	
	for (const std::pair<int, MonitorInfo>& pr : monitors) {
		recalc(pr.first);
	}
	
	std::cout << "\n--- Monitoring Live Events (Press Ctrl+C to stop) ---" << std::endl;
	
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	
	UnhookWinEvent(hook);
	UnhookWindowsHookEx(hhkLowLevelKybd);
	if (hDevNotify) UnregisterDeviceNotification(hDevNotify);
	
	return 0;
}