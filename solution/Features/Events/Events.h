#pragma once

enum class ETracerType {
	Local,
	Enemy,
	Friendly
};

struct PendingImpact {
	bool valid = false;
	int group_key = -1;
	Vector start_pos{};
	Vector end_pos{};
	ETracerType type{};
};

// Одна запись в логе попаданий (виджет "Shot logs").
struct ShotLogEntry_t {
	std::string name;
	int damage{};
	int hitgroup{};
	float distance{};
	unsigned long long time{};
};

struct CEvents {
	void OnFireGameEvent(CGameEvent*);
	void OnBulletImpact(CGameEvent*);
	void OnPlayerHurt(CGameEvent*);
	void OnWeaponFire(CGameEvent*);
	void OnPlayerDeath(CGameEvent*);
	void OnCreateMove();

	PendingImpact pending_local_impact;
	std::unordered_map<uintptr_t, PendingImpact> pending_remote_impacts;

	// Лог попаданий для виджета "Shot logs". Пишется из игрового потока
	// (OnPlayerHurt), читается из потока рендера (Gui).
	std::deque<ShotLogEntry_t> shot_logs;
	std::mutex shot_logs_mutex;

	void FlushPendingImpacts();
	void FlushLocalImpacts(int);
	void FlushRemoteImpacts(int);
	void SpawnTracer(PendingImpact&);
	void FireClientTracer(Vector&, Vector&);
	void PlayVsnd(const char*);
};

inline CEvents* Events = new CEvents();