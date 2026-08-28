#pragma once

#include <YRpp.h>
#include <vector>

namespace FS
{

	// ===========================================================================
	// 触发结果 526「彩色字幕」— FallingStars 自定义触发动作
	//
	// 出现形式与触发结果 11（TextTrigger）类似：黑底、超时自动消失，
	// 位置在【屏幕左上角】（左对齐、顶部向下堆叠，仿未居中时的动作 11
	// 消息区），带【打字机动画】（文字逐字出现）。区别：文本里被()
	// 括起来的文字，颜色由一个独立的「颜色编号」参数（数字 0-42）统一
	// 控制——编号对应 rules 的 [Colors] 段（TextColor 名字表）。
	//
	// 文本语法（三种颜色标记，颜色表相同，0-42）：
	//   (文字) —— 被()或（）括起来的内容用【P2 颜色编号】绘制；
	//   <文字> —— 被<>或《》括起来的内容用【P3 颜色编号】绘制；
	//   [文字] —— 被[]或【】括起来的内容用【P4 颜色编号】绘制；
	//   其余未括起来的文本用默认色【纯白】。
	//   例：P1 文本 = (警告！)<小心>[危险]任务完成，P2=1,P3=2,P4=3
	//   → "警告！"红、"小心"浅灰、"危险"浅蓝、"任务完成"白色。
	//   括号本身（()<>[] 及全角）永不绘制。
	//
	// 音效（与触发结果 11 文本触发事件完全一致，运行时动态解析，全 mod 通用）：
	//   ① 字幕登记时播一声提示音：[RulesClass+0x6AC] = IncomingMessage
	//      = MessageText（YR rules.ini 原版定义，各 mod 的 sound.ini 对应编号，
	//      音频 umessage）
	//   ② 打字机逐字揭示时播打字音：[RulesClass+0x6C4] = MessageCharTyped
	//      = TextBleep（原版 rulesmd.ini "typing" effect，各 mod 的 sound.ini
	//      对应编号，音频 utext）——事件 11 听感主体。
	//   两者引擎均自带越界保护（PlayGlobal 0x75093D jge），任何 mod 缺键也安全。
	//   每个 526 动作登记一次播放一次（触发自毁不重复）。
	//
	// 颜色渲染：内置 43 色【字面 RGB 色表】（按颜色名含义直接对照标准 RGB，
	// 不做任何 HSV/引擎转换）。若要微调某个颜色，改 Body.cpp s_stdColors 表。
	//
	// 颜色索引表（0-42，key 即 rules [Colors] 段的条目名）：
	//   0 Teal       1 Red       2 LightGrey   3 LightBlue  4 Green
	//   5 Purple     6 Gold      7 NeonBlue    8 Orange     9 Magenta
	//   10 Russet    11 DarkGreen 12 Crimson   13 Sky       14 FirstText
	//   15 SecondText 16 ThirdText 17 FourthText 18 Pink    19 DarkBlue
	//   20 olive     21 Khaki    22 LightGoId  23 BrightGrey 24 Grey
	//   25 DarkRed   26 DarkSky  27 NeonGreen  28 Black     29 Yellow
	//   30 Purple2   31 Purple3  32 Thistle    33 Brown2    34 Xanadu
	//   35 Lime      36 Jade     37 Charcoal   38 Peri      39 Aqua
	//   40 AlliedLoad 41 SovietLoad 42 ThirdLoad
	//
	// 参数（FA2 显示顺序 = P1/P2/P3/P4，引擎字段 = Text/Param3/Param4/Param5）：
	//   P1 = 字幕文本（TActionClass::Text，可含()、<>、[]标记），
	//   P2 = () 内颜色编号（引擎 Param3，0-42；未配置或越界时回退 FirstText=14），
	//   P3 = <> 内颜色编号（引擎 Param4，同色表），
	//   P4 = [] 内颜色编号（引擎 Param5，同色表）；
	//   未括起来部分始终为白色。
	//
	// 引擎侧 Hook（复用 UnitCounter 的三个共享 Hook，由 UnitCounter/Body.cpp
	// 作为分发枢纽顺序调用，模块间无状态耦合）：
	//   0x7265C0 TriggerClass::FireActions      —— 动作分发（526 → 本模块）
	//   0x7275D0 TriggerTypeClass::LoadFromINIList —— 新场景清空
	//   0x4F4583 GScreenClass::Render 内        —— 每帧渲染（左上角字幕）
	//
	// 渲染布局（用户指定：左上角，仿未居中的触发结果 11 消息区）：
	//   左对齐贴边（X=8px），从顶部向下按加入顺序堆叠（第 N 条下移
	//   (N-1)×20px 自动让位）；黑底（整条完整宽度）+ 逐段彩色文字 +
	//   【打字机动画】（每帧 1 字符，黑底先出现、文字逐字打出）；
	//   TTL 归零自动消失。不做同文本去重——每个 526 动作登记一条，
	//   同文本多条同样堆叠让位（触发动作只执行一次，不存在续命问题）。
	// ===========================================================================
	namespace ColoredSubtitle
	{
		/// <summary>动作类型。</summary>
		enum ActionKind : int
		{
			Action_ColoredSubtitle = 526, // 彩色字幕（右上角，黑底自动消失）
		};

		/// <summary>单条彩色字幕的运行期数据。</summary>
		struct Entry
		{
			char Text[0x100] = { 0 }; // 字幕文本（可含()、<>、[]标记）
			int TTL = 0;              // 剩余显示帧数（每帧递减，归零消失）
			int ColorIdx = -1;        // () 内文字的 [Colors] 颜色编号（0-42；<0 回退 FirstText）
			int AngleColorIdx = -1;   // <> 内文字的 [Colors] 颜色编号（0-42；<0 回退 FirstText）
			int BracketColorIdx = -1; // [] 内文字的 [Colors] 颜色编号（0-42；<0 回退 FirstText）
			int Reveal = 0;           // 打字机：已揭示的字符数（逐字出现）
			int TypeTimer = 0;        // 打字机：帧累积（每 kTypeSpeed 帧揭示 1 字符）
		};

		/// <summary>全局登记表（允许叠加多条，按加入顺序从下往上堆叠）。</summary>
		extern std::vector<Entry> Entries;

		/// <summary>新场景开始时清空全部字幕。</summary>
		void ClearAll();

		/// <summary>处理触发动作 526：登记一条彩色字幕。</summary>
		void HandleAction(TActionClass* pAction);

		/// <summary>每帧渲染：屏幕中央文本框、黑底、()内容按颜色参数分色、超时消失。</summary>
		void RenderAll();
	}

} // namespace FS
