// 必须在引入 YRpp 头之前取消 windows.h 的 DrawText 宏：
// 否则 Surface::DrawText 的成员声明会被预处理器改写成 DrawTextW，
// 导致 DSurface::Composite->DrawText(...) 调用报 C2665（与 UnitCounter 同因）。
#include <windows.h>
#undef DrawText

#include "Body.h"

#include <Core/Module.h>
#include <Core/Logging.h>
#include <FallingStars.h>

#include <StringTable.h>
#include <Surface.h>
#include <Drawing.h>
#include <TActionClass.h>
#include <cstring>
#include <cwchar>

namespace FS
{
	namespace TopRightText
	{

		std::vector<Entry> Entries;

		// -------------------------------------------------------------------
		// 动作 522/523/524 处理：建立 / 原位替换 / 按编号删除
		//
		// 参数读取（与 FA2 地图 [Actions] 段的实际加载一致，依据
		// TActionClass::LoadFromINI 0x6DD5B0 的模式位分派）：
		//   522/523（P1=4 文本模式，同 Phobos 横幅 800）：
		//     P2 → TActionClass::Text   （CSF 条目名）
		//     P3 → TActionClass::Param3 （文本编号）
		//   524（P1=0 数值模式，同 Phobos 横幅 802）：
		//     P2 → TActionClass::Value  （要删除的文本编号）
		// -------------------------------------------------------------------
		void HandleAction(TActionClass* pAction)
		{
			if (!pAction)
				return;

			const int kind = static_cast<int>(pAction->ActionKind);

			// 诊断日志：记录原始字段，便于排查 FAData / 地图参数是否对齐
			FS_LOG("[FallingStars] TopRightText: 收到动作 kind=%d Value=%d Param3=%d Param4=%d Text=[%s]\n",
				kind, pAction->Value, pAction->Param3, pAction->Param4, pAction->Text);

			// ---- 524：按编号删除（编号在 Value 字段，同 Phobos 802）----
			if (kind == Action_TextDelete)
			{
				const int id = pAction->Value;
				for (auto it = Entries.begin(); it != Entries.end(); ++it)
				{
					if (it->ID == id)
					{
						Entries.erase(it);
						FS_LOG("[FallingStars] TopRightText: 删除 编号=%d（剩余 %zu 行）\n",
							id, Entries.size());
						return;
					}
				}
				FS_LOG("[FallingStars] TopRightText: 删除失败，编号=%d 不存在\n", id);
				return;
			}

			// ---- 522/523：编号在 Param3，文本在 Text（P1=4 文本模式）----
			Entry entry;
			entry.ID = pAction->Param3;
			strncpy(entry.Text, pAction->Text, sizeof(entry.Text) - 1);
			entry.Text[sizeof(entry.Text) - 1] = '\0';

			if (kind != Action_TextCreate && kind != Action_TextReplace)
				return;

			// 按编号查找已有条目（编号锁定：同编号即同一行文本）
			for (Entry& e : Entries)
			{
				if (e.ID == entry.ID)
				{
					// 522 建立 / 523 替换 命中已有编号：原位覆盖文本，
					// 该行在队列中的位置（即屏幕上的行位）保持不变。
					strncpy(e.Text, entry.Text, sizeof(e.Text) - 1);
					e.Text[sizeof(e.Text) - 1] = '\0';
					FS_LOG("[FallingStars] TopRightText: %s 编号=%d [%s]\n",
						kind == Action_TextReplace ? "替换" : "覆盖建立", entry.ID, entry.Text);
					return;
				}
			}

			// 编号不存在：
			//   522 建立 → 追加新行（排在现有文本下方）；
			//   523 替换 → 严格替换语义，编号不存在则不执行（仅记日志），
			//              便于地图作者发现编号笔误。
			if (kind == Action_TextReplace)
			{
				FS_LOG("[FallingStars] TopRightText: 替换失败，编号=%d 不存在（未创建新文本）\n",
					entry.ID);
				return;
			}

			Entries.push_back(entry);
			FS_LOG("[FallingStars] TopRightText: 建立 编号=%d [%s]（当前 %zu 行）\n",
				entry.ID, entry.Text, Entries.size());
		}

		// -------------------------------------------------------------------
		// 每帧渲染（由 0x4F4780 UpdatePrimarySurface Hook 调用，UI 最上层）
		//
		// 右上角布局（满足三点要求）：
		//   1) 右边界 = 战术视图全局边界 view_bound(0xB0CE28) 的 X+Width - 边距，
		//      即侧栏左缘 —— 雷达图与建造栏都位于侧栏内，因此绝不重叠；
		//      view_bound 不可用时兜底为 screenW-178（侧栏固定宽约 168px）。
		//   2) 多行自上而下间隔绘制：第 0 行贴视图顶部 + 边距，行距 20px；
		//   3) 行位按 Entries 的当前顺序逐帧计算 —— 删除条目后剩余文本
		//      自动上移补位，无需额外处理。
		// 样式：黑色背景块 + 白色文字（回退文本用红色，便于发现配置问题）。
		// -------------------------------------------------------------------
		void RenderAll()
		{
			if (Entries.empty())
				return;

			DSurface* pSurface = DSurface::Composite;
			if (!pSurface)
				return;

			const int screenW = pSurface->GetWidth();
			const int screenH = pSurface->GetHeight();
			if (screenW <= 0 || screenH <= 0)
				return;

		const int rightMargin = 0;  // 紧贴右侧：文本右缘与战术视图右缘/屏幕右缘无间隔
		const int topMargin = 0;    // 贴顶：距视图顶部无间隔
		const int lineGap = 0;      // 行间无额外间隔

			int rightEdge;
			int topY;
			{
				// 战术视图全局边界（与 UnitCounter 相同来源）：
				// X+Width 即侧栏左缘（雷达图 + 建造栏整体在侧栏内）。
				DEFINE_NONSTATIC_REFERENCE(RectangleStruct, view_bound, 0xB0CE28);
				rightEdge = view_bound.X + view_bound.Width - rightMargin;
				topY = view_bound.Y + topMargin;

				// 一次性诊断：登记条目 + 布局参数（定位「文本未显示」类问题）
				static bool s_bLayoutLogged = false;
				if (!s_bLayoutLogged)
				{
					s_bLayoutLogged = true;
					FS_LOG("[FallingStars] TopRightText: 渲染开始，条目=%zu 屏幕=%dx%d view_bound=(%d,%d,%d,%d) 右缘=%d 顶=%d\n",
						Entries.size(), screenW, screenH,
						view_bound.X, view_bound.Y, view_bound.Width, view_bound.Height,
						rightEdge, topY);
				}
			}
			if (rightEdge <= 0 || rightEdge > screenW)
				rightEdge = screenW - 178; // 兜底：避开固定宽度侧栏（~168px）
			if (topY < 0)
				topY = 0;

			int accumulatedH = 0;
			for (const Entry& entry : Entries)
			{
				// 显示文本取 CSF 条目（与触发结果 11「文本触发事件」同机制）；
				// 条目未命中（返回 MISSING: 前缀 / 空）时回退为直接绘制原始文本，
				// 保证「永远有反馈」而不是整行消失（同时记一次日志便于排查）。
				const wchar_t* pText = nullptr;
				bool bFallback = false;
				if (entry.Text[0])
					pText = StringTable::LoadString(entry.Text);
				if (!pText || !*pText || wcsncmp(pText, L"MISSING:", 8) == 0)
				{
					// 回退：把 CSF 条目名（或空文本占位符）当普通文本画出来
					static wchar_t s_fallback[0x20];
					if (!entry.Text[0])
					{
						swprintf(s_fallback, 0x20, L"#%d", entry.ID);
					}
					else
					{
						MultiByteToWideChar(CP_ACP, 0, entry.Text, -1,
							s_fallback, 0x20);
					}
					pText = s_fallback;
					bFallback = true;

					// 每个 ID 只记一次，避免刷屏
					static int s_lastLoggedID = -1;
					if (s_lastLoggedID != entry.ID)
					{
						s_lastLoggedID = entry.ID;
						FS_LOG("[FallingStars] TopRightText: CSF 条目 [%s] 未命中（编号=%d），回退显示原始文本\n",
							entry.Text, entry.ID);
					}
				}

				RectangleStruct dim = Drawing::GetTextDimensions(pText, { 0, 0 }, 0, 2, 0);

				// 右对齐到战术视图右缘，自上而下紧密排列
				int posX = rightEdge - dim.Width;
				if (posX < 0) posX = 0;
				int posY = topY + accumulatedH;
				if (posY + dim.Height > screenH) break; // 超出屏幕底部则截断

			// 黑色背景块 + 白色文字（用户要求：黑色背景的白字）
			// 背景块从文本左缘一直延伸到屏幕右缘，覆盖右侧区域、
			// 与屏幕右缘之间无空缺；行与行之间无额外间隔
			RectangleStruct bg = { posX, posY, screenW - posX, dim.Height };
			pSurface->FillRect(&bg, COLOR_BLACK);
			pSurface->DrawText(pText, posX, posY,
				bFallback ? COLOR_RED : COLOR_WHITE);

				accumulatedH += dim.Height + lineGap;
			}
		}

		void ClearAll()
		{
			Entries.clear();
		}

	} // namespace TopRightText
} // namespace FS

// ---------------------------------------------------------------------------
// 模块自注册（免改中央注册表）。
// 本模块自身不占用任何 Hook 地址：动作分发 / 场景清空 / 每帧渲染复用
// UnitCounter 模块已注册的三个共享 Hook（0x7265C0 / 0x7275D0 / 0x4F4780，
// 同一地址在一个 DLL 内只能有一个跳板），详见 UnitCounter/Body.cpp。
// ---------------------------------------------------------------------------
class TopRightTextModule final : public FS::IModule
{
public:
	static TopRightTextModule& Instance()
	{
		static TopRightTextModule instance;
		return instance;
	}

	const char* Name() const override { return "TopRightText"; }

	void OnLoad() override
	{
		FS_LOG("[FallingStars] TopRightText module ready (Trigger Action 522/523/524: 右上角文本建立/替换/删除).\n");
	}
};

REGISTER_MODULE(TopRightTextModule)
