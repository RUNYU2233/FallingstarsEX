// 必须在引入 YRpp 头之前取消 windows.h 的 DrawText 宏：
// 否则 Surface::DrawText 的成员声明会被预处理器改写成 DrawTextW，
// 导致 DSurface::Composite->DrawText(...) 调用报 C2665。
#include <windows.h>
#undef DrawText

#include "Body.h"
#include <Hooks/ColoredSubtitle/Body.h>

#include <Core/Module.h>
#include <Core/Logging.h>
#include <Core/Macro.h>
#include <FallingStars.h>

#include <TriggerTypeClass.h>
#include <BuildingClass.h>
#include <ScenarioClass.h>
#include <SidebarClass.h>
#include <TacticalClass.h>
#include <ColorScheme.h>
#include <TEventClass.h>
#include <cwchar>
#include <cstring>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <set>

// ---------------------------------------------------------------------------
// FallingStars 自定义触发结果 520「单位计数显示」/ 521「科技类型计数」
//
// 引擎侧由三个 Hook 组成：
//   1. TriggerClass::FireActions (0x7265C0) —— 动作分发：遍历触发器的动作链表，
//      识别动作 520/521 并登记计数器；526（彩色字幕）走同一入口转交
//      FS::ColoredSubtitle::HandleAction。
//   2. TriggerTypeClass::LoadFromINIList (0x7275D0) —— 新场景加载时清空全部计数器
//      （ColoredSubtitle 的登记表一并清空）。
//   3. GScreenClass::Render 内、统计面板之后、鼠标绘制之前 (0x4F4583) ——
//      每帧把当前玩家的计数器绘制在右下角（UI 之上、鼠标指针之下：
//      鼠标在 0x4F4593 绘制，晚于本点，故鼠标永远在计数器之上）；
//      随后绘制彩色字幕（位置不同，互不重叠）。
//   4. 文本绘制原语 (0x4A61C0) —— 计数器占用屏幕最底行，右下角底部区域的
//      文本（超级武器倒计时列表等）自动上移 N*20px 让位（仿 SW 倒计时之间
//      逐行堆叠的让位机制；以坐标过滤仅命中底部区域文本）。
//   5. TActionClass::LoadFromINI (0x6DD768) —— 截获 521 的科技类型注册名
//      （FA2 存的是注册名字符串如 MTNK，引擎 atoi 会变 0），仿事件 61 按名匹配。
//
// 地图 [Actions] 段参数（每动作 8 字段：ID, P1..P7）：
//   520：P1=模式(固定填4), P2=显示文本(CSF条目名), P3=统计目标, P4=单位种类, P5=指定所属方
//   521：P1=模式(固定填4), P2=显示文本(CSF条目名), P3=统计目标, P4=指定所属方, P5=科技类型
//         （P5 = 该科技的【注册名】，如 MTNK=Cavalier Medium Tank；引擎 atoi 后
//          Param5 存 0，故由 Hook 5 在加载期截获原始注册名，按名查找真实索引）
//   注：P1 是 TActionClass::LoadFromINI(0x6DD5B0) 的模式位（token2 做 switch 分派），
//       固定填 4 才会走"文本模式"（P2→Text、P3→Param3…），参数才对齐。
// 其中 P2 为字符串参数：游戏引擎加载时将其写入 TActionClass::Text
// （与触发结果 11「文本触发事件」及 Phobos 动作 500 的机制一致）。
// 无自定义文本(P2 为空)时，显示文本回退为「当前数量：」。
// 数量为 0 的计数器自动消除（不绘制、不占用行位；底部面板避让同样只统计实际显示的条目）。
//
// 布局（仿超级武器倒计时贴底边栏，详见 RenderEntry）：
//   计数器绘制在【右下角、侧栏左侧】，紧贴底边栏（命令栏）顶部，逐行向上堆叠。
//   - 右边界 = 战术视图右边缘(view_bound.X+Width，即侧栏左缘) - 边距
//     → 不与侧栏及其中【超级武器倒计时】重叠；
//   - 底边 = DSurface::ViewBounds(0x886FA0) 底部(即命令栏顶部，640x480 下=400，
//     命令栏高 80px) - 4px → 贴着底边栏；
//   - 超级武器倒计时等右下角底部文本由 Hook 4（0x4A61C0 坐标过滤）自动上移让位。
// ---------------------------------------------------------------------------

namespace FS
{
	namespace UnitCounter
	{

		std::vector<Entry> Entries;

		// -------------------------------------------------------------------
		// 521 科技类型注册名截获（仿事件 61「科技类型不存在」的按名匹配）：
		//   FA2 的科技类型下拉（参数类型 46）在地图 [Actions] 里写的是【注册名
		//   字符串】（如 MTNK），但引擎 TActionClass::LoadFromINI 用 atoi 把它
		//   转成 0 存进 Param5，注册名在加载期就丢失了。
		//   因此在 0x6DD768（P5 的 atoi 调用点）截获原始字符串，存到
		//   s_techNames（按 TActionClass* 指针索引），HandleAction 时再按名
		//   在 TechnoTypeClass::Array 里查找真实索引（与事件 61 的检测方式一致）。
		// -------------------------------------------------------------------
		std::map<TActionClass*, std::string> s_techNames;

		// 在 TechnoTypeClass::Array 中按注册名（ID）查找类型索引；找不到返回 -1
		static int FindTechnoIndexByName(const char* name)
		{
			if (!name || !name[0])
				return -1;
			for (int i = 0; i < TechnoTypeClass::Array.Count; ++i)
			{
				TechnoTypeClass* pT = TechnoTypeClass::Array.Items[i];
				if (pT && pT->ID && _stricmp(pT->ID, name) == 0)
					return i;
			}
			return -1;
		}

		// 供 0x6DD768 截获 hook 调用：保存 521 动作的科技类型注册名
		static void CaptureTechName(TActionClass* pAction, const char* name)
		{
			if (pAction && name && name[0])
				s_techNames[pAction] = name;
		}

		// -------------------------------------------------------------------
		// 单位统计
		// -------------------------------------------------------------------
		static int CountUnits(const Entry& entry)
		{
			int count = 0;

			// 解析「指定所属方」目标 House：
			//   FA2 的"指定所属方"下拉按【国家类型】列出，保存的索引对应
			//   HouseTypeClass::Array（0xA83C98）；而 HouseClass::FindByIndex(0x510ED0)
			//   是 ScenarioClass::HouseIndices 槽位(0..15/Player@A..)语义，两者常错位。
			//   双方式解析：槽位有效时用 FindByIndex；否则按国家类型匹配实际实例。
			HouseClass* pSpecified = nullptr;
			if (entry.Target == CountTarget_Specified)
			{
				// FA2"指定所属方"下拉按【国家类型】列出，存的索引 = HouseTypeClass::Array
				// 索引（地图 [Countries] 顺序，如 1=Europeans、6=PsiCorps）。
				// 不用 HouseClass::FindByIndex（它是 Scenario 槽位语义，会返回错误 House）。
				const int idx = entry.SpecifiedHouseIdx;
				if (idx >= 0 && idx < HouseTypeClass::Array.Count)
				{
					HouseTypeClass* pType = HouseTypeClass::Array.Items[idx];
					for (int h = 0; h < HouseClass::Array.Count; ++h)
					{
						HouseClass* pCand = HouseClass::Array.Items[h];
						if (pCand && pCand->Type == pType)
						{
							pSpecified = pCand;
							break;
						}
					}
				}

			// 全量日志：每个计数器条目首次统计时记录一次"指定所属方"的解析结果
			// （逐条目记录而非仅一次，确保多个不同所属方计数器的解析都被看到）。
			static std::set<const Entry*> s_specLogged;
			if (s_specLogged.find(&entry) == s_specLogged.end())
			{
				s_specLogged.insert(&entry);
				FS_LOG("[FallingStars] UnitCounter: 指定所属方解析 idx=%d TypeMatch=%p(国家=%s) HouseCount=%d\n",
					idx, pSpecified,
					pSpecified ? pSpecified->Type->ID : "(null)",
					HouseClass::Array.Count);
			}
		}

			// 521：按科技类型匹配（P5 = 该科技在 TechnoTypeClass::Array 中的序号）
			TechnoTypeClass* pFilterType = nullptr;
			if (entry.Kind == Action_CountByTechnoType
				&& entry.TechnoTypeIndex >= 0
				&& entry.TechnoTypeIndex < TechnoTypeClass::Array.Count)
			{
				pFilterType = TechnoTypeClass::Array.Items[entry.TechnoTypeIndex];
			}

			for (int i = 0; i < TechnoClass::Array.Count; ++i)
			{
				TechnoClass* pTechno = TechnoClass::Array.Items[i];
				if (!pTechno)
					continue;

			// 死亡 / 幽灵单位不计入
			if (!pTechno->IsAlive || pTechno->InLimbo)
				continue;

			// 建造中的单位/建筑不计入：
			//   - 工厂内尚未出来的单位处于 InLimbo（上面已排除）；
			//   - 建筑在「建造动画」期间已上地图但 BState == Construction(0)，
			//     此时尚未完工，不应计入。
			if (pTechno->WhatAmI() == AbstractType::Building
				&& static_cast<BuildingClass*>(pTechno)->BState == static_cast<int>(BStateType::Construction))
			{
				continue;
			}

				// ---- 种类 / 科技类型过滤 ----
				if (entry.Kind == Action_CountByTechnoType)
				{
					if (!pFilterType || pTechno->GetTechnoType() != pFilterType)
						continue;
				}
				else
				{
					switch (entry.Category)
					{
					case Category_Unit: // 载具（含舰船）
						if (pTechno->WhatAmI() != AbstractType::Unit)
							continue;
						break;
					case Category_Infantry:
						if (pTechno->WhatAmI() != AbstractType::Infantry)
							continue;
						break;
					case Category_Aircraft:
						if (pTechno->WhatAmI() != AbstractType::Aircraft)
							continue;
						break;
					case Category_Naval: // 舰船 = 移动方式为浮渡（Float）的载具
						if (pTechno->WhatAmI() != AbstractType::Unit)
							continue;
						if (!pTechno->GetTechnoType())
							continue;
						if (pTechno->GetTechnoType()->SpeedType != SpeedType::Float)
							continue;
						break;
					case Category_Building:
						if (pTechno->WhatAmI() != AbstractType::Building)
							continue;
						break;
					case Category_All:
					default:
						break;
					}
				}

				// ---- 所属方过滤 ----
				HouseClass* pOwner = pTechno->Owner;
				if (!pOwner)
					continue;

				switch (entry.Target)
				{
				case CountTarget_Enemy: // 敌人：不是自己、不是盟友
					if (pOwner == entry.Owner || entry.Owner->IsAlliedWith(pOwner))
						continue;
					break;
				case CountTarget_Ally: // 盟友（含自身）
					if (pOwner != entry.Owner && !entry.Owner->IsAlliedWith(pOwner))
						continue;
					break;
				case CountTarget_Specified: // 指定所属方
					if (pOwner != pSpecified)
						continue;
					break;
				case CountTarget_Self: // 仅触发所属方自身（事件 607/608/609）
					if (pOwner != entry.Owner)
						continue;
					break;
				case CountTarget_All: // 全部
				default:
					break;
				}

				++count;
			}

		return count;
		}

		// -------------------------------------------------------------------
		// 每帧渲染（Hook: 0x6D4455 Tactical 渲染循环）
		// -------------------------------------------------------------------

		// 标准 HSV→RGB（H/S/V 各 0-255）：不依赖引擎 0x517440 的调用约定，
		// 避免寄存器/栈约定出错导致颜色错乱。玩家颜色 BaseColor 存的是
		// [Colors] 段的 HSV 原始值（Read_Colors → 0x474C70 原样存储，
		// ColorScheme 构造 0x68C710 原样复制到 +0x308），标准转换即可还原。
		static ColorStruct HsvToRgb(byte h, byte s, byte v)
		{
			const float H = (h * 360.0f) / 255.0f; // 0-360°
			const float S = s / 255.0f;
			const float V = v / 255.0f;
			const float C = V * S;
			const float X = C * (1.0f - fabsf(fmodf(H / 60.0f, 2.0f) - 1.0f));
			const float m = V - C;
			float r = 0.0f, g = 0.0f, b = 0.0f;
			if (H < 60.0f)      { r = C; g = X; }
			else if (H < 120.0f) { r = X; g = C; }
			else if (H < 180.0f) { g = C; b = X; }
			else if (H < 240.0f) { g = X; b = C; }
			else if (H < 300.0f) { r = X; b = C; }
			else                 { r = C; b = X; }
			ColorStruct out;
			out.R = static_cast<byte>((r + m) * 255.0f);
			out.G = static_cast<byte>((g + m) * 255.0f);
			out.B = static_cast<byte>((b + m) * 255.0f);
			return out;
		}

		// 触发所属方的玩家颜色（引擎 DrawText 的 COLORREF 格式）：
		//   House->ColorSchemeIndex → ColorScheme::Array[idx]->BaseColor（HSV）
		//   → 0x517440 转 RGB → 手动转 16 位 RGB565。
		//   ★ 不要用 Windows RGB()（32 位 0x00BBGGRR），也不要用
		//   Drawing::RGB_To_Int（它按运行时位深输出，32 位色深下低 16 位
		//   截断会红蓝错乱）——YR DrawText 的 COLORREF 是固定 16 位
		//   RGB565（YRpp COLOR_RED=0xF800、COLOR_WHITE=0xFFFF 佐证）。
		//   取不到（空所属方 / 越界 / 空 scheme）时回退白色。
		static int HouseColor(HouseClass* pHouse)
		{
			if (!pHouse)
				return COLOR_WHITE;
			const int idx = pHouse->ColorSchemeIndex;
			if (idx < 0 || idx >= ColorScheme::Array.Count)
				return COLOR_WHITE;
			ColorScheme* pScheme = ColorScheme::Array.Items[idx];
			if (!pScheme)
				return COLOR_WHITE;
			ColorStruct rgb = HsvToRgb(
				pScheme->BaseColor.R, pScheme->BaseColor.G, pScheme->BaseColor.B);
			// 一次性日志：核对 BaseColor 原始值（HSV）与转换结果
			static bool s_colorLogged = false;
			if (!s_colorLogged)
			{
				s_colorLogged = true;
				FS_LOG("[FallingStars] UnitCounter: 所属方颜色 idx=%d BaseColor=(%d,%d,%d) -> RGB=(%d,%d,%d)\n",
					idx, pScheme->BaseColor.R, pScheme->BaseColor.G, pScheme->BaseColor.B,
					rgb.R, rgb.G, rgb.B);
			}
			return ((rgb.R & 0xF8) << 8) | ((rgb.G & 0xFC) << 3) | (rgb.B >> 3);
		}

		static void RenderEntry(const Entry& entry, int order, int count)
		{
		DSurface* pSurface = DSurface::Composite;
		if (!pSurface)
			return;

		const int screenW = pSurface->GetWidth();
		const int screenH = pSurface->GetHeight();
		if (screenW <= 0 || screenH <= 0)
			return;

		// 组装显示文本：有自定义文本(CSF)用自定义，否则默认「当前数量：」
		const wchar_t* pPrefix = nullptr;
		if (entry.Text[0])
			pPrefix = StringTable::LoadString(entry.Text); // 520/521 共用：P2 = 自定义显示文本
		else
			pPrefix = L"当前数量：";                          // 无自定义文本时的默认文本

		wchar_t buffer[256] = { 0 };
		if (pPrefix && *pPrefix)
			swprintf(buffer, 256, L"%s %d", pPrefix, count);
		else
			swprintf(buffer, 256, L"%d", count);

		// ---- 右下角布局（仿超级武器倒计时贴底边栏排列）----
		// 1) 右边界 = 战术视图右边缘(侧栏左缘) - 边距：计数器落在侧栏左侧的游玩区，
		//    不与侧栏及其中【超级武器倒计时】重叠。
		//    侧栏宽度在 YRpp 的 SidebarClass 中无 Width 成员，故改用战术视图全局边界
		//    view_bound(0xB0CE28, RectangleStruct)：其 X+Width 即侧栏左缘。
		// 2) 底边 = 可见区底部(即命令栏顶部)之上贴边绘制：DSurface::ViewBounds(0x886FA0)
		//    是不含命令栏的可见区（640x480 逻辑分辨率下 = (0,0,640,400)，命令栏高 80px），
		//    其 Y+Height 即命令栏顶部。仿 SW 倒计时贴底边栏，不做任何特殊避让。
		const int rightMargin = 10;
		int rightEdge;
		{
			DEFINE_NONSTATIC_REFERENCE(RectangleStruct, view_bound, 0xB0CE28);
			rightEdge = view_bound.X + view_bound.Width - rightMargin;
		}
		if (rightEdge <= 0 || rightEdge > screenW)
			rightEdge = screenW - rightMargin;

		int baseY = DSurface::ViewBounds.Y + DSurface::ViewBounds.Height - 20; // = 380，命令栏顶部上方 20px
		if (baseY <= 0 || baseY > screenH)
			baseY = screenH - 100; // 兜底：命令栏高 80 + 20

		// 3) 逐行向上堆叠：order 从 0 开始，第 0 行贴着命令栏之上，
		//    其余依次上移，互不重叠。
		const int lineHeight = 20;
		RectangleStruct dim = Drawing::GetTextDimensions(buffer, { 0, 0 }, 0, 0, 0);
		int posX = rightEdge - dim.Width;
		if (posX < 0) posX = 0;
		int posY = baseY - order * lineHeight;
		if (posY < 0) posY = 0;

		// 黑色背景框（避免文字在画面里突兀），文字用【触发所属方的玩家颜色】渲染。
		RectangleStruct bg = { posX - 4, posY - 2, dim.Width + 8, dim.Height + 4 };
		pSurface->FillRect(&bg, COLOR_BLACK);
		const int color = HouseColor(entry.Owner);
		pSurface->DrawText(buffer, posX, posY, color);
		}

	void RenderAll()
	{
		// 注意：CurrentPlayer 是 DEFINE_REFERENCE(HouseClass*, ...)，
		// 返回的就是非 const 指针；Entry::Owner 需要 HouseClass*，不能加 const。
		HouseClass* pPlayer = HouseClass::CurrentPlayer;
		if (!pPlayer)
			return;

		// 一次性诊断日志：确认渲染 hook 生效（DebugView 可见）
		static bool s_bRenderReadyLogged = false;
		if (!s_bRenderReadyLogged)
		{
			s_bRenderReadyLogged = true;
			FS_LOG("[FallingStars] UnitCounter: 渲染 hook 生效，当前玩家=%p，已登记计数器=%zu 个\n",
				pPlayer, Entries.size());
		}

#ifndef FS_STABLE
		// -------------------------------------------------------------------
		// 兜底 2：仿 Phobos 版本号显示 —— 左下角红字黑底，显示 FallingStars v0.1a
		//   用 Phobos GScreenClass_DrawText 验证过的方式：FillRect 黑底 +
		//   3 参数 DrawText（内部 NoShadow）。看到这行 = 渲染 hook + 绘制全通。
		//   ★ 稳定应用版（FS_STABLE）编译掉：玩家侧不显示版本号，版本信息
		//     只以内嵌 VERSIONINFO 资源（EXs.0.1）形式存在于 DLL 内部。
		// -------------------------------------------------------------------
		{
			DSurface* pSurface = DSurface::Composite;
			if (pSurface)
			{
				const int screenW = pSurface->GetWidth();
				const int screenH = pSurface->GetHeight();
				if (screenW > 0 && screenH > 0)
				{
				wchar_t text[96] = { 0 };
				swprintf(text, 96, L"FallingStars v0.1a");
					RectangleStruct dim = Drawing::GetTextDimensions(text, { 0, 0 }, 0, 0, 0);
					// 左下角（避开右上角 Phobos 版本号的区域），并抬高到命令栏之上
					RectangleStruct bg = { 10, screenH - dim.Height - 72, dim.Width + 10, dim.Height + 10 };
					pSurface->FillRect(&bg, COLOR_BLACK);
					pSurface->DrawText(text, bg.X + 5, bg.Y + 5, COLOR_RED);
				}
			}
		}
#endif // !FS_STABLE

		int order = 0;
		for (Entry& entry : Entries) // 非 const：归零时置 Defunct 锁存
		{
			if (entry.Owner != pPlayer)
				continue;

			// 锁存隐藏：一旦该计数器统计数量曾归零，即永久不再绘制（不占行位），
			// 之后即使指定类型单位重新出现也不恢复 —— 只有触发器再次触发
			// （HandleAction 覆盖更新）才会重置 Defunct 重新显示。
			if (entry.Defunct)
				continue;

			const int count = CountUnits(entry);
			if (count <= 0)
			{
				entry.Defunct = true; // 归零 → 锁定隐藏
				continue;
			}

			RenderEntry(entry, order, count);
			++order;
		}

		// 全量行为日志：逐帧统计【所有】计数器条目（不受"当前玩家"过滤），
		// 仅在状态变化或每 300 帧心跳时落盘 —— 完整记录每个计数器随时间的行为，
		// 又避免每帧刷屏拖慢帧率（满帧率时也不写盘）。
		static std::unordered_map<const Entry*, int> s_lastCount;
		static std::unordered_map<const Entry*, int> s_lastDefunct;
		static bool s_firstRender = true;
		static int s_frame = 0;
		++s_frame;
		const bool heartbeat = (s_frame % 300 == 0);
		for (const Entry& e : Entries)
		{
			const int c = CountUnits(e);
			const int def = e.Defunct ? 1 : 0;
			const int lc = s_lastCount[&e];
			const int ld = s_lastDefunct[&e];
			if (s_firstRender || heartbeat || lc != c || ld != def)
			{
				const char* techName = "";
				if (e.Kind == Action_CountByTechnoType
					&& e.TechnoTypeIndex >= 0
					&& e.TechnoTypeIndex < TechnoTypeClass::Array.Count)
				{
					techName = TechnoTypeClass::Array.Items[e.TechnoTypeIndex]->ID;
				}
				FS_LOG("[FallingStars] UnitCounter: 帧%d 条目 Owner=%p(=%s) 类型=%d 目标=%d 种类=%d 科技=%d(%s) Defunct=%d count=%d -> %s\n",
					s_frame, e.Owner,
					e.Owner == pPlayer ? "当前玩家" : "其它",
					e.Kind, e.Target, e.Category, e.TechnoTypeIndex, techName,
					def, c,
					(e.Owner == pPlayer && !e.Defunct && c > 0) ? "显示" : "隐藏");
				s_lastCount[&e] = c;
				s_lastDefunct[&e] = def;
			}
		}
		s_firstRender = false;
	}

		// ===================================================================
		// 触发条件（事件）607/608/609「单位数量判定」
		//   607 数量等于 / 608 数量大于 / 609 数量小于
		// 统计"触发器所属方"拥有的指定单位种类数量，与 P2 比较；不绘制任何
		// 计数器，仅作为触发条件：满足数量关系时该触发器判定成立。
		// 参数：P1=单位种类(数值0-5,类型6)  P2=比较数值N(类型6)  P3/P4弃用。
		// 所属方：直接取 HasOccured 的 pHouse（触发器所属方），无需配置。
		// 集成点：TriggerClass::HaveEventsOccured（TEventClass::HasOccured
		// 0x71E940 的调用方；HasOccured 已被 Ares/Phobos 占用，不能在其上挂接）。
		// ===================================================================
		std::map<TEventClass*, CountCondition> s_eventConditions;

		// 复用 520/521 的统计内核：用临时 Entry 承载过滤条件后调用 CountUnits。
		// pHouse 每次求值实时传入（不缓存）：同一事件可能被多个触发器实例共享，
		// 只有"当前触发器"的所属方才是正确的统计对象。
		bool EvaluateCountCondition(const CountCondition& cond, HouseClass* pHouse)
		{
			Entry tmp;
			tmp.Owner = pHouse ? pHouse : (cond.Owner ? cond.Owner : HouseClass::CurrentPlayer);
			tmp.Kind = Action_CountByCategory; // 按单位种类统计（同动作 520）
			tmp.Category = cond.Category;
			tmp.Target = CountTarget_Self;     // 只统计触发所属方自身
			const int count = CountUnits(tmp);

			switch (cond.Op)
			{
			case Cmp_Equal:    return count == cond.CompareValue;
			case Cmp_Greater:  return count >  cond.CompareValue;
			case Cmp_Less:     return count <  cond.CompareValue;
			}
			return false;
		}

		// 前向声明：FA2 统计目标编号(0敌/1己/2盟/3全/4指定) → 内部 CountTarget 枚举。
		static CountTarget MapCountTarget(int raw);

		// 参数捕获逻辑见下方 BuildConditionFromEvent：P1=单位种类→Value、P2=比较数值→String(atoi)；
		// 所属方由 HasOccured 的 pHouse 直接给出（即触发器所属方）。
		//
		// 从运行时已加载的 TEventClass 读取 607/608/609 的 2 个参数（惰性，由求值 hook 调用）。
		// 两个参数都是类型码 6 的数值，引擎按通用事件路径加载：
		//   P1（单位种类）→ Value：0全部/1载具/2步兵/3飞行器/4舰船/5建筑（同动作 520 的 CountCategory，直接映射 Category 枚举）
		//   P2（比较数值 N）→ String（十进制数字串，atoi 得到 N）
		// 所属方：直接用触发器的所属方（pHouse，来自 HasOccured 调用方），不另行配置。
		static CountCondition BuildConditionFromEvent(TEventClass* pEvent, HouseClass* pHouse)
		{
			CountCondition c;
			const int ek = static_cast<int>(pEvent->EventKind);
			c.Op = (ek == Event_CountGreater) ? Cmp_Greater
				 : (ek == Event_CountLess)    ? Cmp_Less
				 : Cmp_Equal;

			// 所属方：用触发器的所属方（HasOccured 的 pHouse）；取不到时回退当前玩家。
			c.Owner = pHouse ? pHouse : HouseClass::CurrentPlayer;

			// —— P1：单位种类（数值 0-5）——
			const int v = pEvent->Value;
			const char* s = pEvent->String;
			int cat = v;
			if (cat < Category_All || cat > Category_Building)
				cat = Category_All; // 越界兜底 → 全部

			// —— P2：比较数值 N（数字串）——
			const int cmp = (s && *s) ? atoi(s) : 0;

			c.Category = cat;
			c.CompareValue = cmp;
			c.Initialized = true;

			FS_LOG("[FallingStars] CountEvent Raw: kind=%d House=%p Value=%d String=\"%s\"\n",
				ek, pHouse, v, s ? s : "");
			FS_LOG("[FallingStars] CountEvent Active: category=%d cmp=%d op=%d owner=%p\n",
				c.Category, c.CompareValue, c.Op, c.Owner);
			return c;
		}

		// -------------------------------------------------------------------
		// 动作 520/521 处理：登记 / 更新计数器
		// -------------------------------------------------------------------
		// FA2 统计目标编号 → 内部枚举（FA2: 0敌方/1己方/2同盟/3全部/4指定所属方）。
		// 注意：内部 CountTarget 枚举(0=Enemy/1=Ally/2=Specified/3=All)与 FA2 编号
		// 不一致（FA2 的 2 是同盟而非指定、4 才是指定所属方），绝不能 static_cast。
		static CountTarget MapCountTarget(int raw)
		{
			switch (raw)
			{
			case 0:  return CountTarget_Enemy;
			case 1: case 2: return CountTarget_Ally; // 己方/同盟 → 盟友（含自身）
			case 3:  return CountTarget_All;
			case 4:  return CountTarget_Specified;
			default: return CountTarget_Enemy;
			}
		}

		void HandleAction(TActionClass* pAction, HouseClass* pHouse)
		{
			if (!pAction)
				return;

			// 兼容「无所属方」的触发：此时以当前玩家作为计数器归属方
			if (!pHouse)
				pHouse = HouseClass::CurrentPlayer;
			if (!pHouse)
				return;

			const int kind = static_cast<int>(pAction->ActionKind);

			Entry entry;
			entry.Owner = pHouse;
			entry.Kind = kind;
			// 注意：P1 是【模式位】（TActionClass::LoadFromINI 的 token2 会做 switch
			// 分派，0-11 走不同参数解析；只有 =4 时走"文本模式"：P2→Text、P3→Param3、
			// P4→Param4…）。因此统计目标从 P3 读（Param3），不要用 Value。
			strncpy(entry.Text, pAction->Text, sizeof(entry.Text) - 1); // P2 字符串 → Text 字段
			entry.Text[sizeof(entry.Text) - 1] = '\0';

		if (kind == Action_CountByTechnoType)
		{
			// 521：P3=统计目标, P4=指定所属方, P5=科技类型（→Param3/Param4/Param5）
			entry.RawTarget = pAction->Param3; // FA2 原始编号（0敌方/1己方/2同盟/3全部/4指定），预留
			entry.Target = MapCountTarget(pAction->Param3);
			entry.SpecifiedHouseIdx = pAction->Param4;

			// 科技类型：优先用加载期截获的注册名（FA2 存的是注册名如 MTNK，
			// atoi 会变 0），按名在 TechnoTypeClass::Array 里找真实索引（仿事件 61）；
			// 截获失败（旧地图/其它编辑器）时回退 Param5 数值索引。
			entry.TechnoTypeIndex = pAction->Param5;
			const auto it = s_techNames.find(pAction);
			if (it != s_techNames.end() && !it->second.empty())
			{
				const int idx = FindTechnoIndexByName(it->second.c_str());
				if (idx >= 0)
					entry.TechnoTypeIndex = idx;
			}
		}
			else
			{
				// 520：P3=统计目标, P4=指定所属方, P5=单位种类（→Param3/Param4/Param5）
				//   ★ 布局与 521 统一：P4=所属方下拉、P5=种类/科技（FA2 类型码 0,13,6,2,6）
				entry.RawTarget = pAction->Param3; // FA2 原始编号，预留
				entry.Target = MapCountTarget(pAction->Param3);
				entry.SpecifiedHouseIdx = pAction->Param4;
				entry.Category = pAction->Param5;
			}

			// 参数合法性兜底：未配置 / 越界时回退默认值，保证不崩溃
			if (entry.Target < CountTarget_Enemy || entry.Target > CountTarget_All)
				entry.Target = CountTarget_Enemy;
			if (entry.Category < Category_All || entry.Category > Category_Building)
				entry.Category = Category_All;

			// 521：解析科技类型索引对应的【注册名】（如 HARP），便于日志确认匹配目标
			const char* technoName = "";
			if (kind == Action_CountByTechnoType
				&& entry.TechnoTypeIndex >= 0
				&& entry.TechnoTypeIndex < TechnoTypeClass::Array.Count)
			{
				technoName = TechnoTypeClass::Array.Items[entry.TechnoTypeIndex]->ID;
			}

			// 同一所属方 + 同一动作 + 同一【统计参数】→ 覆盖更新（重置 Defunct 恢复显示）；
			// 统计参数不同 → 追加，允许多个计数器并存。
			// 身份 key：520 按单位种类、521 按科技类型，且【统计目标】与【指定所属方】
			// 也参与——否则"指定(欧洲联盟)载具"会被"指定(PsiCorps)载具"合并覆盖。
			// 注意：不能用 Text 作身份 key —— 两个 520 都用默认文本时 Text 相同会被合并。
			for (Entry& e : Entries)
			{
				if (e.Owner == pHouse && e.Kind == kind
					&& e.Target == entry.Target
					&& (kind == Action_CountByTechnoType
						? e.TechnoTypeIndex == entry.TechnoTypeIndex
						: e.Category == entry.Category)
					&& e.SpecifiedHouseIdx == entry.SpecifiedHouseIdx)
				{
					e = entry; // entry.Defunct 默认为 false → 触发器再次触发即恢复显示
					FS_LOG("[FallingStars] UnitCounter: 更新 [%s] (类型=%d 目标=%d 种类=%d 科技=%d 名=%s)\n",
						entry.Text, entry.Kind, entry.Target, entry.Category, entry.TechnoTypeIndex, technoName);
					return;
				}
			}

			Entries.push_back(entry);
			FS_LOG("[FallingStars] UnitCounter: 新增 [%s] (类型=%d 目标=%d 种类=%d 科技=%d 名=%s)\n",
				entry.Text, entry.Kind, entry.Target, entry.Category, entry.TechnoTypeIndex, technoName);
		}

		void ClearAll()
		{
			FS_LOG("[FallingStars] UnitCounter: 清空全部计数器 (场景加载) Entries=%zu techNames=%zu events=%zu\n",
				Entries.size(), s_techNames.size(), s_eventConditions.size());
			Entries.clear();
			s_techNames.clear();       // 防 TActionClass* 指针跨场景失效
			s_eventConditions.clear(); // 防 TEventClass* 指针跨场景失效
		}

	} // namespace UnitCounter
} // namespace FS

// ---------------------------------------------------------------------------
// Hook 1: TriggerClass::FireActions（0x7265C0）—— 触发器的动作执行入口
//
// 选择此地址而非 TActionClass::Execute（0x6DD8B0），原因：
//   - Phobos 已 hook 0x6DD8B0 处理其 500+ 自定义动作；同地址双 DLL 的
//     DEFINE_HOOK 会互相覆盖（Syringe 只保留一个跳板），导致本模块失效。
//   - FireActions（0x7265C0）是「触发条件满足→执行全部动作」的入口，
//     Phobos 未占用；在这里遍历动作链表即可拿到 520/521 及其参数，
//     同时原版 FireActions 仍会照常执行（520/521 在原版 Execute 中无操作）。
//
// 若 0x7265C0 的 prologue 长度与 size=0x6 不符导致崩溃，
// 用 IDA 确认后调整 size；或临时改回 0x6DD8B0（仅限未装 Phobos 时）。
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x7265C0, TriggerClass_FireActions_UnitCounter, 0x6)
{
	GET(TriggerClass*, pTrigger, ECX);
	if (!pTrigger || !pTrigger->Type)
		return 0;

	for (TActionClass* pAction = pTrigger->Type->FirstAction;
		pAction; pAction = pAction->NextAction)
	{
		const int kind = static_cast<int>(pAction->ActionKind);

		// 520/521：单位计数器（右下角，按所属方统计，仅该方可见）
		if (kind == FS::UnitCounter::Action_CountByCategory
			|| kind == FS::UnitCounter::Action_CountByTechnoType)
		{
			FS_LOG("[FallingStars] UnitCounter: FireActions 分发 动作=%d 触发=%p 所属方=%p 文本=%s\n",
				kind, pTrigger, pTrigger->House, pAction->Text);
			FS::UnitCounter::HandleAction(pAction, pTrigger->House);
		}
		// 526：彩色字幕（屏幕中央文本框，仿触发结果 11，文本内《编号》控制颜色）
		else if (kind == FS::ColoredSubtitle::Action_ColoredSubtitle)
		{
			FS::ColoredSubtitle::HandleAction(pAction);
		}
	}

	return 0; // 原版 FireActions 照常执行
}

// ---------------------------------------------------------------------------
// Hook 2: TriggerTypeClass::LoadFromINIList（0x7275D0）—— 场景加载读取触发器
// 定义时清空上一局的计数器（Phobos 未占用此地址）。
// size=0x6：此处 prologue 为 `51`(push ecx, 1B) + `A1 C8 58 7F 00`
// (mov eax,[0x007F58C8], 5B)，共 6 字节完整指令；0x5 会切断 mov 导致
// SyringeEx 跳板错位（拼出 mov eax,[0xE97F58C8]）→ 读图即崩。
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x7275D0, TriggerTypeClass_LoadFromINIList_UnitCounter, 0x6)
{
	FS::UnitCounter::ClearAll();
	FS::ColoredSubtitle::ClearAll(); // 彩色字幕同为内存登记表，跨场景一并失效
	return 0;
}

// ---------------------------------------------------------------------------
// Hook 3: GScreenClass::Render（0x4F4480）内、统计面板之后、鼠标绘制之前（0x4F4583）
// —— 每帧绘制计数器（UI 之上、鼠标指针之下）
//
// 为什么选 0x4F4583：
//   - 反汇编 GScreenClass::Render(0x4F4480..0x4F45A8) 确认每帧固定执行段：
//       0x4F457E call 0x55F1E0        ← 统计面板
//       0x4F4583 mov ecx,[0x887640]   ← 本 hook 点（载入 WWMouseClass::Instance）
//       0x4F4593 call [WWMouse+0x3C]  ← 鼠标指针绘制到 Composite！
//       0x4F459A call [this+0x44]     ← SetCursor
//       0x4F45A8 ret
//     （0x4F44C9 的 jne 只跳过战术视图渲染；0x4F4547 之后的统计面板/鼠标/SetCursor
//      每帧无条件执行，不受脏标记影响 → 计数器每帧稳定刷新。）
//   - 在此绘制计数器：晚于统计面板等全部 UI（文字压在 UI 最上层），
//     早于鼠标指针绘制（鼠标画在其后 → 鼠标永远在计数器之上）。
//   - 之前用的 0x4F45B0（帧尾鼠标渲染函数入口）与 0x4F4780（DoBlit）都在
//     鼠标绘制(0x4F4593)之后 → 计数器盖鼠标；0x4F4583 是唯一同时满足
//     "UI 之后 + 鼠标之前" 的稳定点。
//   - size=0x6：`8B 0D 40 76 88 00`(mov ecx,[0x887640], 6B) 完整指令，
//     跳回 0x4F4589 后原函数照常执行。trampoline 的 pushad/popad 保护寄存器，
//     RenderAll() 不会破坏本函数（fastcall）的 ecx/edx。
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x4F4583, GScreenClass_Render_UnitCounter, 0x6)
{
	// 共享渲染点（UI 之上、鼠标之下）：各模块各自判空、各自绘制，
	// 互不读写对方状态 —— 任一模块无条目即空转，互不影响。
	FS::UnitCounter::RenderAll();     // 右下角：单位计数器（520/521）
	FS::ColoredSubtitle::RenderAll(); // 屏幕中央：彩色字幕（526）
	return 0;
}

// ---------------------------------------------------------------------------
// Hook 4: 文本绘制原语（0x4A61C0）—— 右下角底部文本让位（仿超级武器倒计时）
//
// 需求③：当当前玩家存在可见计数器时，计数器占用【屏幕最底行】，
// 右下角底部区域的其它文本（超级武器倒计时列表等）自动让位向上平移，
// 完全仿照"SW 倒计时之间从底部向上逐行堆叠"的让位机制。
//
// 实现要点：
//   - 0x4A61C0 是引擎统一的文本绘制原语，所有 UI 文本（统计面板、SW 列表、
//     底部消息等）都经它绘制。它的三个 rect 参数（位置/裁剪/包围盒）位于
//     栈槽 [esp+0x4] / [esp+0x10] / [esp+0x14]（11 参数签名，各调用者一致）。
//   - 【坐标过滤】：只平移 Y 在屏幕底部 130px 内的文本。超级武器倒计时列表
//     画在右下角、从底部向上排 → 必被命中；统计面板画在屏幕中部（Y=300-382、
//     X≈100）不受影响。
//   - 【第一道排除】：统计面板 0x55F1E0 的最后一行（Y=382/500）也接近底部，
//     用返回地址 [0x55F1E0,0x55F685) 排除，避免误移统计信息。
//   - 平移量 = N*20px（N = 当前玩家实际显示的计数器行数，与 RenderEntry 行高一致）。
//     计数器本身画在 GScreenClass::Render 内（0x4F4583，Hook 3），占用最底行；
//     被上移的文本与它不再重叠。
//   - size=0xA：覆盖 prologue 两条完整指令 `mov eax,[esp+8]`(4B) +
//     `sub esp,0x40C`(6B)，跳回 0x4A61CA 从 `test eax,eax` 继续，原函数不受影响。
//     ⚠️ 不要用 0x4：JMP 为 5 字节，size=0x4 时跳转会覆盖 sub 的首字节(81)，
//     跳回后从指令中间(0x4A61C5 的 EC)解码成 in al,dx 特权指令 → EXCEPTION_PRIV_INSTRUCTION
//     （即 2026-08-23 的崩溃 0x004A61C5）。size 必须 ≥5 且落在完整指令边界。
// ---------------------------------------------------------------------------
namespace
{
	// 统计当前玩家实际显示的计数器数量（驱动底部文本让位行数）。
	// 数量为 0 的条目已被"消除"（RenderAll 不绘制且置 Defunct 锁存），
	// 故只统计 !Defunct 且 CountUnits>0 的条目，与 RenderAll 的显示逻辑保持一致。
	static int VisibleCounterCountForCurrentPlayer()
	{
		HouseClass* pPlayer = HouseClass::CurrentPlayer;
		if (!pPlayer)
			return 0;
		int n = 0;
		for (const auto& e : FS::UnitCounter::Entries)
		{
			if (e.Owner == pPlayer && !e.Defunct && FS::UnitCounter::CountUnits(e) > 0)
				++n;
		}
		return n;
	}
}

DEFINE_HOOK(0x4A61C0, TextDraw_UnitCounter_Shift, 0xA)
{
	// 返回地址（调用方地址）位于 [ESP+0]
	GET_STACK(DWORD, retAddr, 0x0);

	// 第一道过滤：排除统计面板 0x55F1E0（画在屏幕中部，其最后一行 Y 接近
	// 底部阈值但不属于右下角 SW 列表，不应让位）。
	if (retAddr >= 0x55F1E0 && retAddr < 0x55F685)
		return 0;

	const int n = VisibleCounterCountForCurrentPlayer();
	if (n <= 0)
		return 0;

	DSurface* pSurface = DSurface::Composite;
	if (!pSurface)
		return 0;
	const int screenH = pSurface->GetHeight();
	if (screenH <= 0)
		return 0;

	const int shift = n * 20; // 上移像素数（与 RenderEntry 行高 20 一致）

	// 全量日志：首次激活让位时记录一次（每帧多次调用的原语，不逐次打印以免刷屏）
	static bool s_shiftLogged = false;
	if (n > 0 && !s_shiftLogged)
	{
		s_shiftLogged = true;
		FS_LOG("[FallingStars] UnitCounter: Hook4 文本让位激活 N=%d shift=%dpx\n", n, shift);
	}

	// 第二道过滤：只平移【屏幕底部区域】的文本（Y 在底部 130px 内），
	// 即右下角的超级武器倒计时列表等——它们像 SW 倒计时之间一样逐行让位。
	const int bottomLine = screenH - 130;

	// 三个 rect 形参（位置 / 裁剪 / 包围盒），统一上移，保证整块平移
	GET_STACK(RectangleStruct*, pRectA, 0x4);
	GET_STACK(RectangleStruct*, pRectB, 0x10);
	GET_STACK(RectangleStruct*, pRectC, 0x14);
	if (pRectA && pRectA->Y >= bottomLine) pRectA->Y -= shift;
	if (pRectB && pRectB->Y >= bottomLine) pRectB->Y -= shift;
	if (pRectC && pRectC->Y >= bottomLine) pRectC->Y -= shift;

	return 0; // 执行被窃取的 mov 指令后继续原函数
}

// ---------------------------------------------------------------------------
// Hook 11（0x6DD602）：TActionClass::LoadFromINI —— 强制 FallingStars 文本动作
// 走"文本模式"（P1=4），使文本参数 P2 必定存入 Text 字段。
//
// 背景：LoadFromINI 按 P1（token2，动作行第 2 个参数）分派参数解析：
//   P1=4（case 4 @ 0x6DD6D0）→ P2 strncpy 进 [ebp+0x6d]（Text 字段，文本模式）；
//   P1=0（case 0 @ 0x6DD614）→ P2 存 [ebp+0x90]（非 Text）→ 文本被丢进 Value，
//   显示为空。地图作者不再需要手动把 P1 填 4（520/521/525/526 统一强制）。
//
// size=0x5：`83 FB 0B 77 24`(cmp ebx,0xb; ja 0x6dd731) 完整指令，
// 跳回 0x6DD607（jmp [ebx*4+0x6dd880] 跳转表）。
// EBX = P1（token2 atoi 结果），EBP = this（TActionClass*）。
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x6DD602, TActionClass_LoadFromINI_ForceTextMode, 0x5)
{
	GET(TActionClass*, pAction, EBP);
	if (!pAction)
		return 0;

	const int kind = static_cast<int>(pAction->ActionKind);
	// FallingStars 文本动作：P2 一律进 Text 字段（不再要求地图 P1=4）
	if (kind == FS::UnitCounter::Action_CountByCategory       // 520
		|| kind == FS::UnitCounter::Action_CountByTechnoType   // 521
		|| kind == 525                                          // 字幕显示（示范包方案B，转调原版动作11）
		|| kind == FS::ColoredSubtitle::Action_ColoredSubtitle) // 526
	{
		R->EBX(4); // 强制文本模式 → P2 进 Text
	}

	return 0; // trampoline 执行 cmp ebx,0xb; ja —— EBX 已按需改为 4
}

// ---------------------------------------------------------------------------
// Hook 5: TActionClass::LoadFromINI（0x6DD5B0）默认路径 P5 的 atoi 调用点（0x6DD768）
// —— 截获 521 的科技类型注册名（仿事件 61「科技类型不存在」的按名匹配）
//
// 背景：FA2 的科技类型下拉（参数类型 46）在地图 [Actions] 里写的是【注册名
// 字符串】（如 MTNK），但引擎加载时把 P5 交给 atoi → atoi("MTNK")=0，注册名
// 丢失，Param5 存 0。之前 DLL 用 Param5 索引查 TechnoTypeClass::Array[0]
// → 永远匹配到 Array 第一个类型（如 GACNST 盟军建造厂），计数全错。
//
// 反汇编定位（默认路径 0x6DD731 起的顺序解析）：
//   0x6DD738 strtok → P3；0x6DD74D strtok → P4；0x6DD762 strtok → P5(token6)
//   0x6DD767 push eax        ← P5 原始字符串指针压栈
//   0x6DD768 call atoi       ← 本 hook 点（替换此 5 字节 call）
//   0x6DD774 mov [ebp+0x3c], eax  ← Param5 = atoi(P5)
// 故本 hook 入口时 [ESP+0] 正是 P5 的原始字符串（atoi 之前），EBP = this。
//
// 处理：仅当 ActionKind==521 时，把该字符串存入 s_techNames（按 TActionClass*
// 索引）；HandleAction 再据此在 TechnoTypeClass::Array 中按名查找真实索引。
// 对 520 及其它动作（P5 是数字）不捕获，不影响原解析。
//
// size=0x5：`call 0x7c9bfd`（E8 rel32）完整指令，跳回 0x6DD76D 继续。
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x6DD768, TActionClass_LoadFromINI_CaptureTechName, 0x5)
{
	// this（TActionClass*）在 EBP（0x6DD5B7: mov ebp,ecx，之后未改）
	GET(TActionClass*, pAction, EBP);
	if (!pAction)
		return 0;

	// 只对 521 动作捕获；其它动作 P5 是数值/无关，不处理
	if (static_cast<int>(pAction->ActionKind) != FS::UnitCounter::Action_CountByTechnoType)
		return 0;

	// [ESP+0] = 0x6DD767 push 的 P5 原始 token 字符串（注册名，如 "MTNK"）
	GET_STACK(char*, pToken, 0x0);
	if (pToken && *pToken)
	{
		FS_LOG("[FallingStars] UnitCounter: 截获521科技类型注册名 pAction=%p 名=%s\n", pAction, pToken);
		FS::UnitCounter::CaptureTechName(pAction, pToken);
	}

	return 0; // 执行被窃取的 call atoi 后继续原函数
}

	// ---------------------------------------------------------------------------
	// Hook 6（已启用）：TriggerClass 事件求值循环里的 HasOccured 调用点（0x726540）
	//   —— 单位数量判定事件 607/608/609 的求值入口。
	//
	// 为何挂在【调用点】而非 HasOccured 入口（0x71E940）：
	//   - Phobos 的 TEventClass_Execute 已挂在 0x71E940（size 0x5，处理其 500+ 自定义
	//     事件），同地址双 DLL 的 DEFINE_HOOK 会互相覆盖（Syringe 只保留一个跳板），
	//     谁后加载谁生效、另一个失效。Ares 又挂在入口 +9（0x71E949）。入口附近已被
	//     占满，本 DLL 不能再挤进去。
	//   - 0x726540 是引擎里【唯一】调用 HasOccured 的地方（TriggerClass 事件求值循环，
	//     由反汇编扫描 call 0x71e940 确认），Phobos/Ares 均未占用。本 hook 替换的正是
	//     这条 `call 0x71e940`（E8 rel32，5 字节）：
	//       · 非本 DLL 事件 → return 0，trampoline 执行原始 call（进入 HasOccured，
	//         Phobos/Ares 照常处理，正常 ret 0x18 清栈）；
	//       · 本 DLL 事件 → 直接算出布尔、清理 6 个栈参数、跳到 call 之后（0x726545）。
	//
	// 栈布局（与 Phobos hook 一致，HasOccured 有 6 个参数，尾部 ret 0x18 由被调方清栈）：
	//   ECX = this（TEventClass*，mov ecx,edi 刚执行）
	//   [esp+0] = 参数1（事件编号 int）   [esp+4] = 参数2（pHouse，触发器所属方）
	//   [esp+8..0x14] = 参数3..6（Object/ActivationFrame/isRepeating/pSource，本 DLL 不用）
	// 提前返回时必须自己 add esp,0x18 清掉 6 个参数（原函数靠 ret 0x18 清），再跳到 0x726545。
	//
	// 参数捕获（惰性，运行时）：
	//   HasOccured 运行时，TEventClass 的 Value/String 已被 LoadFromINI 填好，且 pHouse
	//   （触发器所属方）是第 2 参数，故不必再挂 LoadFromINI，直接首次构造 CountCondition
	//   并缓存。字段映射见 BuildConditionFromEvent（P1=单位种类→Value，P2=数值→String）；
	//   若引擎对未知种类丢弃参数（Value=0 且 String 空），再改用 LoadFromINI(0x71F4E0)
	//   直接读原始 [Events] 行。
	// ---------------------------------------------------------------------------
	DEFINE_HOOK(0x726540, TriggerClass_HasEventsOccurred_CallHasOccured_UnitCounter, 0x5)
	{
		GET(TEventClass*, pEvent, ECX);
		if (!pEvent)
			return 0; // 执行原始 call

		const int kind = static_cast<int>(pEvent->EventKind);

		if (kind != FS::UnitCounter::Event_CountEquals
			&& kind != FS::UnitCounter::Event_CountGreater
			&& kind != FS::UnitCounter::Event_CountLess)
		{
			return 0; // 非本 DLL 事件：trampoline 执行原始 call → HasOccured（Phobos/Ares 处理）
		}

		// pHouse = HasOccured 的第 2 参数（[esp+4]，调用点尚未压入返回地址，故与
		// Phobos 在函数内读的 [esp+8] 差 4 字节）。pHouse 即触发器所属方 —— 事件
		// 607/608/609 用它作为统计对象（不再单独配所属方）。
		GET_STACK(HouseClass*, pHouse, 0x4);

		// 惰性构造并缓存该事件的计数条件（首次命中时解析 Value/String + pHouse）。
		FS::UnitCounter::CountCondition& cond = FS::UnitCounter::s_eventConditions[pEvent];
		if (!cond.Initialized)
		{
			cond = FS::UnitCounter::BuildConditionFromEvent(pEvent, pHouse);
			cond.Initialized = true;
		}

		const bool result = FS::UnitCounter::EvaluateCountCondition(cond, pHouse);
		FS_LOG("[FallingStars] CountEvent: 求值 kind=%d Owner=%p category=%d cmp=%d op=%d => %s\n",
			kind, (pHouse ? pHouse : cond.Owner), cond.Category, cond.CompareValue, cond.Op,
			result ? "TRUE" : "FALSE");

		R->AL(result ? 1 : 0);
		R->ESP(R->ESP() + 0x18); // 清掉 6 个栈参数（原函数用 ret 0x18 清，此处提前返回需手动清）
		return 0x726545;         // 跳到 call 之后（test al, al），正常读取 AL
	}

	// ---------------------------------------------------------------------------
	// Hook 9（关键修复）：GetAttachType 调用点（0x7271F4，替换 call 0x71f680）
	//   —— 给 607/608/609 事件补 AttachType 位，对齐 Phobos 的 TEventClass_GetFlags
	//   （0x7271F9，Phobos 已占该地址，不能双 DLL 同挂，故挂调用点 0x7271F4）。
	//
	// 背景（参考 Phobos 源码 src/Ext/TEvent/）：
	//   - 引擎 0x7271E0 遍历触发器的 FirstEvent，逐个调 GetAttachType(0x71F680)
	//     OR 累加 AttachType，该汇总决定触发器如何挂载/激活（LogicClass 等）。
	//   - 原版 GetAttachType 对未知事件（608）返回 0；Phobos 的 0x7271F9 hook 只给
	//     其区间 500 <= n < _DummyMaximum(606) 的事件补 0x10（GetFlags 默认值）。
	//   - 我们的 607/608/609 超出 Phobos 区间 → 无人补位 → AttachType 汇总缺位 →
	//     引用它们的触发器（01000045）被引擎跳过、永不求值（本次排查的根因）。
	// 此处替换 GetAttachType 调用本身：607/608/609 → 直接返回 0x10（仿 Phobos），
	// 其余事件 trampoline 走原 GetAttachType。ESI 此时仍指向当前事件
	// （0x7271F1 mov ecx,[esi+0x2c] 之后、0x7271F9 mov esi,[esi+0x28] 之前）。
	// size=0x5：`E8 rel32`(call 0x71f680) 完整指令，跳回 0x7271F9。
	// ---------------------------------------------------------------------------
	DEFINE_HOOK(0x7271F4, GetAttachType_UnitCounter, 0x5)
	{
		GET(TEventClass*, pEvent, ESI); // 当前事件（0x7271F1 用 ESI 读 EventKind 后未变）
		if (pEvent)
		{
			const int kind = static_cast<int>(pEvent->EventKind);
			if (kind >= FS::UnitCounter::Event_CountEquals
				&& kind <= FS::UnitCounter::Event_CountLess)
			{
				R->EAX(0x10); // 仿 Phobos GetFlags 默认值：LogicClass 标志
				return 0x7271F9; // 跳过 GetAttachType，跳到 call 之后（or edi,eax）
			}
		}
		return 0; // 其他事件：trampoline 执行原 call GetAttachType
	}

	// ---------------------------------------------------------------------------
	// Hook 10（关键修复）：0x7264DA（mov al,[esp+0x14]）—— 强制 607-609 事件走事件循环
	//   —— 0x7264E7 的 `test al,al; jne 0x72659a` 在参数3≠0 时跳过整个事件循环
	//      （"无条件满足"路径），导致引用 607/608/609 的触发器（01000045）的事件
	//      永不经过 0x726540（Hook 6）求值。本 hook 改写栈上的参数3 → 0：
	//      trampoline 的 mov al,[esp+0x14] 读到 0 → test al,al 不跳 → 进入事件循环
	//      → Hook 6 正常求值 607-609。不改寄存器、不改栈布局，无副作用。
	//   size=0x8：`8A 44 24 14 53 84 C0 57`(mov al,[esp+0x14]; push ebx;
	//   test al,al; push edi) 完整指令，trampoline 照常执行（栈平衡由原指令保证）。
	// ---------------------------------------------------------------------------
	DEFINE_HOOK(0x7264DA, EventEval_ForceConditional_UnitCounter, 0x8)
	{
		GET(TriggerClass*, pTrig, ESI); // 0x7264C2 mov esi,ecx 后 esi=this
		if (pTrig && pTrig->Type)
		{
			for (TEventClass* ev = pTrig->Type->FirstEvent; ev; ev = ev->NextEvent)
			{
				const int k = static_cast<int>(ev->EventKind);
				if (k >= FS::UnitCounter::Event_CountEquals
					&& k <= FS::UnitCounter::Event_CountLess)
				{
					// 改写栈上参数3 → 0：trampoline 的 mov al,[esp+0x14] 读到 0
					*(DWORD*)(R->ESP() + 0x14) = 0;
					break;
				}
			}
		}
		return 0; // trampoline 执行原 8 字节（mov al,[esp+0x14] + push ebx + test + push edi）
	}

	// ---------------------------------------------------------------------------
	// 模块自注册（免改中央注册表）
	// ---------------------------------------------------------------------------
class UnitCounterModule final : public FS::IModule
{
public:
	static UnitCounterModule& Instance()
	{
		static UnitCounterModule instance;
		return instance;
	}

	const char* Name() const override { return "UnitCounter"; }

	void OnLoad() override
	{
		FS_LOG("[FallingStars] UnitCounter module ready (Trigger Action 520/521: 单位计数显示).\n");
	}
};

REGISTER_MODULE(UnitCounterModule)
