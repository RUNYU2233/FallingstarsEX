// 必须在引入 YRpp 头之前取消 windows.h 的 DrawText 宏：
// 否则 Surface::DrawText 的成员声明会被预处理器改写成 DrawTextW，
// 导致 DSurface::Composite->DrawText(...) 调用报 C2665（与 UnitCounter 同因）。
#include <windows.h>
#undef DrawText

#include "Body.h"

#include <Core/Module.h>
#include <Core/Logging.h>
#include <Core/Macro.h>
#include <FallingStars.h>
#include <StringTable.h>
#include <VocClass.h> // 播放触发结果 11 同款提示音

namespace FS
{
	namespace ColoredSubtitle
	{
		std::vector<Entry> Entries;

		// -------------------------------------------------------------------
		// 43 色【字面 RGB 色表】——按颜色名的直观含义直接对照标准 RGB，
		// 不做任何 HSV/引擎转换（早期引擎转换方案在游戏内颜色异常）。
		//   Teal 按用户认知取"青色"亮青；其余按常见标准色值。
		// 若要微调某个颜色，直接改对应行的三个数即可。
		// -------------------------------------------------------------------
		static const ColorStruct s_stdColors[43] = {
			{   0, 255, 255 }, //  0 Teal      青（亮青）
			{ 255,   0,   0 }, //  1 Red       红
			{ 192, 192, 192 }, //  2 LightGrey 浅灰
			{ 173, 216, 230 }, //  3 LightBlue 浅蓝
			{   0, 128,   0 }, //  4 Green     绿
			{ 128,   0, 128 }, //  5 Purple    紫
			{ 255, 215,   0 }, //  6 Gold      金
			{   0, 128, 255 }, //  7 NeonBlue  霓虹蓝
			{ 255, 165,   0 }, //  8 Orange    橙
			{ 255,   0, 255 }, //  9 Magenta   品红
			{ 128,  70,  27 }, // 10 Russet    赤褐
			{   0, 100,   0 }, // 11 DarkGreen 深绿
			{ 220,  20,  60 }, // 12 Crimson   绯红
			{ 135, 206, 235 }, // 13 Sky       天蓝
			{ 255, 215,   0 }, // 14 FirstText 消息首行（金黄）
			{ 255, 165,   0 }, // 15 SecondText 第二行（橙）
			{   0, 255, 255 }, // 16 ThirdText  第三行（青）
			{   0, 255,   0 }, // 17 FourthText 第四行（绿）
			{ 255, 192, 203 }, // 18 Pink      粉
			{   0,   0, 139 }, // 19 DarkBlue  深蓝
			{ 128, 128,   0 }, // 20 Olive     橄榄
			{ 240, 230, 140 }, // 21 Khaki     卡其
			{ 238, 221, 130 }, // 22 LightGold 浅金
			{ 192, 192, 192 }, // 23 BrightGrey 亮灰
			{ 128, 128, 128 }, // 24 Grey      灰
			{ 139,   0,   0 }, // 25 DarkRed   深红
			{   0, 191, 255 }, // 26 DarkSky   深天蓝
			{  57, 255,  20 }, // 27 NeonGreen 霓虹绿
			{   0,   0,   0 }, // 28 Black     黑（渲染时提亮为 40,40,40，见 GetTextColor）
			{ 255, 255,   0 }, // 29 Yellow    黄
			{ 147, 112, 219 }, // 30 Purple2   紫2
			{ 138,  43, 226 }, // 31 Purple3   紫3
			{ 216, 191, 216 }, // 32 Thistle   蓟
			{ 165,  42,  42 }, // 33 Brown2    棕
			{ 115, 134, 120 }, // 34 Xanadu    绿灰
			{   0, 255,   0 }, // 35 Lime      亮绿
			{   0, 168, 107 }, // 36 Jade      翡翠
			{  54,  69,  79 }, // 37 Charcoal  炭灰
			{ 204, 204, 255 }, // 38 Peri      长春花蓝
			{   0, 255, 255 }, // 39 Aqua      水色
			{   0, 128, 255 }, // 40 AlliedLoad 盟军蓝
			{ 255,   0,   0 }, // 41 SovietLoad 苏军红
			{ 128,   0, 128 }, // 42 ThirdLoad 尤里紫
		};

		// 段颜色种类：-1=默认白，-2=()用 ColorIdx，-3=<>用 AngleColorIdx，-4=[]用 BracketColorIdx
		enum : int
		{
			kSegDefault = -1, // 未括起 → 纯白
			kSegParen   = -2, // () 内 → entry.ColorIdx
			kSegAngle   = -3, // <> 内 → entry.AngleColorIdx
			kSegBracket = -4, // [] 内 → entry.BracketColorIdx
		};

		// 按颜色编号取色（0-42，黑色提亮为 40,40,40；越界回退 FirstText=14）
		static const ColorStruct& GetColorByIdx(int idx)
		{
			if (idx < 0 || idx >= 43)
				idx = 14; // FirstText 兜底
			if (idx == 28)
			{
				// 黑色（28）在纯黑背景上不可见 → 提亮为深灰 40,40,40 以衬托
				static const ColorStruct s_blackLift = { 40, 40, 40 };
				return s_blackLift;
			}
			return s_stdColors[idx];
		}

		// 按段颜色种类取色（默认白 / () / <> / [] / 直接编号）
		static const ColorStruct& GetTextColor(int kind, const Entry& e)
		{
			if (kind == kSegDefault)
			{
				static const ColorStruct s_white = { 255, 255, 255 };
				return s_white;
			}
			if (kind == kSegParen)
				return GetColorByIdx(e.ColorIdx);
			if (kind == kSegAngle)
				return GetColorByIdx(e.AngleColorIdx);
			if (kind == kSegBracket)
				return GetColorByIdx(e.BracketColorIdx);
			return GetColorByIdx(kind);
		}

		void ClearAll()
		{
			Entries.clear();
			FS_LOG("[FallingStars] ColoredSubtitle: 清空全部字幕\n");
		}

		void HandleAction(TActionClass* pAction)
		{
			if (!pAction)
				return;

			FS_LOG("[FallingStars] ColoredSubtitle: 收到动作 kind=%d 文本=[%s] ()颜色=%d <>颜色=%d []颜色=%d\n",
				static_cast<int>(pAction->ActionKind), pAction->Text,
				pAction->Param3, pAction->Param4, pAction->Param5);

			Entry entry;
			strncpy(entry.Text, pAction->Text, sizeof(entry.Text) - 1);
			entry.Text[sizeof(entry.Text) - 1] = '\0';
			// FA2 参数 → 引擎字段（文本模式 P1=4）：P2→Text、P3→Param3、P4→Param4、P5→Param5
			entry.ColorIdx = pAction->Param3;        // P2：() 内颜色编号
			entry.AngleColorIdx = pAction->Param4;   // P3：<> 内颜色编号
			entry.BracketColorIdx = pAction->Param5; // P4：[] 内颜色编号

			// 显示时长：上限 5 秒（300 帧），保证"显示后消失"。
			//   引擎公式 TTL = [RulesClass+0x14C0]×900 在本环境偏长（约 15 秒），
			//   故 clamp 到 [1 秒, 5 秒]，异常时回退 300 帧。
			//   ★ 触发动作本身只执行一次（触发后自毁），字幕靠 TTL 自然消失，
			//     不存在"反复续命"问题，无需去重。
			entry.TTL = 300;
			if (RulesClass::Instance)
			{
				const double msgDur = *(const double*)(
					reinterpret_cast<const char*>(RulesClass::Instance) + 0x14C0);
				if (msgDur > 0.0)
				{
					int t = static_cast<int>(msgDur * 900.0);
					if (t < 60)  t = 60;   // 下限 1 秒
					if (t > 300) t = 300;  // 上限 5 秒
					entry.TTL = t;
				}
			}

			// 登记字幕。★ 不做"同文本去重/忽略"：地图触发（动作 526）在满足
			// 条件后执行一次即自毁，不会重复执行；因此每个动作都对应一条独立
			// 字幕，多条同文本字幕也按加入顺序自动让位堆叠（见 RenderAll）。
			// 若确有重复执行的触发链，字幕会叠加显示，属正常语义。
			Entries.push_back(entry);

			// 播放与触发结果 11（文本触发事件）完全相同的提示音：
			//   原版 MessageListClass::AddMessage（0x5D3BA0）在调用方要求提示音
			//   （参数7=0）时播放 [RulesClass+0x6AC] 指定的声音（YR rules.ini
			//   IncomingMessage=MessageText 对应的音效，具体编号随各 mod 的
			//   sound.ini 而定）。此处原样复刻：同一声音索引、panning=0x2000、
			//   volume=1.0f，听感与动作 11 一致。
			//   ★ 每次登记都播放（触发一次响一次）。
			if (RulesClass::Instance)
			{
				const int snd = *(const int*)(
					reinterpret_cast<const char*>(RulesClass::Instance) + 0x6AC);
				VocClass::PlayGlobal(snd, 0x2000, 1.0f);
				FS_LOG("[FallingStars] ColoredSubtitle: 播放提示音 snd=%d\n", snd);
			}
		}

		// -------------------------------------------------------------------
		// 渲染：屏幕左上角（仿未居中时触发结果 11 的左上角消息区，左对齐、
		// 顶部向下堆叠），黑底 + 逐段彩色文字 + 打字机动画。多段文本按()
		// 分组着色（()内用颜色参数、其余默认色）；() 括号不进入任何段，
		// 永不绘制；打字机按 Reveal 逐段裁剪，只影响显示不影响括号配对。
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

			const int lineHeight = 20; // 多条字幕自动让位：第 N 条下移 (N-1)*20px
			int order = 0;

			for (auto it = Entries.begin(); it != Entries.end();)
			{
				if (--it->TTL <= 0)
				{
					it = Entries.erase(it); // 超时消失（同触发结果 11）
					continue;
				}

				// 1) 文本转宽字符（DSurface::DrawText 需要 wchar_t*）。
				//    ★ CSF 兼容：先按 key 查 CSF（StringTable::LoadString）——
				//      查到（FA2 里"选 CSF 文本"填的 Name:xxx）→ 显示 CSF 文本
				//      （多语言、集中管理）；查不到（INI 直接写的字面文本）→
				//      回退按字面文本显示。两种填法都支持。
				//    ★ 顺序：必须先 LoadString 再解析()颜色分组（CSF
				//      文本里也可能带()）。
				if (!it->Text[0])
				{
					++it;
					continue; // 空文本不渲染
				}
				wchar_t wtext[0x100] = { 0 };
				const wchar_t* pResolved = StringTable::LoadString(it->Text);
				if (pResolved && pResolved[0])
				{
					wcsncpy(wtext, pResolved, sizeof(wtext) / sizeof(wchar_t) - 1);
				}
				else
				{
					// CSF 未命中且返回空：按字面文本原样转宽字符
					MultiByteToWideChar(CP_ACP, 0, it->Text, -1,
						wtext, static_cast<int>(sizeof(wtext) / sizeof(wchar_t)) - 1);
				}

				// 1b) 打字机动画：每 kTypeSpeed 帧揭示一个字符（逐字出现）。
				//     ★ 关键：Reveal 只控制【绘制裁剪】，分段解析始终基于【完整
				//       文本】——这样 () 括号必然配对、永远不会被画出来。
				//       （若按截断文本解析，'(' 出现而 ')' 未打出时配对失败，
				//       括号会被当普通文字显示——这就是之前括号被打出来的原因。）
				//     ★ 速度：kTypeSpeed=1 = 每帧 1 字符（60fps），对齐 Phobos
				//       MessageLabel 的 AnimPos += (dt >> 4)，即每 16ms 1 字符。
				const int kTypeSpeed = 1; // 每 1 帧 1 字符（与触发结果 11 同速）
				const size_t fullLen = wcslen(wtext);
				if (++it->TypeTimer >= kTypeSpeed)
				{
					it->TypeTimer = 0;
					if (it->Reveal < static_cast<int>(fullLen))
					{
						++it->Reveal;
						// ★ 与触发结果 11 完全一致：每个新字符揭示时播放【打字机音】
						//   [RulesClass+0x6C4] = MessageCharTyped = TextBleep
						//   （各 mod 的 sound.ini 对应编号，音频 utext）——
						//   这是事件 11 文本逐字出现时的"滴答"音（用户实测听感主体）。
						//   运行时动态解析，任何 mod 通用。
						if (RulesClass::Instance)
						{
							const int bleep = *(const int*)(
								reinterpret_cast<const char*>(RulesClass::Instance) + 0x6C4);
							VocClass::PlayGlobal(bleep, 0x2000, 1.0f);
						}
					}
				}

				// 2) 解析三种括号分段：每段 = (颜色种类, 起始指针, 长度) —— 基于【完整文本】
				//    ()或（）→ P2 颜色(ColorIdx)；<>或《》→ P3 颜色(AngleColorIdx)；
				//    []或【】→ P4 颜色(BracketColorIdx)；未括起 → 白色。
				//    括号本身不进入任何段 → 永不绘制。
				struct Seg { const wchar_t* p; int len; int colorKind; };
				Seg segs[32];
				int nSegs = 0;

				const wchar_t* p = wtext;
				const wchar_t* segStart = wtext;
				while (*p && nSegs < 31)
				{
					int kind = 0; // 匹配到的段颜色种类（kSegParen/kSegAngle/kSegBracket）
					const wchar_t* close = nullptr;
					const wchar_t openCh = p[0];
					if (openCh == L'(' || openCh == L'（')
					{
						close = wcschr(p + 1, (openCh == L'(') ? L')' : L'）');
						if (close) kind = kSegParen;
					}
					else if (openCh == L'<' || openCh == L'《')
					{
						close = wcschr(p + 1, (openCh == L'<') ? L'>' : L'》');
						if (close) kind = kSegAngle;
					}
					else if (openCh == L'[' || openCh == L'【')
					{
						close = wcschr(p + 1, (openCh == L'[') ? L']' : L'】');
						if (close) kind = kSegBracket;
					}

					if (close)
					{
						// 收尾括号前的普通文本段（白色）
						if (p > segStart)
						{
							segs[nSegs++] = { segStart, static_cast<int>(p - segStart), kSegDefault };
						}
						// 括号内的文字单独成段，用对应颜色参数选的颜色
						if (close > p + 1)
						{
							segs[nSegs++] = { p + 1, static_cast<int>(close - p - 1), kind };
						}
						p = close + 1;
						segStart = p;
						continue;
					}
					++p;
				}
				if (p > segStart)
				{
					segs[nSegs++] = { segStart, static_cast<int>(p - segStart), kSegDefault };
				}

				// 3b) 黑底宽度：各段【完整长度】宽度之和（段不含括号 → 黑底不含
				//     括号占位），打字机期间黑底保持此完整宽度
				// ★ 测量 marginX 必须为 0：GetTextDimensions(0x4A59E0) 是"tooltip
				//   文本框测量"，marginX=2 会给宽度加左右各 2px，而 DrawText
				//   （Fancy_Text_Print_Wide NoShadow）绘制无此边距 → x 多推进
				//   4px，每段之间出现空位（"括号括住的文字两边有空位"即此因）。
				int totalW = 0;
				RectangleStruct dimFull = Drawing::GetTextDimensions(wtext, { 0, 0 }, 0, 0, 0);
				for (int i = 0; i < nSegs; ++i)
				{
					wchar_t tmp[0x100] = { 0 };
					wcsncpy(tmp, segs[i].p, segs[i].len);
					RectangleStruct dim = Drawing::GetTextDimensions(tmp, { 0, 0 }, 0, 0, 0);
					totalW += dim.Width;
				}

				// 4) 左上角（仿未居中时触发结果 11 的左上角消息区）：
				//    左对齐贴边、从顶部向下堆叠
				int posX = 8;
				int posY = 8 + order * lineHeight;
				if (posY > screenH - 24) posY = screenH - 24;

				// 5) 黑底（整行包一个框，纯黑；黑色文字已在 GetTextColor 提亮）
				RectangleStruct bg = { posX - 4, posY - 2, totalW + 8, dimFull.Height + 4 };
				pSurface->FillRect(&bg, COLOR_BLACK);

				// 6) 逐段绘制（打字机：每段按 Reveal 裁剪可见部分；() 括号不在
				//    任何段内 → 永不绘制）
				int x = posX;
				for (int i = 0; i < nSegs; ++i)
				{
					const int segOff = static_cast<int>(segs[i].p - wtext); // 段起始字符偏移
					int vis = it->Reveal - segOff;                          // 该段已揭示字符数
					if (vis <= 0)
						continue;                     // 该段尚未揭示（打字机未到）
					if (vis > segs[i].len)
						vis = segs[i].len;            // 该段已全部揭示
					wchar_t tmp[0x100] = { 0 };
					wcsncpy(tmp, segs[i].p, vis);
					RectangleStruct dim = Drawing::GetTextDimensions(tmp, { 0, 0 }, 0, 0, 0);
					const ColorStruct& c = GetTextColor(segs[i].colorKind, *it);
					// ★ YR DrawText 的 COLORREF 是固定 16 位 RGB565
					//   （YRpp COLOR_RED=0xF800、COLOR_WHITE=0xFFFF 佐证）。
					//   不能直接用 Windows RGB()（32 位 0x00BBGGRR，红蓝互换），
					//   也不能用 Drawing::RGB_To_Int（按运行时位深输出，32 位
					//   色深下低 16 位截断同样错乱）——手动转 RGB565 最稳。
					const COLORREF color =
						((c.R & 0xF8) << 8) | ((c.G & 0xFC) << 3) | (c.B >> 3);
					pSurface->DrawText(tmp, x, posY, color);
					x += dim.Width;
				}

				++order;
				++it;
			}
		}
	}
} // namespace FS
