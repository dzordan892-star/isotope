#pragma once

#include "Helpers/HitboxGeometry.h"
#include "Helpers/LagCompensation.h"
#include "Helpers/SpreadCache.h"
#include "../../Utils/Math/Math.h"
#include <chrono>
#include <vector>

struct CCSGOInput;
struct C_CSPlayerPawn;
struct CUserCommand;
struct LagRecord;

class CLegitbot {
   public:
	void OnCreateMove(CCSGOInput* input);
	void OnPresent();

	void PrepareAutoStop();
	bool ApplyAutoStop(int tick_offset);
	void RestoreAutoStop(CUserCommand* cmd);

   private:
	struct Point_t {
		Vector point;
		Vector angle{};
		float fov = 0.f;
		float distance = 0.f;
		LagRecord* record = nullptr;
		int hitbox = -1;
		int group = -1;
	};

	struct Target_t {
		C_CSPlayerPawn* player = nullptr;
		LagRecord* record = nullptr;
		Vector point{};
		Vector angle{};
		float damage = 0.f;
		float distance = 0.f;
		float fov = 0.f;
		int hitbox = -1;
		int group = -1;
	};

	bool IsWeaponReady() const;
	int HitboxGroup(const LagHitbox_t& hitbox) const;
	float MinimumDamage(C_CSPlayerPawn* player) const;
	bool UpdateAccuracy();
	uint32_t SeedFor(const Vector& angle, int tick) const;
	uint32_t ComputeRandomSeed(const Vector& angle, int tick) const;
	int ShotTick() const;
	Vector SolveSpreadAngle(const Vector& desired, int tick, uint32_t& out_seed) const;
	bool SpreadOffset(uint32_t seed, float& out_x, float& out_y) const;
	// Проверяет, попадёт ли предсказанная сервером пуля в цель. Не меняет
	// углы: если разброс уводит мимо - выстрел просто не производится.
	bool PredictedSpreadHit(const Vector& eye, const Vector& view, C_CSPlayerPawn* target,
		bool penetrating, uint32_t& out_seed, float& out_offset) const;
	std::vector<LagRecord*> SelectRecords(C_CSPlayerPawn* player, int processing_tick) const;
	std::vector<Point_t> BuildPoints(LagRecord* record, const Vector& eye) const;
	float CalcHitchance(LagRecord* record, const Vector& eye, const Vector& angle, int group);
	bool EvaluatePoint(C_CSPlayerPawn* player, LagRecord* record, const Point_t& point, const Vector& eye,
		bool penetrating, Target_t& best) const;
	bool FindTarget(const Vector& eye, const Vector& view, bool penetrating, Target_t& target,
		int preferred_handle = -1, bool require_hitchance = true);
	void UseTopHead(LagRecord* record, const Vector& eye, Target_t& target) const;
	bool ShouldAutoStop(bool shot_possible);
	bool RunKnifebot(CCSGOInput* input, const Vector& eye);
	bool ApplyAngles(CCSGOInput* input, const Vector& angle) const;
	bool Fire(const Vector& angle, LagRecord* record = nullptr);
	Vector Humanize(const Vector& angle) const;

	std::chrono::steady_clock::time_point trigger_start{};
	std::chrono::steady_clock::time_point aim_start{};
	// Триггер сам снимает IN_ATTACK через тик после выстрела, иначе
	// автоматическое оружие стреляет непрерывно и начинает мазать.
	bool trigger_shot_active = false;
	// После выстрела стоп выключен, пока оружие снова не сможет стрелять.
	// Обычного bool мало: на скауте он сбрасывался через тик и стоп
	// включался ещё до восстановления м нNextPrimaryAttackTick.
	int auto_stop_fire_tick = -1;
	int aim_last_handle = -1;
	int aim_lost_ticks = 0;
	int trigger_last_handle = -1;

	// Сглаженный угол аима. Храним его между тиками, чтобы Smoothing
	// работал даже если движок перезаписывает viewangles из мыши.
	Vector aim_smoothed_angle{};
	bool aim_smoothed_valid = false;
	float aim_wobble_phase = 0.f;
	// Движок иногда отдаёт нулевые viewangles на один тик. Без кэша
	// аим на таком тике теряет цель, а прицел дёргается ("срывает сенс").
	Vector last_valid_view{};

	CSpreadCache spread_cache;
	float shot_inaccuracy = 0.f;
	float shot_spread = 0.f;
	std::vector<HitboxGeometry_t::RayData_t> hitchance_rays;
	std::vector<size_t> desired_hitboxes;
	std::vector<size_t> other_hitboxes;
	const LagRecord* hitchance_record = nullptr;
	uint64_t hitchance_revision = 0;
	uint64_t hitchance_spread_revision = 0;
	Vector hitchance_eye{};

	bool auto_stop_armed = false;
	int peek_ticks = 0;
	int stop_command = -1;
	int stop_weapon = -1;
	int predicted_stop_command = -1;
	int applied_stop_command = -1;
	Vector stop_input{};
	std::vector<Vector> stop_subtick_input{};
};

extern CLegitbot* Legitbot;
