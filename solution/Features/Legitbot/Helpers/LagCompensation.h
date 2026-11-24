#pragma once

#include "../../../Utils/Math/Math.h"
#include "../../../SDK/Valve/Datatypes/Hitbox.h"
#include <cstdint>
#include <cstring>
#include <chrono>
#include <deque>
#include <span>
#include <unordered_map>
#include <vector>
#include <unordered_set>

struct HitboxGeometry_t;

struct C_CSPlayerPawn;
struct CSkeletonInstance;
struct CHitboxTransformRecord;
class CModel;

struct LagHitbox_t {
	const CHitbox* descriptor = nullptr;
	Vector mins;
	Vector maxs;
	float radius = 0.f;
	int hitgroup = 0;
	int index = -1;
	uint8_t shape_type = 0;
	bool foot = false;
	bool geometry_valid = true;
};

struct LagRecord {
	uint64_t revision = 0;
	C_CSPlayerPawn* player = nullptr;
	int handle = -1;
	float m_flSpawnTime = 0.f;
	float m_flSimulationTime = 0.f;
	int m_nSimulationTick = 0;
	int animation_tick = 0;
	int source_tick = 0;
	uint64_t source_revision = 0;
	int extrapolated_ticks = 0;
	int move_type = 0;
	int weapon_handle = -1;
	float gravity_scale = 1.f;
	uint64_t trace_mask = 0;
	Vector m_vecOrigin;
	Vector m_vecVelocity;
	Vector m_vecMins;
	Vector m_vecMaxs;
	int m_fFlags = 0;
	Vector m_angEyeAngles;
	Vector m_angAbsRotation;
	int update_tick = 0;
	bool shooting = false;
	Matrix2x4_t root_transform;

	CModel* model = nullptr;
	uintptr_t hitbox_list_context = 0;
	uint8_t hitbox_set = 0;
	uint64_t pose_hash = 0;
	std::span<const LagHitbox_t> hitboxes;
	std::vector<Matrix2x4_t> hitbox_transforms;
	// Скелетные кости (bone_cache), снятые на том же тике, что и hitbox_transforms.
	// Движковый ClipTraceToPlayer строит хитбокс-трансформы не из истории
	// m_HitboxTransformHistory и не из HitboxTransformOverride, а напрямую из
	// bone-массива скелета (skeleton + 0x1C0 = m_modelState + 0x80). Из-за этого
	// прострел по бектреку отвергался (awc = awt: клип не видел позу записи).
	// На время трассы подменяем указатель bone_cache на записанные кости.
	std::vector<Matrix2x4_t> bones;
	std::vector<HitboxGeometry_t> geometry;

	// Хеш позы считается один раз при записи. Раньше same_pose определялся
	// полным memcmp всех матриц для каждой пары записей на каждом тике, что
	// при 65 записях и ~5 КБ трансформов давало мегабайты сравнений в тик.
	static uint64_t HashPose(std::span<const Matrix2x4_t> transforms) {
		uint64_t hash = 0xcbf29ce484222325ULL;
		const auto* bytes = reinterpret_cast<const unsigned char*>(transforms.data());
		const size_t count = transforms.size() * sizeof(Matrix2x4_t);
		for (size_t i = 0; i < count; ++i) {
			hash ^= bytes[i];
			hash *= 0x100000001b3ULL;
		}
		return hash;
	}

	bool MatchesGeometry(CModel* candidate_model, uintptr_t candidate_context, uint8_t candidate_set,
		std::span<const LagHitbox_t> candidate_hitboxes, size_t candidate_count, uint64_t candidate_pose_hash) const {
		return revision != 0 && model == candidate_model && hitbox_list_context == candidate_context &&
			hitbox_set == candidate_set && hitboxes.data() == candidate_hitboxes.data() &&
			hitboxes.size() == candidate_hitboxes.size() && candidate_count != 0 &&
			hitbox_transforms.size() == candidate_count && pose_hash == candidate_pose_hash;
	}
};

class CLagRecordScope {
  public:
	// Готовый мировой фильтр трасс для конкретной цели. Нужен, чтобы не
	// создавать заново фильтры физики на каждую запись/точку.
	struct WorldFilter_t {
		CTraceFilter* filter = nullptr;
	};

  private:
	CSkeletonInstance* skeleton = nullptr;
	void* previous = nullptr;
	CHitboxTransformList transforms;
	// Последняя запись истории хитбоксов скелета на момент входа в scope.
	// Игровой ClipTraceToPlayer/поиск хитбокса берёт позу именно оттуда,
	// поэтому на время трассы подменяем её содержимое на позу записи.
	CHitboxTransformRecord* history_record = nullptr;
	int history_count = 0;
	int history_capacity = 0;
	Matrix2x4_t* history_transforms = nullptr;
	uint32_t history_flags = 0;

	// Кости скелета, подменённые на позу записи. Клип движка читает их
	// напрямую из skeleton->bone_cache, поэтому одной истории хитбоксов мало.
	Matrix2x4_t* previous_bones = nullptr;
	bool bones_substituted = false;

  public:
	explicit CLagRecordScope(const LagRecord* record, int processing_tick = -1, const WorldFilter_t* world = nullptr);
	~CLagRecordScope();
	CLagRecordScope(const CLagRecordScope&) = delete;
	CLagRecordScope& operator=(const CLagRecordScope&) = delete;

	explicit operator bool() const { return skeleton != nullptr; }
};

class CLagCompensation {
private:
	struct HitboxCache_t {
		CModel* model = nullptr;
		uintptr_t context = 0;
		uint8_t set = 0;
		std::vector<LagHitbox_t> hitboxes;
	};

	int extrapolation_tick = -1;

	int server_tick = -1;
	uint64_t record_revision = 0;
	std::chrono::steady_clock::time_point server_tick_time;
	// Кэш processing tick на один команда-тик: функция считается по steady_clock,
	// и повторные вызовы внутри одного CreateMove (скан, fire, валидация)
	// раньше могли давать разные тики и нестабильную валидность бектрека.
	mutable int cached_processing_tick = -1;
	mutable int cached_processing_command = -1;
	mutable int cached_processing_server_tick = -1;
	int max_records = 65;
	float max_unlag = 1.f;
	std::vector<LagRecord> free_records;
	std::vector<HitboxCache_t> hitbox_cache;
	std::unordered_map<int, LagRecord> extrapolated_records;

	std::unordered_set<int> extrapolation_tried;

	const std::vector<LagHitbox_t>* GetHitboxes(CSkeletonInstance* skeleton, int count);
	void BuildGeometry(LagRecord& record);
	bool PredictMovement(LagRecord& record, int ticks, const Vector& acceleration);
	bool Extrapolate(LagRecord& record, const LagRecord& source, const LagRecord& previous, int ticks);
	void RemoveInvalidRecords();
	void TryExtrapolate(int handle, int processing_tick);

public:
	std::unordered_map<int, std::deque<LagRecord>> lag_records;

	// Максимальное окно бектрека, заданное сервером. Нужно снаружи, чтобы
	// при пустом SelectRecords можно было отобрать последнюю валидную запись.
	float MaxUnlag() const { return max_unlag; }

	bool RecordDataIntoTrack(C_CSPlayerPawn* player, LagRecord* record, int tick, const LagRecord* previous = nullptr);
	void OnAnimationUpdate(C_CSPlayerPawn* player, int tick);
	void OnNetUpdate();
	void ExtrapolateRecords(int processing_tick);
	LagRecord* GetExtrapolatedRecord(C_CSPlayerPawn* player, int processing_tick);
	int GetProcessingTick() const;
	bool ValidRecord(const LagRecord* record, int processing_tick = -1) const;
	void Reset();
};

extern CLagCompensation* LagCompensation;
