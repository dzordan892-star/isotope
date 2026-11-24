#include <shared.h>
#include "FeatureDebug.h"
#include <chrono>
#include <cstdarg>
#include <mutex>

namespace {
	int64_t NowMs() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	const char* DebugLogPath() {
		static char path[MAX_PATH]{};
		if (path[0])
			return path;

		char profile[MAX_PATH]{};
		const DWORD length = GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
			sprintf_s(path, "%s\\Desktop\\isotope_debug.txt", profile);
		else
			strcpy_s(path, "C:\\Users\\Public\\isotope_debug.txt");
		return path;
	}

	void WriteDebugLine(const char* fmt, va_list args) {
		static std::mutex write_mutex;
		static FILE* file = nullptr;
		static bool log_checked = false;
		static int pending = 0;

		std::lock_guard<std::mutex> lock(write_mutex);

		if (!log_checked) {
			log_checked = true;
			WIN32_FILE_ATTRIBUTE_DATA info{};
			if (GetFileAttributesExA(DebugLogPath(), GetFileExInfoStandard, &info)) {
				const ULONGLONG size = (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
				if (size > 8ull * 1024ull * 1024ull)
					DeleteFileA(DebugLogPath());
			}
		}

		if (!file)
			file = _fsopen(DebugLogPath(), "a", _SH_DENYNO);

		if (!file)
			return;

		vfprintf(file, fmt, args);
		fputc('\n', file);

		if (++pending >= 128) {
			pending = 0;
			fflush(file);
		}
	}
}

namespace FeatureDebug {
	bool verbose = false;

	void Dump(const char* fmt, ...) {
		va_list args;
		va_start(args, fmt);
		WriteDebugLine(fmt, args);
		va_end(args);
	}

	bool Throttle(int id, int64_t interval_ms) {
		static int64_t last_ms[4096] = {};
		id = (id >= 0 ? id : 0) & 0xFFF;
		const int64_t now = NowMs();
		if (now - last_ms[id] < interval_ms)
			return false;
		last_ms[id] = now;
		return true;
	}
}
