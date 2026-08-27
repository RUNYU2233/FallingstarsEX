#pragma once

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>

namespace FS::Log
{

	/// <summary>
	/// 日志文件路径：gamemd.exe 同目录下的 FallingStars.log。惰性解析，进程内只算一次。
	/// </summary>
	inline const char* GetLogPath()
	{
		static char s_logPath[MAX_PATH] = { 0 };
		if (!s_logPath[0])
		{
			char exePath[MAX_PATH] = { 0 };
			GetModuleFileNameA(nullptr, exePath, MAX_PATH);
			char* pSlash = strrchr(exePath, '\\');
			if (pSlash)
				pSlash[1] = '\0';
			strncat(exePath, "FallingStars.log", sizeof(exePath) - strlen(exePath) - 1);
			strncpy(s_logPath, exePath, sizeof(s_logPath) - 1);
		}
		return s_logPath;
	}

	/// <summary>
	/// 每次进程启动（新一次运行）调用一次：清空上次的日志内容，并写入会话头
	/// （含时间戳）。之后所有日志均以追加方式写入。放在 ExeRun 入口最先执行，
	/// 保证"下次记录时删除上次记录的内容"。
	/// </summary>
	inline void BeginSession()
	{
		FILE* f = fopen(GetLogPath(), "w");
		if (f)
		{
			time_t t = time(nullptr);
			struct tm ltm;
			localtime_s(&ltm, &t);
			char stamp[64];
			strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &ltm);
			fprintf(f, "[FallingStars] === 会话开始 %s ===\n", stamp);
			fclose(f);
		}
	}

	/// <summary>
	/// 清空日志文件但不写任何内容（稳定应用版 FS_STABLE 专用：注入后 log 里
	/// 只应有一句欢迎语，不需要调试会话头）。放在 ExeRun 入口最先执行。
	/// </summary>
	inline void ResetLog()
	{
		FILE* f = fopen(GetLogPath(), "w");
		if (f)
			fclose(f);
	}

	/// <summary>
	/// 打印一条格式化日志：同时输出到调试器（DebugView / VS 输出窗口）并以追加方式
	/// 写入 FallingStars.log。每行带 [HH:MM:SS] 时间戳前缀，便于完整复盘所有行为。
	/// </summary>
	inline void Print(const char* fmt, ...)
	{
		char buffer[2048];

		// 时间戳前缀（vsnprintf 之后把正文拼在时间戳之后）
		time_t t = time(nullptr);
		struct tm ltm;
		localtime_s(&ltm, &t);
		const int hdr = (int)strftime(buffer, sizeof(buffer), "[%H:%M:%S] ", &ltm);

		va_list args;
		va_start(args, fmt);
		const int n = vsnprintf(buffer + hdr, sizeof(buffer) - hdr, fmt, args);
		va_end(args);
		if (n <= 0)
			return;
		const int total = hdr + n;

		OutputDebugStringA(buffer);

		FILE* f = fopen(GetLogPath(), "a");
		if (f)
		{
			fwrite(buffer, 1, static_cast<size_t>(total), f);
			fclose(f);
		}
	}

} // namespace FS::Log

#ifndef FS_LOG
#ifdef FS_STABLE
// 稳定应用版（FS_STABLE）：彻底关闭调试日志——FS_LOG 展开为空操作，
// 所有调用点零开销、零输出；log 文件只由 ExeRun 入口写那句欢迎语。
#define FS_LOG(...) ((void)0)
#else
#define FS_LOG(...) ::FS::Log::Print(__VA_ARGS__)
#endif
#endif
