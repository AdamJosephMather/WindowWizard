package main

import (
	"fmt"
	"slices"
	"syscall"
	"time"
	"unicode/utf16"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	user32                  = windows.NewLazySystemDLL("user32.dll")
	
	procSetWinEventHook     = user32.NewProc("SetWinEventHook")
	procGetWindowTextW      = user32.NewProc("GetWindowTextW")
	procIsWindowVisible     = user32.NewProc("IsWindowVisible")
	procGetAncestor         = user32.NewProc("GetAncestor")
	procGetMessageW         = user32.NewProc("GetMessageW")
	procSendMessage         = user32.NewProc("SendMessageW")
	
	procTranslateMessage    = user32.NewProc("TranslateMessage")
	procDispatchMessageW    = user32.NewProc("DispatchMessageW")
	
	procEnumWindows         = user32.NewProc("EnumWindows")
	
	procGetWindowPlacement  = user32.NewProc("GetWindowPlacement")
	procSetWindowPos        = user32.NewProc("SetWindowPos")
	
	procEnumDisplayMonitors = user32.NewProc("EnumDisplayMonitors")
	procGetMonitorInfoW     = user32.NewProc("GetMonitorInfoW")
	
	shell32 				= windows.NewLazySystemDLL("shell32.dll")
	procSHAppBarMessage 	= shell32.NewProc("SHAppBarMessage")
	
	procFindWindowW = user32.NewProc("FindWindowW")
	procGetWindowRect = user32.NewProc("GetWindowRect")
	procGetClientRect = user32.NewProc("GetClientRect")
	
	procShowWindow = user32.NewProc("ShowWindow")
	
	procGetWindowLongW  = user32.NewProc("GetWindowLongW")
	procGetClassNameW   = user32.NewProc("GetClassNameW")
	
	procGetWindowLongPtrW = user32.NewProc("GetWindowLongPtrW")
	
	procSetWindowsHookEx    = user32.NewProc("SetWindowsHookExW")
	procUnhookWindowsHookEx = user32.NewProc("UnhookWindowsHookEx")
	procCallNextHookEx      = user32.NewProc("CallNextHookEx")
	procGetAsyncKeyState    = user32.NewProc("GetAsyncKeyState")
	
	procIsIconic = user32.NewProc("IsIconic")
	
	dwmapi  = windows.NewLazySystemDLL("dwmapi.dll")
	procDwmGetWindowAttribute = dwmapi.NewProc("DwmGetWindowAttribute") //
)

const (
	GA_ROOT              = 2
	GA_ROOTOWNER         = 3

	GWL_EXSTYLE int32 = -20
	WS_EX_TOOLWINDOW     = 0x00000080
	WS_EX_APPWINDOW      = 0x00040000

	EVENT_OBJECT_CREATE         = 0x8000
	EVENT_OBJECT_DESTROY        = 0x8001
	EVENT_OBJECT_SHOW           = 0x8002
	EVENT_OBJECT_HIDE           = 0x8003
	EVENT_OBJECT_LOCATIONCHANGE = 0x800B
	EVENT_SYSTEM_FOREGROUND     = 0x0003
	EVENT_SYSTEM_MINIMIZESTART  = 0x0016
	EVENT_SYSTEM_MINIMIZEEND    = 0x0017
	
	SWP_NOSIZE        = 0x0001
	SWP_NOMOVE        = 0x0002
	SWP_NOZORDER      = 0x0004
	SWP_NOOWNERZORDER = 0x0200
	SWP_SHOWWINDOW    = 0x0040
	SWP_NOACTIVATE    = 0x0010
	
	ABM_GETSTATE = 0x00000004
	ABS_AUTOHIDE = 0x0000001
	ABS_ALWAYSONTOP = 0x0000002
	
	SW_HIDE            = 0
	SW_SHOWNORMAL      = 1
	SW_SHOWMINIMIZED   = 2
	SW_SHOWMAXIMIZED   = 3
	SW_RESTORE         = 9
	
	WH_KEYBOARD_LL = 13
	HC_ACTION      = 0
	WM_KEYDOWN     = 0x0100
	WM_SYSKEYDOWN  = 0x0104
	WM_KEYUP       = 0x0101
	WM_SYSKEYUP    = 0x0105
	
	VK_RMENU  = 0xA5 // Right Alt
	VK_LSHIFT = 0xA0 // Left Shift
	VK_RSHIFT = 0xA1 // Right Shift
	VK_SHIFT  = 0x10 // Shift (either)
	
	WM_CLOSE = 0x0010
	
	VK_H = 0x48 // VK codes for letters are just their ASCII value
	VK_J = 0x4A
	VK_K = 0x4B
	VK_L = 0x4C
	VK_T = 0x54
	VK_M = 0x4D
	VK_W = 0x57
	VK_0 = 0x30
	VK_1 = 0x31
	VK_2 = 0x32
	VK_3 = 0x33
	VK_4 = 0x34
	VK_5 = 0x35
	VK_6 = 0x36
	VK_7 = 0x37
	VK_8 = 0x38
	VK_9 = 0x39
	
	OBJID_WINDOW         = 0
	DWMWA_EXTENDED_FRAME_BOUNDS = 9
)

var suppressed = map[uintptr]time.Time{}
const suppressionWindow = 500 * time.Millisecond

func suppressWindow(hwnd uintptr) {
	suppressed[hwnd] = time.Now()
}

type KBDLLHOOKSTRUCT struct {
	VkCode      uint32
	ScanCode    uint32
	Flags       uint32
	Time        uint32
	DwExtraInfo uintptr
}

type APPBARDATA struct {
	CbSize uint32
	HWnd   uintptr
	UCallbackMessage uint32
	UEdge uint32
	Rc RECT
	LParam uintptr
}

type MSG struct {
	HWnd   uintptr
	Msg    uint32
	WParam uintptr
	LParam uintptr
	Time   uint32
	Pt     struct {
		X int32
		Y int32
	}
}

type POINT struct {
	X int32
	Y int32
}

type RECT struct {
	Left   int32
	Top    int32
	Right  int32
	Bottom int32
}

type MONITORINFO struct {
	CbSize    uint32
	RcMonitor RECT
	RcWork    RECT
	DwFlags   uint32
}

type WINDOWPLACEMENT struct {
	Length           uint32
	Flags            uint32
	ShowCmd          uint32
	MinPosition      POINT
	MaxPosition      POINT
	NormalPosition   RECT
}

type treeNode struct {
	children []interface{}
	parent *treeNode
	splitHorz bool
}

type workSpaces struct {
	activeWorkspace int
	activeNodes []uintptr
	trees []treeNode
}

var curMon int
var data []workSpaces
var taskbar_heights []int32
var monitors []MONITORINFO
var makingChanges bool = false

func getExtendedFrameBounds(hwnd uintptr) (RECT, bool) {
	var r RECT
	rcb := uintptr(unsafe.Sizeof(r))

	hr, _, _ := procDwmGetWindowAttribute.Call(
		hwnd,
		uintptr(DWMWA_EXTENDED_FRAME_BOUNDS),
		uintptr(unsafe.Pointer(&r)),
		rcb,
	)
	if hr != 0 { // S_OK == 0
		return RECT{}, false
	}
	return r, true
}

func getWindowExStyle(hwnd uintptr) uintptr {
	idx := GWL_EXSTYLE              // int32 variable (not a constant expression)
	style, _, _ := procGetWindowLongPtrW.Call(
		hwnd,
		uintptr(idx),               // now legal: int32 -> uintptr via variable
	)
	return style
}

func getWindowClass(hwnd uintptr) string {
	buf := make([]uint16, 256)
	r, _, _ := procGetClassNameW.Call(
		hwnd,
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(len(buf)),
	)
	if r == 0 {
		return ""
	}
	return syscall.UTF16ToString(buf)
}

func restoreWindow(hwnd uintptr) {
	procShowWindow.Call(hwnd, SW_RESTORE)
	suppressWindow(hwnd)
}

func minimizeWindow(hwnd uintptr) {
	procShowWindow.Call(hwnd, SW_SHOWMINIMIZED)
	suppressWindow(hwnd)
}

func taskbarIsAutoHidden() bool {
	var abd APPBARDATA
	abd.CbSize = uint32(unsafe.Sizeof(abd))

	ret, _, _ := procSHAppBarMessage.Call(
		uintptr(ABM_GETSTATE),
		uintptr(unsafe.Pointer(&abd)),
	)

	return (ret & ABS_AUTOHIDE) != 0
}

func findTaskbarWindows() []uintptr {
	var result []uintptr

	// Primary
	h1, _, _ := procFindWindowW.Call(
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr("Shell_TrayWnd"))),
		0,
	)
	if h1 != 0 {
		result = append(result, h1)
	}

	// Secondary monitors
	h2, _, _ := procFindWindowW.Call(
		uintptr(unsafe.Pointer(syscall.StringToUTF16Ptr("Shell_SecondaryTrayWnd"))),
		0,
	)
	if h2 != 0 {
		result = append(result, h2)
	}

	return result
}

func compareSizes(hwnd uintptr) RECT {
	var r RECT // l, t, r, b
	procGetWindowRect.Call(hwnd, uintptr(unsafe.Pointer(&r)))
	
	var r2 RECT // this is 0, 0, w, h
	procGetClientRect.Call(hwnd, uintptr(unsafe.Pointer(&r2)))
	
	r1_w := r.Right-r.Left
	r1_h := r.Bottom-r.Top
	
	r2_w := r2.Right
	r2_h := r2.Bottom
	return RECT{(r1_w-r2_w)/2, 0, r1_w-r2_w, r1_h-r2_h}
}

func getTaskbarRects() map[uintptr]RECT {
	taskbars := findTaskbarWindows()
	result := map[uintptr]RECT{}

	for _, hwnd := range taskbars {
		var r RECT
		procGetWindowRect.Call(hwnd, uintptr(unsafe.Pointer(&r)))

		result[hwnd] = r
	}

	return result
}

func getMessage(msg *MSG) bool {
	r1, _, _ := procGetMessageW.Call(
		uintptr(unsafe.Pointer(msg)),
		0,
		0,
		0,
	)
	// >0 = message, 0 = WM_QUIT, <0 = error
	return r1 > 0
}

func translateMessage(msg *MSG) {
	procTranslateMessage.Call(uintptr(unsafe.Pointer(msg)))
}

func dispatchMessage(msg *MSG) {
	procDispatchMessageW.Call(uintptr(unsafe.Pointer(msg)))
}

func moveWindowToMonitor(hwnd uintptr, monitorIndex int, width, height int32) {
	ms := enumDisplayMonitors()
	if monitorIndex < 0 || monitorIndex >= len(ms) {
		return
	}

	m := ms[monitorIndex].RcMonitor

	x := m.Left + (m.Right-m.Left-width)/2
	y := m.Top + (m.Bottom-m.Top-height)/2

	moveResizeWindow(hwnd, x, y, width, height)
}

func moveResizeWindow(hwnd uintptr, x, y, width, height int32) {
	change := compareSizes(hwnd)
	
	procSetWindowPos.Call(
		hwnd,
		0, // hWndInsertAfter (0 = no change)
		uintptr(x-change.Left),
		uintptr(y-change.Top),
		uintptr(width+change.Right),
		uintptr(height+change.Bottom),
		SWP_SHOWWINDOW,
	)
}

func getWindowTitle(hwnd uintptr) string {
	buf := make([]uint16, 255)
	procGetWindowTextW.Call(
		hwnd,
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(len(buf)),
	)

	// Find null terminator
	n := 0
	for n < len(buf) && buf[n] != 0 {
		n++
	}
	if n == 0 {
		return ""
	}
	return string(utf16.Decode(buf[:n]))
}

func getWindowPlacement(hwnd uintptr) (showCmd uint32, ok bool) {
	var wp WINDOWPLACEMENT
	wp.Length = uint32(unsafe.Sizeof(wp))

	r, _, _ := procGetWindowPlacement.Call(
		hwnd,
		uintptr(unsafe.Pointer(&wp)),
	)

	if r == 0 {
		return 0, false
	}
	return wp.ShowCmd, true
}

func isInterestingWindow(hwnd uintptr) bool {
	// Visible?
	vis, _, _ := procIsWindowVisible.Call(hwnd)
	if vis == 0 {
		return false
	}
	
	// Root owner (closer to Alt+Tab behavior)
	root, _, _ := procGetAncestor.Call(hwnd, GA_ROOTOWNER)
	if root != hwnd {
		return false
	}
	
	// Extended styles
	exStyle := getWindowExStyle(hwnd)
	
	// Skip tool windows (floating palettes, etc.)
	if exStyle&WS_EX_TOOLWINDOW != 0 {
		return false
	}
	
	// Filter out known shell windows (taskbar / Start menu hosts)
	class := getWindowClass(hwnd)
	switch class {
	case "Shell_TrayWnd",          // taskbar
		"Shell_SecondaryTrayWnd",  // secondary taskbar
		"DV2ControlHost",          // classic start menu host
		"Windows.UI.Core.CoreWindow": // modern Start menu / UWP shell
		return false
	}
	
	// Must have a non-empty title
	title := getWindowTitle(hwnd)
	if len(title) == 0 {
		return false
	}
	
	return true
}

func enumDisplayMonitors() []MONITORINFO {
	taskbars := getTaskbarRects()
	keepem := !taskbarIsAutoHidden()
	monitors = []MONITORINFO{}

	cb := syscall.NewCallback(func(hMonitor, hdc uintptr, lprc uintptr, lparam uintptr) uintptr {
		var mi MONITORINFO
		mi.CbSize = uint32(unsafe.Sizeof(mi))

		procGetMonitorInfoW.Call(
			hMonitor,
			uintptr(unsafe.Pointer(&mi)),
		)
		
		if (keepem) {
			for _, r := range(taskbars) {
				if r.Left >= mi.RcMonitor.Left && r.Right <= mi.RcMonitor.Right && r.Top >= mi.RcMonitor.Top && r.Bottom <= mi.RcMonitor.Bottom {
					taskbar_heights = append(taskbar_heights, r.Bottom-r.Top)
				}
			}
		}else{
			taskbar_heights = append(taskbar_heights, 0)
		}
		
		monitors = append(monitors, mi)
		return 1 // continue enumeration
	})

	procEnumDisplayMonitors.Call(0, 0, cb, 0)
	return monitors
}

func isInTree(hwnd uintptr, tree *treeNode) *treeNode {
	for _, c := range tree.children {
		c_hwnd, ok := c.(uintptr)
		if ok && c_hwnd == hwnd{
			return tree
		}
		c_nd, ok := c.(*treeNode)
		if ok {
			opt := isInTree(hwnd, c_nd)
			if opt != nil {
				return opt
			}
		}
	}
	
	return nil
}

func tryToSetActive(hwnd uintptr) {
	for mi, wss := range(data) {
		for j, tree := range(wss.trees) {
			if isInTree(hwnd, &tree) != nil {
				wss.activeNodes[j] = hwnd
				curMon = mi
				return
			}
		}
	}
}

func eventCallback(hWinEventHook, event, hwnd, idObject, idChild, dwEventThread, dwmsEventTime uintptr) uintptr {
	// Only actual window objects
	if int32(idObject) != OBJID_WINDOW { return 0 }
	
	if makingChanges { return 0; }

	ev := uint32(event)
	
	t, ok := suppressed[hwnd];
	if ev != EVENT_SYSTEM_FOREGROUND && ok {
		if time.Since(t) < suppressionWindow {
			// ignore artificial events
//			fmt.Println("SUPPRESS:", hwnd, event)
			return 0
		}
		delete(suppressed, hwnd) // cleanup
	}
	
	if (ev == EVENT_OBJECT_SHOW || ev == EVENT_SYSTEM_MINIMIZEEND) {
		windowDeleted(hwnd)
		
		title := getWindowTitle(hwnd)
		fmt.Println("Open  - ", title)
		
		if !isInterestingWindow(hwnd) {
			return 0
		}
		
		wss := data[curMon]
		aw := wss.activeWorkspace
		addToTree(hwnd, &wss.trees[aw], wss.activeNodes[aw], 2)
		
		locate()
	} else if (ev == EVENT_OBJECT_LOCATIONCHANGE) {
		
	}else if (ev == EVENT_OBJECT_HIDE || ev == EVENT_SYSTEM_MINIMIZESTART) {
		title := getWindowTitle(hwnd)
		fmt.Println("Close - ", title)
		
		if (windowDeleted(hwnd)) {
			locate()
		}
	}else if (ev == EVENT_SYSTEM_FOREGROUND) {
		tryToSetActive(hwnd)
	}
	
	return 0
}

func removeFromTree(hwnd uintptr, tree *treeNode) bool {
	for i, c := range(tree.children) {
		c_hwnd, ok := c.(uintptr)
		if (ok && c_hwnd == hwnd) {
			tree.children = slices.Delete(tree.children, i, i+1)
			
			if len(tree.children) == 1 && tree.parent != nil {
				for j, c_p := range(tree.parent.children) {
					if c_p == tree {
						c_c_nd, ok := tree.children[0].(*treeNode)
						if ok {
							c_c_nd.parent = tree.parent
						}
						
						tree.parent.children[j] = tree.children[0]
						break
					}
				}
			}else if len(tree.children) == 1 {
				nd, ok := tree.children[0].(*treeNode)
				if ok {
					tree.splitHorz = nd.splitHorz
					tree.children = nd.children
					for _, c2 := range tree.children {
						nd2, ok := c2.(*treeNode)
						if ok {
							nd2.parent = tree
						}
					}
				}
			}
			
			return true
		}
		
		c_nd, ok := c.(*treeNode)
		if (ok) {
			if (removeFromTree(hwnd, c_nd)) { return true }
		}
	}
	
	return false
}

func addToTree(hwnd uintptr, tree *treeNode, activeNode uintptr, splitdir int) bool {
	if len(tree.children) == 0 {
		tree.children = append(tree.children, hwnd)
		return true
	}
	
	if len(tree.children) == 1 {
		tree.children = append(tree.children, hwnd)
		if splitdir == 2 || splitdir == 0 {
			tree.splitHorz = false
		}else{
			tree.splitHorz = true
		}
		return true
	}
	
	for i, c := range tree.children {
		c_hwnd, ok := c.(uintptr)
		if (ok && (c_hwnd == activeNode || activeNode == 0)) {
			splitHorz := false
			if (splitdir == 2) {
				splitHorz = !tree.splitHorz
			}else if (splitdir == 0){
				splitHorz = false
			}else{
				splitHorz = true
			}
			
			if (splitHorz != tree.splitHorz) {
				newNode := treeNode{}
				newNode.children = append(newNode.children, c_hwnd)
				newNode.children = append(newNode.children, hwnd)
				newNode.splitHorz = splitHorz
				newNode.parent = tree
				tree.children[i] = &newNode
			}else{
				tree.children = append(tree.children, hwnd)
			}
			
			return true
		}
		
		c_nd, ok := c.(*treeNode)
		if (ok) {
			if (addToTree(hwnd, c_nd, activeNode, splitdir)) { return true }
		}
	}
	
	return false
}

func getNewActive(tree *treeNode) uintptr {
	for _, c := range(tree.children) {
		hwnd, ok := c.(uintptr)
		if (ok) { return hwnd }
		nd, ok := c.(*treeNode)
		if (ok) {
			opt := getNewActive(nd)
			if (opt != 0) {
				return opt
			}
		}
	}
	
	return 0
}

func windowDeleted(hwnd uintptr) bool {
	foundit := false;
	
	for wi := range data {
		wss := &data[wi] // pointer to the real workspace

		for ti := range wss.trees {
			tree := &wss.trees[ti] // pointer to the real tree node

			if removeFromTree(hwnd, tree) {
				foundit = true;
				
				// We found and removed it in this tree
				if wss.activeNodes[ti] == hwnd {
					wss.activeNodes[ti] = getNewActive(tree)
				}
				break // no need to keep searching this workspace
			}
		}
	}
	
	return foundit;
}

func locate() {
	makingChanges = true
	
	for m_idx, wss := range(data) {
		rct := monitors[m_idx].RcMonitor
		if !taskbarIsAutoHidden() {
			rct.Bottom -= taskbar_heights[m_idx]
		}
		
		drawTree(&wss.trees[wss.activeWorkspace], rct.Left, rct.Top, rct.Right-rct.Left, rct.Bottom-rct.Top)
	}
	
	makingChanges = false
}

func drawTree(tree *treeNode, x, y, w, h int32) {
	if (len(tree.children) == 0) { return }
	
	println("Drawing tree", len(tree.children))
	
	cx := x;
	cy := y;
	cw := w;
	ch := h;
	ax := int32(0);
	ay := int32(0);
	
	if (tree.splitHorz) {
		ch = h/int32(len(tree.children))
		ay = ch
	}else{
		cw = w/int32(len(tree.children))
		ax = cw;
	}
	
	for _, c := range tree.children {
		hwnd, ok := c.(uintptr)
		if (ok) {
			restoreWindow(hwnd)
			moveResizeWindow(hwnd, cx, cy, cw, ch);
		}
		nd, ok := c.(*treeNode)
		if (ok) {
			drawTree(nd, cx, cy, cw, ch);
		}
		
		
		
		cx += ax
		cy += ay
	}
}

func printTree(tree *treeNode, depth int) {
	for range depth {
		fmt.Print("=")
	}
	fmt.Println("Split direction", tree.splitHorz)
	
	for _, c := range tree.children {
		c_hwnd, ok := c.(uintptr)
		if ok {
			for range depth {
				fmt.Print("-")
			}
			fmt.Println(getWindowTitle(c_hwnd))
			continue
		}
		
		c_nd, ok := c.(*treeNode)
		if ok {
			printTree(c_nd, depth+1)
			continue
		}
		
		for range depth {
			fmt.Print("-")
		}
		fmt.Printf("Unknown: %T\n", c)
	}
}

func toggleSlice(hwnd uintptr) {
	for wi := range len(data) {
		wss := &data[wi]
		
		for ti := range len(wss.trees) {
			tree := &wss.trees[ti]
			node := isInTree(hwnd, tree);
			if node == nil {
				continue
			}
			
			makingChanges = true
			
			new := !node.splitHorz
			windowDeleted(hwnd)
			if new {
				addToTree(hwnd, tree, wss.activeNodes[ti], 1)
			}else{
				addToTree(hwnd, tree, wss.activeNodes[ti], 0)
			}
			
			locate()
			
			makingChanges = false
			return
		}
	}
}

func nextMonitor(hwnd uintptr) {
	if len(data) <= 1 {
		return
	}
	
	isin := isInTree(hwnd, &data[curMon].trees[data[curMon].activeWorkspace])
	if isin == nil {
		return
	}
	
	curMon = (curMon + 1) % len(data)
	
	windowDeleted(hwnd)
	addToTree(hwnd, &data[curMon].trees[data[curMon].activeWorkspace], data[curMon].activeNodes[data[curMon].activeWorkspace], 2)
	locate()
}

// Helper to check if a key is currently pressed down
func isKeyDown(vkCode uintptr) bool {
	// GetAsyncKeyState's high bit is set if the key is down.
	// This means the int16 representation of the state will be negative.
	state, _, _ := procGetAsyncKeyState.Call(vkCode)
	return int16(state) < 0
}

// Our new keyboard hook callback
func keyboardCallback(nCode int, wParam uintptr, lParam uintptr) uintptr {
	// If nCode < 0, we must pass it on
	if nCode < 0 {
		return callNextHookEx(nCode, wParam, lParam)
	}

	// We only care about key down events
	if nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
		// Cast lParam to our struct
		kbd := (*KBDLLHOOKSTRUCT)(unsafe.Pointer(lParam))
		vkCode := kbd.VkCode

		// Check if Right Alt is held down
		if isKeyDown(VK_RMENU) {
			
			// Check if Shift is also held down (either L or R)
//			isShift := isKeyDown(VK_LSHIFT) || isKeyDown(VK_RSHIFT)

			switch vkCode {
			case VK_H:
//				if isShift {
//					fmt.Println("HOTKEY: RAlt + Shift + H")
//				} else {
//					fmt.Println("HOTKEY: RAlt + H")
//				}
				return 1 // Return 1 to "swallow" the key (other apps won't see it)
			case VK_J:
//				if isShift {
//					fmt.Println("HOTKEY: RAlt + Shift + J")
//				} else {
//					fmt.Println("HOTKEY: RAlt + J")
//				}
				return 1
			case VK_K:
//				if isShift {
//					fmt.Println("HOTKEY: RAlt + Shift + K")
//				} else {
//					fmt.Println("HOTKEY: RAlt + K")
//				}
				return 1
			case VK_L:
//				if isShift {
//					fmt.Println("HOTKEY: RAlt + Shift + L")
//				} else {
//					fmt.Println("HOTKEY: RAlt + L")
//				}
				return 1
			case VK_M:
				hwnd := data[curMon].activeNodes[data[curMon].activeWorkspace];
				if hwnd != 0 {
					nextMonitor(hwnd)
				}
				return 1
			case VK_T:
				hwnd := data[curMon].activeNodes[data[curMon].activeWorkspace];
				if hwnd != 0 {
					toggleSlice(hwnd)
				}
				return 1
			case VK_W:
				hwnd := data[curMon].activeNodes[data[curMon].activeWorkspace];
				if hwnd != 0 {
					procSendMessage.Call(
						uintptr(hwnd),
						uintptr(WM_CLOSE),
						wParam,
						lParam,
					)
				}
				return 1
			case VK_0:
				switchWorkspace(9)
				return 1
			case VK_1:
				switchWorkspace(0)
				return 1
			case VK_2:
				switchWorkspace(1)
				return 1
			case VK_3:
				switchWorkspace(2)
				return 1
			case VK_4:
				switchWorkspace(3)
				return 1
			case VK_5:
				switchWorkspace(4)
				return 1
			case VK_6:
				switchWorkspace(5)
				return 1
			case VK_7:
				switchWorkspace(6)
				return 1
			case VK_8:
				switchWorkspace(7)
				return 1
			case VK_9:
				switchWorkspace(8)
				return 1
			}
		}
	}
	
	return callNextHookEx(nCode, wParam, lParam)
}

func minimizeTree(tree *treeNode) {
	for _, c := range(tree.children) {
		hwnd, ok := c.(uintptr)
		if ok {
			println("Minimizing: ", getWindowTitle(hwnd))
			minimizeWindow(hwnd)
		}
		nd, ok := c.(*treeNode)
		if ok {
			minimizeTree(nd)
		}
	}
}

func restoreTree(tree *treeNode) {
	for _, c := range(tree.children) {
		hwnd, ok := c.(uintptr)
		if ok {
			println("Restoring: ", getWindowTitle(hwnd))
			restoreWindow(hwnd)
		}
		nd, ok := c.(*treeNode)
		if ok {
			restoreTree(nd)
		}
	}
}

func switchWorkspace(wi int) {
	println("Switching workspace: ", wi)
	
	makingChanges = true
	
	old := data[curMon].activeWorkspace
	
	minimizeTree(&data[curMon].trees[old])
	data[curMon].activeWorkspace = wi
	restoreTree(&data[curMon].trees[wi])
	locate()
	
	makingChanges = false
}

// Helper function to call CallNextHookEx
func callNextHookEx(nCode int, wParam uintptr, lParam uintptr) uintptr {
	// We pass 0 as the hook handle, which is fine
	ret, _, _ := procCallNextHookEx.Call(0, uintptr(nCode), wParam, lParam)
	return ret
}

func main() {
	ms := enumDisplayMonitors()
	curMon = 0
	for i, m := range ms {
		fmt.Printf("Monitor %d: %+v\n", i, m.RcMonitor)
		
		ws := workSpaces{}
		ws.trees = make([]treeNode, 10)
		ws.activeNodes = make([]uintptr, 10)
		ws.activeWorkspace = 0
		
		data = append(data, ws)
	}
	
	if (len(data) == 0) {
		fmt.Println("Not enough monitors to run (0)")
		return
	}
	
	fmt.Print("\n\n\n\n\n")
	
	cb1 := syscall.NewCallback(func(hwnd uintptr, lparam uintptr) uintptr {
		if !isInterestingWindow(hwnd) {
			return 1;
		}
		title := getWindowTitle(hwnd)
		
		
		state, ok := getWindowPlacement(hwnd)
		if ok {
			switch state {
			case SW_SHOWMINIMIZED:
				return 0;
			case SW_SHOWMAXIMIZED:
				fmt.Println("State: maximized")
			case SW_SHOWNORMAL:
				fmt.Println("State: normal")
			}
		}
		
		fmt.Println("Open  - ", title)
		
		wss := data[curMon]
		addToTree(hwnd, &wss.trees[wss.activeWorkspace], wss.activeNodes[wss.activeWorkspace], 2)
		wss.activeNodes[wss.activeWorkspace] = hwnd
		
		return 1 // continue enumeration
	})
	
	procEnumWindows.Call(cb1, 0)
	
	// we now have all windows/monitors, time to organize them
	locate();
	
	// start listening for more windows
	cb := syscall.NewCallback(eventCallback)

	// We hook a range: CREATE..HIDE, and then filter inside.
	// This gives us CREATE, DESTROY, SHOW, HIDE, but we only care about SHOW/DESTROY.
	hook, _, err := procSetWinEventHook.Call(
		uintptr(0x0003),                      // Event Min
		uintptr(0x800B),                      // Event Max
		0,                                    // hmodWinEventProc (0 = this module)
		cb,                                   // callback
		0, 0,                                 // process & thread (0,0 = all)
		0,                                    // WINEVENT_OUTOFCONTEXT
	)
	if hook == 0 {
		fmt.Println("SetWinEventHook failed:", err)
		return
	}
	
	kbCallback := syscall.NewCallback(keyboardCallback)
	kbHook, _, err := procSetWindowsHookEx.Call(
		uintptr(WH_KEYBOARD_LL),
		kbCallback,
		0,
		0,
	)
	if kbHook == 0 {
		fmt.Println("SetWindowsHookEx failed:", err)
		return
	}
	// Make sure to unhook when the program exits
	defer procUnhookWindowsHookEx.Call(kbHook)
	
	var msg MSG
	for getMessage(&msg) {
		translateMessage(&msg)
		dispatchMessage(&msg)
	}
}