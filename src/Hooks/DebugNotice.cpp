#include "FallingStars.h"

#include <YRpp.h>
#include <Core/Module.h>
#include <Core/Logging.h>

#pragma comment(lib, "shell32.lib") // ShellExecuteW

// ---------------------------------------------------------------------------
// Debug-only startup notice.
//
// Purpose: in a DEBUG build, right after FallingStars.dll is injected and the
// game reaches the ExeRun entry hook (FallingStars.cpp, DEFINE_HOOK(0x7CD810)),
// show a modal "gate" dialog telling the user "this is a debug build / test
// session". Release builds compile this whole file to nothing.
//
// Three choices via MessageBoxW(MB_YESNOCANCEL) - NOT TaskDialogIndirect:
// TaskDialog needs Common Controls v6 (comctl32 v6 manifest), which the
// 2001-era gamemd.exe process does not have, so TaskDialogIndirect simply
// fails there and the dialog would never appear. MessageBox is a legacy API
// that works in any process.
//   - 是       : continue into the game.
//   - 否       : terminate the process - the game never starts (a debug
//                build must not run in normal play).
//   - 取消      : opens the project link (placeholder URL, replace it), then
//                re-shows the dialog, since the user has not decided yet.
// Closing via Esc / X returns IDCANCEL, i.e. the link action - acceptable.
//
// This module is driven by FS::ModuleRegistry::OnLoadAll(), which the entry
// hook calls - the same Phobos pattern (Phobos::ExeRun). No --handshakes flag
// is needed, and no game address is hardcoded in this file.
// ---------------------------------------------------------------------------
// Debug-only startup notice. 稳定应用版（FS_STABLE）强制编译为空——
// 即使误用 Debug 配置 + FS_STABLE 也不会弹任何注入提示。
#if defined(_DEBUG) && !defined(FS_STABLE)

namespace
{

	// TODO: replace with the real project/documentation link.
	constexpr const wchar_t* ProjectLink = L"https://github.com/ME-RA2YR-Studio/FallingStars/releases";

	WNDPROC g_prevWndProc = nullptr;

	// Subclassed dialog proc: the X (close) button sends WM_CLOSE, which
	// MessageBox maps to IDCANCEL - but we want X to behave exactly like 否
	// (exit the game, no link). So we redirect WM_CLOSE to a synthetic click
	// on the "否" button (WM_COMMAND/IDNO), letting MessageBox close itself
	// through its normal path.
	LRESULT CALLBACK GateWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == WM_CLOSE)
		{
			SendMessageW(hWnd, WM_COMMAND, IDNO, 0);
			return 0;
		}
		return CallWindowProcW(g_prevWndProc, hWnd, msg, wParam, lParam);
	}

	// WH_CBT hook: rename MessageBox's built-in "取消" button to "链接"
	// (MessageBox cannot customize button text), and subclass the dialog so
	// X (WM_CLOSE) is treated as 否.
	//
	// IMPORTANT: only touch the MessageBox dialog itself. The hook fires for
	// EVERY activated window (including the game window when the user clicks
	// back to it); subclassing a non-dialog window - or subclassing the
	// dialog a second time after refocus - chains GateWndProc to itself and
	// crashes on the next click. So: require the IDCANCEL button, and only
	// subclass once.
	LRESULT CALLBACK CbtProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (nCode == HCBT_ACTIVATE)
		{
			HWND const hDlg = reinterpret_cast<HWND>(wParam);
			if (GetDlgItem(hDlg, IDCANCEL)) // MessageBox dialog only
			{
				SetWindowTextW(GetDlgItem(hDlg, IDCANCEL), L"链接(&L)");
				if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hDlg, GWLP_WNDPROC)) != &GateWndProc)
				{
					g_prevWndProc = reinterpret_cast<WNDPROC>(
						SetWindowLongPtrW(hDlg, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GateWndProc)));
				}
			}
		}
		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}

	/// <summary>Show the gate dialog once.
	/// 是 = continue; 否 / X(close) = terminate (no link);
	/// 链接 = open the project link, then terminate exactly like 否.</summary>
	bool ConfirmDebugRun()
	{
		g_prevWndProc = nullptr;
		HHOOK const hHook = SetWindowsHookExW(WH_CBT, &CbtProc, nullptr, GetCurrentThreadId());

		const int result = MessageBoxW(nullptr,
			L"FallingStars 调试版已注入 gamemd.exe。\n"
			L"\n"
			L"当前运行的是 Debug 构建：此进程包含调试/测试代码。\n"
			L"选择“是”继续游戏，选择“否”退出游戏，选择“链接”打开项目链接并退出。",
			L"FallingStars - 调试提醒",
			MB_YESNOCANCEL | MB_ICONWARNING | MB_TOPMOST);

		if (hHook)
			UnhookWindowsHookEx(hHook);

		if (result == IDYES)
			return true;

		// 链接按钮（Esc 也返回 IDCANCEL，与链接一致）：打开链接后退出。
		// X 关闭已被子类化转成 IDNO，直接退出、不开链接。
		if (result == IDCANCEL)
			ShellExecuteW(nullptr, L"open", ProjectLink, nullptr, nullptr, SW_SHOWNORMAL);

		return false;
	}

} // namespace

class DebugNoticeModule final : public FS::IModule
{
public:
	static DebugNoticeModule& Instance()
	{
		static DebugNoticeModule instance;
		return instance;
	}

	const char* Name() const override { return "DebugNoticeModule"; }

	void OnLoad() override
	{
		if (!ConfirmDebugRun())
		{
			// Hard-stop: a debug build must not run in normal play.
			TerminateProcess(GetCurrentProcess(), 0);
		}

		FS_LOG("[FallingStars] Debug notice shown.\n");
	}
};

REGISTER_MODULE(DebugNoticeModule)

#endif // _DEBUG && !FS_STABLE
