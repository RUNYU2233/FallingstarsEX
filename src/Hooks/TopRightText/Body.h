#pragma once

#include <YRpp.h>
#include <vector>

namespace FS
{

	// ===========================================================================
	// 触发结果 522「右上角文本建立」/ 523「右上角文本替换」/ 524「右上角文本删除」
	// — FallingStars 自定义触发动作（仿 Phobos 版本号显示的右上角黑底白字样式）
	//
	// 功能概述：
	//   在屏幕右上角（战术视图范围内、自动避开右侧的雷达图与建造栏）绘制若干行
	//   常驻文本。每行文本由一个数字编号锁定：
	//     522 建立：以指定编号新建一行文本；若编号已存在则原位覆盖其文本；
	//     523 替换：按编号替换已有文本的内容（在队列中的位置保持不变）；
	//     524 删除：按编号删除一行文本，剩余文本自动上移补位。
	//
	// 地图 [Actions] 段参数（每动作 8 字段：ID, P1..P7）：
	//   522：P1=4(文本模式), P2=显示文本(CSF条目名), P3=文本编号
	//   523：P1=4(文本模式), P2=新显示文本(CSF条目名), P3=要替换的文本编号
	//   524：P1=0(数值模式), P2=要删除的文本编号（加载时写入 TActionClass::Value）
	//   参数布局与 Phobos 横幅触发（800 建立/替换、802 删除）完全同构：
	//   - 522/523 与 Phobos 800 一样走 P1=4 文本模式：P2→Text、P3→Param3；
	//   - 524 与 Phobos 802 一样走 P1=0 数值模式：P2→Value（Phobos 删除横幅
	//     时同样从 Value 字段读取编号，见 Phobos TActionExt::DeleteBanner）。
	//
	// 引擎侧 Hook（复用 UnitCounter 已占用的三个地址，见 UnitCounter/Body.cpp，
	// 同一地址在一个 DLL 内只能注册一个 Hook，故由其 Hook 函数体内追加调用）：
	//   0x7265C0 TriggerClass::FireActions         —— 动作分发（520-524 共用）
	//   0x7275D0 TriggerTypeClass::LoadFromINIList —— 新场景清空
	//   0x4F4780 GScreenClass::UpdatePrimarySurface—— 整帧送显前绘制（UI 最上层）
	//
	// 布局规则：
	//   - 右边界 = 战术视图全局边界 view_bound(0xB0CE28) 的 X+Width - 边距，
	//     即侧栏左缘（雷达图与建造栏均在侧栏内）→ 永不与之重叠；
	//   - 多行文本自上而下逐行间隔绘制（行距 20px），按建立顺序排列；
	//   - 删除后按剩余条目顺序重新计算行位，自动上移补位；
	//   - 文本为全局（不分所属方）：任意触发建立/替换/删除对所有玩家生效，
	//     与原版触发结果 11「文本触发事件」的全局播报语义一致。
	//
	// 已知限制（与 520/521 计数器一致）：
	//   登记表只存活于内存，不写入存档；读档后需由触发重新建立。
	// ===========================================================================
	namespace TopRightText
	{
		/// <summary>动作类型。</summary>
		enum ActionKind : int
		{
			Action_TextCreate = 522, // 右上角文本建立
			Action_TextReplace = 523, // 右上角文本替换（按编号，位置不变）
			Action_TextDelete = 524  // 右上角文本删除（按编号，剩余自动补位）
		};

		/// <summary>单行右上角文本的运行期数据（由动作 522/523 创建 / 更新）。</summary>
		struct Entry
		{
			int ID = 0;               // 文本编号（522/523 → P3/Param3；524 匹配 Value）
			char Text[0x20] = { 0 };  // P2 字符串：显示文本（CSF 条目名，同触发结果 11）
		};

		/// <summary>全局登记表（按建立顺序排列 = 屏幕自上而下的行序）。</summary>
		extern std::vector<Entry> Entries;

		/// <summary>新场景开始时清空全部文本。</summary>
		void ClearAll();

		/// <summary>处理触发动作 522/523/524（无所属方语义，全局生效）。</summary>
		void HandleAction(TActionClass* pAction);

		/// <summary>每帧渲染：右上角黑底白字、自上而下间隔绘制、自动补位。</summary>
		void RenderAll();
	}

} // namespace FS
