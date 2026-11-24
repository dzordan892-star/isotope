#include <shared.h>
#include <cstdint>
#include <cstdio>

// Простейший обработчик крашей: пишет код исключения, адрес и стек в
// %USERPROFILE%\Desktop\isotope_crash.txt. Без него любые вылеты при смене карты/режима
// приходится угадывать. Адреса пишутся ещё и как смещение от client.dll
// и от самой DLL чита, чтобы по ним сразу найти место в коде.
static LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;

static const char* CrashLogPath() {
    static char path[MAX_PATH]{};
    if (path[0])
        return path;

    char profile[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
        sprintf_s(path, "%s\\Desktop\\isotope_crash.txt", profile);
    else
        strcpy_s(path, "C:\\Users\\Public\\isotope_crash.txt");
    return path;
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (info && info->ExceptionRecord) {
        const auto* record = info->ExceptionRecord;

        HMODULE client = GetModuleHandleA("client.dll");
        HMODULE engine = GetModuleHandleA("engine2.dll");
        HMODULE cheat = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&CrashFilter),
            &cheat);

        FILE* file = nullptr;
        if (fopen_s(&file, CrashLogPath(), "a") == 0 && file) {
            const auto address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
            const auto client_base = reinterpret_cast<uintptr_t>(client);
            const auto engine_base = reinterpret_cast<uintptr_t>(engine);
            const auto cheat_base = reinterpret_cast<uintptr_t>(cheat);

            fprintf(file, "crash: code=0x%08lX addr=%p client_off=0x%llX engine_off=0x%llX cheat_off=0x%llX tid=%lu\n",
                static_cast<unsigned long>(record->ExceptionCode),
                record->ExceptionAddress,
                static_cast<unsigned long long>(client ? address - client_base : 0),
                static_cast<unsigned long long>(engine ? address - engine_base : 0),
                static_cast<unsigned long long>(cheat ? address - cheat_base : 0),
                static_cast<unsigned long>(GetCurrentThreadId()));

            if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
                const char* access = record->ExceptionInformation[0] == 1 ? "write"
                    : (record->ExceptionInformation[0] == 8 ? "execute" : "read");
                fprintf(file, "crash: access=%s address=%p\n",
                    access,
                    reinterpret_cast<void*>(record->ExceptionInformation[1]));
            }

            void* frames[24] = {};
            const USHORT count = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
            fprintf(file, "crash: stack=%u\n", static_cast<unsigned>(count));
            for (USHORT i = 0; i < count; ++i) {
                const auto frame = reinterpret_cast<uintptr_t>(frames[i]);
                fprintf(file, "  [%02u] %p client+0x%llX engine+0x%llX cheat+0x%llX\n",
                    static_cast<unsigned>(i),
                    frames[i],
                    static_cast<unsigned long long>(client ? frame - client_base : 0),
                    static_cast<unsigned long long>(engine ? frame - engine_base : 0),
                    static_cast<unsigned long long>(cheat ? frame - cheat_base : 0));
            }
            fclose(file);
        }
    }

    if (previous_filter)
        return previous_filter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(module);

		previous_filter = SetUnhandledExceptionFilter(CrashFilter);

		if (HANDLE thread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Entry_t::Fill), module, 0, nullptr))
			CloseHandle(thread);
	} else if (reason == DLL_PROCESS_DETACH) {
		
	}

	return TRUE;
}
