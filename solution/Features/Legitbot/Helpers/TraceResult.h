#pragma once

#include <cstdint>

struct CHitbox;

namespace HitboxTrace {

// Причина, по которой ReadTargetImpact не нашёл хитбокс. Нужна для
// диагностики: awi в логе показывает только сам факт, а тут видно, где
// именно теряется попадание.
enum class Miss : uint8_t {
	None = 0,
	DamageEnded,   // info.damage <= 0 - пуля остановилась
	BadSegment,    // info.handle указывает вне trace_segments
	NoHitbox,      // ни декодер, ни сегмент не дали хитбокс
};

struct Impact {
	const CHitbox* hitbox = nullptr;
	float damage = 0.f;
	bool penetrated = false;
	Miss reason = Miss::None;
};

template <typename TraceData, typename NativeDecoder>
Impact ReadTargetImpact(const TraceData& data, uint32_t target_identity, NativeDecoder&& decode) {
	bool penetrated = false;
	for (int i = 0; i < data.trace_info.size; ++i) {
		const auto& info = data.trace_info[i];
		if (info.damage <= 0.f)
			return {nullptr, 0.f, penetrated, Miss::DamageEnded};
		if (info.IsPenetrationSegment()) {
			penetrated = true;
			continue;
		}

		const uint32_t index = info.handle >> 16;
		if (data.filled_segment_count <= 0 || index >= static_cast<uint32_t>(data.filled_segment_count))
			return {nullptr, 0.f, penetrated, Miss::BadSegment};
		const auto& segment = data.trace_segments[index];

		// В entity_handle лежит полноценный CBaseHandle (entry | serial << 15),
		// а target_identity - голый индекс сущности. Раньше здесь было прямое
		// сравнение handle == index: оно не могло совпасть НИКОГДА, поэтому
		// сегменты попадания в игрока отбрасывались ещё до декодирования.
		// Сравниваем только entry-часть - это верно и для голого индекса.
		const bool target_segment = segment.type == 1 && target_identity < UINT32_MAX - 1 &&
									(segment.entity_handle & 0x7FFFu) == (target_identity & 0x7FFFu);

		// Штатный путь - декодер (trace.hitbox из GetTraceInfo): он всегда
		// возвращает хитбокс из модели и однозначно безопасен. Указатель из
		// сегмента берём только как фолбэк: его раскладка зависит от версии
		// игры, а декодер после фикса маски handle работает и на сегментах
		// попадания в игрока.
		const CHitbox* hitbox = decode(info);
		if (!hitbox && target_segment && segment.body)
			hitbox = reinterpret_cast<const CHitbox*>(segment.body);
		if (hitbox)
			return {hitbox, info.damage, penetrated, Miss::None};
	}
	return {nullptr, 0.f, penetrated, Miss::NoHitbox};
}

} // namespace HitboxTrace
