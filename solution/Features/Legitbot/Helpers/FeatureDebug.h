#pragma once

#include <cstdint>

// Временная диагностика. Реализация (Dump/Throttle) - в FeatureDebug.cpp,
// чтобы её можно было вызывать из любых хуков.
namespace FeatureDebug {
	// Подробные построчные логи (run:/ft/scan:). Выключено по умолчанию,
	// иначе файл распухает до десятков мегабайт.
	extern bool verbose;

	void Dump(const char* fmt, ...);

	// true, если с момента прошлого вызова с этим id прошло >= interval_ms.
	// Позволяет не заспамить лог из кода, который зовётся каждый тик.
	bool Throttle(int id, int64_t interval_ms);
}
