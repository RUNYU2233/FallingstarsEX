#pragma once

#include <YRpp.h>
#include <vector>

namespace FS
{

	// ===========================================================================
	// 触发结果 520「单位计数显示」/ 521「科技类型计数」— FallingStars 自定义触发动作
	//
	// 对触发所属方在屏幕右下角绘制一行文本，后面跟随一个数字：
//   520：自定义文本(CSF) + 指定类别（载具/步兵/飞行器/舰船/建筑）的单位数量；
//   521：自定义文本(CSF) + 指定科技类型（按 TechnoTypeClass::Array 序号）的单位数量。
//        无自定义文本(P2 为空)时显示「当前数量：」。
	// 统计范围按「敌人 / 盟友 / 指定所属方 / 全图全部」过滤。
	//
	// 布局（仿超级武器倒计时贴底边栏）：
	//   - 计数器绘制在右下角（侧栏左侧）、紧贴底边栏（命令栏）顶部，
	//     多条目逐行向上堆叠（行距 18px）；
	//   - 文本颜色 = 【触发所属方（Owner）的玩家颜色】（ColorSchemeIndex →
	//     ColorScheme::BaseColor(HSV) → 引擎 0x517440 转 RGB），黑底；
	//   - 超级武器倒计时等右下角底部文本由 Hook 4 自动上移让位
	//     （0x4A61C0 坐标过滤：Y 在屏幕底部 130px 内的文本上移 N*18px）。
	//
	// 地图 [Actions] 段参数布局（每动作 8 字段：ID, P1..P7）：
//   520：P1=统计目标, P2=显示文本(CSF条目名), P3=单位种类, P4=指定所属方
//   521：P1=统计目标, P2=显示文本(CSF条目名), P3=指定所属方, P4=科技类型索引
//         （P4 = 该科技在 TechnoTypeClass::Array 中的序号，用于统计该科技类型单位）
//   数量为 0 的计数器自动消除并【锁存隐藏】：即使单位重新出现也不再绘制，
//   只有触发器再次触发才会恢复显示。
//
// 引擎侧 Hook（详见 Body.cpp）：
//   0x7265C0 TriggerClass::FireActions         —— 动作分发（避开 Phobos 的 0x6DD8B0）
//   0x7275D0 TriggerTypeClass::LoadFromINIList —— 新场景清空
//   0x4F4583 GScreenClass::Render（统计面板后、鼠标绘制前）—— 每帧绘制
//            （UI 之上、鼠标指针之下）
	// ===========================================================================
	namespace UnitCounter
	{
		/// <summary>动作类型。</summary>
		enum ActionKind : int
		{
			Action_CountByCategory = 520, // 单位计数显示（按种类）
			Action_CountByTechnoType = 521 // 科技类型计数（按具体科技类型）
		};

		/// <summary>统计目标（对应动作的 P1 / Value）。</summary>
		enum CountTarget : int
		{
			CountTarget_Enemy = 0,      // 触发所属方的敌人
			CountTarget_Ally = 1,       // 触发所属方的盟友（含其自身）
			CountTarget_Specified = 2,  // 指定所属方（见 SpecifiedHouseIdx）
			CountTarget_All = 3,        // 地图上全部所属方
			CountTarget_Self = 4        // 仅触发所属方自身（事件 607/608/609：所属方由触发器决定，无需配置）
		};

		/// <summary>统计的单位种类（对应动作 520 的 P3）。</summary>
		enum CountCategory : int
		{
			Category_All = 0,       // 全部单位
			Category_Unit = 1,      // 载具（含舰船）
			Category_Infantry = 2,  // 步兵
			Category_Aircraft = 3,  // 飞行器
			Category_Naval = 4,     // 舰船（移动方式为浮渡的载具）
			Category_Building = 5   // 建筑
		};

		/// <summary>单个计数器的运行期配置（由动作 520/521 创建 / 更新）。</summary>
		struct Entry
		{
		HouseClass* Owner = nullptr;     // 触发所属方（仅该方玩家可见）
		int Kind = Action_CountByCategory; // 动作类型（520 或 521）
		char Text[0x20] = { 0 };         // P2 字符串：520/521 共用的自定义显示文本（CSF 条目名）
		int Target = CountTarget_Enemy;  // 统计目标（P1）
		int RawTarget = 0;               // FA2 原始统计目标编号（0敌方/1己方/2同盟/3全部/4指定所属方）
		                                 // 预留：当前文字颜色按所属方(Owner)显示，此字段暂未参与配色
		int Category = Category_All;     // 单位种类（520 的 P3）
		int SpecifiedHouseIdx = -1;      // 统计目标为「指定所属方」时的所属方索引（520 的 P4 / 521 的 P3）
		int TechnoTypeIndex = -1;        // 521 统计的科技类型在 TechnoTypeClass::Array 中的序号（P4）
		bool Defunct = false;            // 锁存隐藏：统计数量曾归零后永久不绘制（不占行位），
		                                 // 仅当触发器再次触发（HandleAction 覆盖更新）时才恢复
		};

		// ===========================================================================
		// 触发条件（事件）607/608/609「单位数量判定」
		//
		// 统计"触发器所属方"拥有的指定单位类型数量并做比较，但【不绘制任何计时器】，
		// 仅作为触发条件判断当前数量与给定数值的关系，满足时该触发器判定成立：
		//   607：数量等于 (count == N)
		//   608：数量大于 (count >  N)
		//   609：数量小于 (count <  N)
		//
		// 参数为 [Events] 行里的 2 个数字（紧跟 EventKind 之后，P3/P4 弃用）：
		//   P1 = 单位种类（FA2 参数类型 6，数值：0全部/1载具/2步兵/3飞行器/4建筑/5海军，同动作 520）
		//   P2 = 比较数值 N（FA2 参数类型 6，与当前数量比较）
		//   所属方不单独配：直接用"触发器的所属方"（HasOccured 的 pHouse）作为统计对象。
		//
		// 集成方式（详见 Body.cpp）：
		//   - 条件判定：挂在 TriggerClass 事件求值循环里【唯一】调用 HasOccured 的
		//     调用点 0x726540（替换 `call 0x71e940` 这条指令）。不直接挂 0x71E940 入口：
		//     Phobos 的 TEventClass_Execute 已占 0x71E940（size 0x5），Ares 占入口 +9
		//     （0x71E949），入口附近已被占满，同地址双 DLL 会互相覆盖。
		//     对本 DLL 的 607/608/609 这一次调用，直接返回 CountUnits 的判定结果即可，
		//     调用方的 AND/OR / LinkTrigger 组合逻辑完全不动；非本 DLL 事件走 trampoline
		//     执行原始 call（进入 HasOccured 由 Phobos/Ares 处理）。
		//   - 参数捕获（惰性，运行时）：HasOccured 运行时 Value/String 已被 LoadFromINI 填好，
		//     故在求值里首次构造 CountCondition 并缓存，无需再挂 LoadFromINI。
		//     字段映射（两个参数都是类型码 6 的数值，引擎按通用事件加载）：
		//       P1（单位种类）→ Value；P2（比较数值 N）→ String（十进制串，atoi）
		//     引擎若对未知 EventKind 丢弃参数（Value=0 且 String 空），则改在 0x71F4E0 直接读
		//     原始 [Events] 行；此情形由 DebugView 日志 "[FallingStars] CountEvent Raw:" 暴露。
		// ===========================================================================
		enum CountEventKind : int
		{
			Event_CountEquals   = 607, // 数量等于 N
			Event_CountGreater  = 608, // 数量大于 N
			Event_CountLess     = 609  // 数量小于 N
		};

		enum CountCmpOp : int
		{
			Cmp_Equal = 0,   // ==
			Cmp_Greater = 1, // >
			Cmp_Less = 2     // <
		};

		/// <summary>单个数量判定条件的运行期配置（由事件 607/608/609 创建）。</summary>
		struct CountCondition
		{
			HouseClass* Owner = nullptr; // 触发器所属方（统计此所属方拥有的单位）
			int Category = Category_All; // P1：单位种类（0全部/1载具/2步兵/3飞行器/4建筑/5海军，同动作 520）
			int CompareValue = 0;        // P2：比较数值 N（与当前数量比较）
			int Op = Cmp_Equal;          // 比较方式（由 EventKind 推导：607/608/609）
			bool Initialized = false;    // 参数是否已从 LoadFromINI 捕获
		};

		/// <summary>
		/// 判定单个数量条件是否成立（复用 CountUnits 的统计逻辑）。
		/// pHouse 为本次求值的触发器所属方 —— 不缓存（同一事件可能被多个触发器实例共享），
		/// 每次求值传入，保证统计对象始终是"当前这个触发器"的所属方。
		/// </summary>
		bool EvaluateCountCondition(const CountCondition& cond, HouseClass* pHouse);

		/// <summary>全局登记表（仅存在于本 DLL 堆内，不跨 DLL 边界）。</summary>
		extern std::vector<Entry> Entries;

		/// <summary>新场景开始时清空所有计数器。</summary>
		void ClearAll();

		/// <summary>处理触发动作 520/521：登记 / 更新一个计数器。</summary>
		void HandleAction(TActionClass* pAction, HouseClass* pHouse);

		/// <summary>每帧渲染：只绘制当前玩家（HouseClass::Player）的计数器。</summary>
		void RenderAll();

		// -------------------------------------------------------------------
		// 动作参数读取（与 FA2 地图 [Actions] 段的实际加载一致）：
		//   P1 → 模式位（token2，TActionClass::LoadFromINI 用它做 switch 分派，
		//        不存储；固定填 4 才走"文本模式"）
		//   P2 → TActionClass::Text   （显示文本，CSF 条目名）
		//   P3 → TActionClass::Param3 （统计目标）
		//   P4 → TActionClass::Param4 （520=单位种类 / 521=指定所属方）
		//   P5 → TActionClass::Param5 （520=指定所属方 / 521=科技类型索引）
		// 依据：0x6DD5B0 LoadFromINI 反汇编——token2=4 走 case4(文本模式)：
		//   P2→Text@0x6D、P3→Param3@0x34、P4→Param4@0x38、P5→Param5@0x3C。
		// 切勿用 &Value+index 偏移（Value 在 0x90，结构末尾，token2 不写字段）。
		// -------------------------------------------------------------------

	} // namespace UnitCounter

} // namespace FS
