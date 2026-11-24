#include <shared.h>
#include "Helpers/HitboxPacket.h"
#include "Helpers/FeatureDebug.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstring>

static Vector DirectionToAngle(const Vector& direction) {
	return {
		-std::atan2(direction.z, std::hypot(direction.x, direction.y)) * Math::rads,
		std::atan2(direction.y, direction.x) * Math::rads,
		0.f
	};
}

static float AngleDistance(const Vector& first, const Vector& second) {
	return std::hypot(std::remainder(first.x - second.x, 360.f), std::remainder(first.y - second.y, 360.f));
}

bool CLegitbot::IsWeaponReady() const {
	if (!ctx.pawn || !ctx.controller || !ctx.weapon || !ctx.weapon_data || !ctx.pawn->Alive())
		return false;

	const int weapon_type = ctx.weapon_data->m_WeaponType();
	if (weapon_type < WEAPONTYPE_PISTOL || weapon_type > WEAPONTYPE_MACHINEGUN)
		return false;

	if (Config::Get<bool>(Vars::Legitbot::ScopeOnly) && ctx.weapon_data->m_nZoomLevels() > 0 && !ctx.pawn->m_bIsScoped())
		return false;

	// CanShoot() намеренно НЕ проверяем: аим должен продолжать наводиться
	// и во время восстановления оружия. Сам выстрел всё равно проверяется
	// в Fire() - там CanShoot() обязателен.
	if (ctx.weapon->m_iClip1() <= 0 || ctx.weapon->m_bInReload())
		return false;

	return true;
}

int CLegitbot::HitboxGroup(const LagHitbox_t& hitbox) const {
	static constexpr int groups[] = {-1, 0, 1, 2, 3, 3, 4, 4, 1};
	if (hitbox.hitgroup < 0 || hitbox.hitgroup >= static_cast<int>(sizeof(groups) / sizeof(groups[0])))
		return -1;

	return hitbox.foot && (hitbox.hitgroup == 6 || hitbox.hitgroup == 7) ? 5 : groups[hitbox.hitgroup];
}

float CLegitbot::MinimumDamage(C_CSPlayerPawn* player) const {
	const int wanted = Config::Get<int>(Vars::Legitbot::MinimumDamage);
	if (!player || wanted < 100)
		return static_cast<float>(wanted);

	return static_cast<float>(std::min(wanted, std::max(player->m_iHealth(), 1)));
}

bool CLegitbot::UpdateAccuracy() {
	if (!ctx.weapon || !ctx.weapon_data || !ctx.cmd)
		return false;

	float inaccuracy = EnginePrediction ? EnginePrediction->WeaponInaccuracy() : 0.f;
	float spread = EnginePrediction ? EnginePrediction->WeaponSpread() : 0.f;

	if (!EnginePrediction || !EnginePrediction->WeaponAccuracyValid() || EnginePrediction->WeaponAccuracyCommand() != ctx.cmd->command_number) {
		const float penalty = ctx.weapon->m_fAccuracyPenalty();
		const float recoil = ctx.weapon->m_flRecoilIndex();
		const float turning = ctx.weapon->m_flTurningInaccuracy();
		const float turning_delta = ctx.weapon->m_flTurningInaccuracyDelta();
		const Vector turning_direction = ctx.weapon->m_vecTurningInaccuracyEyeDirLast();
		const float update_time = ctx.weapon->m_flLastAccuracyUpdateTime();

		const bool updated = ctx.weapon->UpdateAccuracyPenalty();
		if (updated) {
			inaccuracy = ctx.weapon->GetInaccuracy();
			spread = ctx.weapon->GetSpread();
		}

		ctx.weapon->m_fAccuracyPenalty() = penalty;
		ctx.weapon->m_flRecoilIndex() = recoil;
		ctx.weapon->m_flTurningInaccuracy() = turning;
		ctx.weapon->m_flTurningInaccuracyDelta() = turning_delta;
		ctx.weapon->m_vecTurningInaccuracyEyeDirLast() = turning_direction;
		ctx.weapon->m_flLastAccuracyUpdateTime() = update_time;

		if (!updated)
			return false;
	}

	// Реальные значения нужны для компенсации разброса в seed trigger:
	// сервер всё равно применит свой разброс, cvar его не отключает.
	shot_inaccuracy = inaccuracy;
	shot_spread = spread;

	// Seed trigger использует детерминированный выстрел: разброс обнуляется
	// так же, как в NoSpread, но точка прицеливания берётся по верхней точке головы.
	if (Config::Get<bool>(Vars::Legitbot::SeedTrigger)) {
		inaccuracy = 0.f;
		spread = 0.f;
	}

	static auto random_seed = reinterpret_cast<CUniformRandomStreamState::Constructor_t>(
		GetProcAddress(static_cast<HMODULE>(Memory->Modules.Tier0.Handle()), xs("??0?$CUniformRandomStreamImpl@VCThreadNullMutex@@@@QEAA@H@Z")));
	static auto random_float = reinterpret_cast<CUniformRandomStreamState::RandomFloat_t>(
		GetProcAddress(static_cast<HMODULE>(Memory->Modules.Tier0.Handle()), xs("?RandomFloat@?$CUniformRandomStreamImpl@VCThreadNullMutex@@@@QEAAMMM@Z")));

	return random_seed && random_float && spread_cache.Update(inaccuracy, spread, ctx.weapon_data->m_bIsRevolver(), random_seed, random_float);
}

namespace {
	// CS2 использует Park-Miller (ran1), а не CUniformRandomStream из тира0.
	struct CRandomStream {
		static constexpr int IA = 16807, IM = 2147483647, IQ = 127773, IR = 2836;
		static constexpr int NTAB = 32, NDIV = 1 + (IM - 1) / NTAB;
		int idum = 0, iy = 0, iv[NTAB]{};

		void Seed(int s) {
			idum = s < 0 ? s : -s;
			iy = 0;
		}

		int Next() {
			if (idum <= 0 || !iy) {
				if (-idum < 1)
					idum = 1;
				else
					idum = -idum;

				for (int j = NTAB + 7; j >= 0; --j) {
					const int k = idum / IQ;
					idum = IA * (idum - k * IQ) - IR * k;
					if (idum < 0)
						idum += IM;
					if (j < NTAB)
						iv[j] = idum;
				}
				iy = iv[0];
			}

			const int k = idum / IQ;
			idum = IA * (idum - k * IQ) - IR * k;
			if (idum < 0)
				idum += IM;

			const int j = iy / NDIV;
			iy = iv[j];
			iv[j] = idum;
			return iy;
		}

		float RandomFloat(float lo, float hi) {
			float value = static_cast<float>(Next()) * (1.f / static_cast<float>(IM));
			if (value > 0.99999988f)
				value = 0.99999988f;
			return value * (hi - lo) + lo;
		}
	};

	// ComputeRandomSeed в CS2 = первые 4 байта SHA1 от
	// { квантованный pitch, квантованный yaw, tick } (12 байт).
	// Реализуем сами, чтобы не зависеть от сигнатуры движковой функции.
	class CSeedHash {
	  public:
		void Reset() {
			state[0] = 0x67452301u; state[1] = 0xEFCDAB89u; state[2] = 0x98BADCFEu;
			state[3] = 0x10325476u; state[4] = 0xC3D2E1F0u;
			count = 0;
		}

		void Update(const void* data, size_t len) {
			const auto* bytes = static_cast<const uint8_t*>(data);
			size_t i = static_cast<size_t>(count & 63);
			count += len;
			size_t offset = 0;
			if (i) {
				const size_t part = 64 - i;
				if (len >= part) {
					std::memcpy(buffer + i, bytes, part);
					Transform(buffer);
					offset = part;
				} else {
					std::memcpy(buffer + i, bytes, len);
					return;
				}
			}
			for (; offset + 64 <= len; offset += 64)
				Transform(bytes + offset);
			if (offset < len)
				std::memcpy(buffer, bytes + offset, len - offset);
		}

		uint32_t Finish() {
			uint8_t padding[64]{};
			padding[0] = 0x80;
			const size_t index = static_cast<size_t>(count & 63);
			const size_t pad_len = index < 56 ? 56 - index : 120 - index;
			const uint64_t bits = count * 8;
			Update(padding, pad_len);
			uint8_t length[8]{};
			for (int i = 0; i < 8; ++i)
				length[7 - i] = static_cast<uint8_t>(bits >> (i * 8));
			Update(length, 8);

			uint32_t digest[5]{};
			for (int i = 0; i < 5; ++i)
				digest[i] = state[i];
			return digest[0];
		}

	  private:
		static uint32_t Rotl(uint32_t value, int bits) {
			return (value << bits) | (value >> (32 - bits));
		}

		void Transform(const uint8_t* block) {
			uint32_t words[80]{};
			for (int i = 0; i < 16; ++i)
				words[i] = static_cast<uint32_t>(block[i * 4]) << 24 |
					static_cast<uint32_t>(block[i * 4 + 1]) << 16 |
					static_cast<uint32_t>(block[i * 4 + 2]) << 8 |
					static_cast<uint32_t>(block[i * 4 + 3]);
			for (int i = 16; i < 80; ++i)
				words[i] = Rotl(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

			uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
			for (int i = 0; i < 80; ++i) {
				uint32_t f, k;
				if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
				else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
				else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
				else { f = b ^ c ^ d; k = 0xCA62C1D6u; }

				const uint32_t temp = Rotl(a, 5) + f + e + k + words[i];
				e = d; d = c; c = Rotl(b, 30); b = a; a = temp;
			}
			state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
		}

		uint32_t state[5]{};
		uint64_t count = 0;
		uint8_t buffer[64]{};
	};

	static float SeedNormalize(float value) {
		return value - std::floor(value / 360.f + 0.5f) * 360.f;
	}
}

uint32_t CLegitbot::SeedFor(const Vector& angle, int tick) const {
	// Квантование до 0.5 градуса происходит в самом ComputeRandomSeed,
	// поэтому повторяем его здесь перед хэшированием.
	struct SeedInput_t {
		float pitch;
		float yaw;
		int tick;
	} input{
		std::floor(SeedNormalize(angle.x) * 2.f) * 0.5f,
		std::floor(SeedNormalize(angle.y) * 2.f) * 0.5f,
		tick
	};

	CSeedHash hash;
	hash.Reset();
	hash.Update(&input, sizeof(input));
	const uint32_t seed = hash.Finish();
	if (seed)
		return seed;

	// Резервный путь - старый вызов движковой функции.
	using ComputeRandomSeed_t = uint32_t(__fastcall*)(void*, Vector*, uint32_t);
	static auto compute = reinterpret_cast<ComputeRandomSeed_t>(
		Memory->Modules.Client.Find(xs("48 89 5C 24 08 57 48 81 EC F0 00 00 00 F3 0F 10 0A 48 8D 8C 24")).As<void*>());
	if (!compute)
		return 0;
	Vector angles{ std::remainder(angle.x, 360.f), std::remainder(angle.y, 360.f), 0.f };
	return compute(nullptr, &angles, static_cast<uint32_t>(tick));
}

int CLegitbot::ShotTick() const {
	// Сервер хэширует tick из globalvars (renderer tick count), а не
	// client_tick команды и не tickbase контроллера. В ttt-коде сид
	// считается как tick_count-1 / tick_count - оба проверяются.
	return GlobalVars ? GlobalVars->tick_count : (ctx.controller ? ctx.controller->m_nTickBase() : 0);
}

uint32_t CLegitbot::ComputeRandomSeed(const Vector& angle, int tick) const {
	const uint32_t seed = SeedFor(angle, tick);
	if (seed)
		return seed;
	return ctx.cmd ? static_cast<uint32_t>(ctx.cmd->pb.base().random_seed()) : 0u;
}

Vector CLegitbot::SolveSpreadAngle(const Vector& desired, int tick, uint32_t& out_seed) const {
	// Сервер считает разброс из сида, который зависит от live view angle.
	// Просто вычесть offset из прицела мало: сменив угол, мы сменим и сид.
	// Ищем самосогласованный угол: его сид даёт offset, а вычитание этого
	// offset из желаемого угла возвращает тот же самый сид.
	Vector best = desired;
	uint32_t best_seed = 0;
	float best_error = FLT_MAX;

	const float base_pitch = std::floor(desired.x * 2.f) * 0.5f;
	const float base_yaw = std::floor(desired.y * 2.f) * 0.5f;

	// Оба тика: в субтике атаки движок берёт tickbase или tickbase-1.
	for (int tick_delta = 0; tick_delta <= 1; ++tick_delta) {
		const int use_tick = tick - tick_delta;

		for (int ip = -4; ip <= 4; ++ip) {
			for (int iy = -4; iy <= 4; ++iy) {
				const Vector probe{ base_pitch + ip * 0.5f, base_yaw + iy * 0.5f, 0.f };
				const uint32_t seed = SeedFor(probe, use_tick);
				if (!seed)
					continue;

				float offset_x = 0.f, offset_y = 0.f;
				SpreadOffset(seed, offset_x, offset_y);

				// offset_x = right/yaw, offset_y = up/pitch. Сервер прибавит
				// их к directions: right*offset_x + up*offset_y. Повышение
				// pitch, наоборот, опускает forward (forward.z = -sin(pitch)),
				// поэтому компенсация = +offset_y в pitch и +offset_x в yaw.
				Vector candidate{ desired.x + offset_y, desired.y + offset_x, 0.f };
				candidate.x = std::remainder(candidate.x, 360.f);
				candidate.y = std::remainder(candidate.y, 360.f);

				// Сид должен остаться тем же - иначе сервер применит другой
				// offset и пуля снова уйдёт мимо.
				if (SeedFor(candidate, use_tick) != seed)
					continue;

				const float error = std::hypot(candidate.x - probe.x, candidate.y - probe.y);
				if (error < best_error) {
					best_error = error;
					best = candidate;
					best_seed = seed;
				}
			}
		}
	}

	if (best_seed) {
		out_seed = best_seed;
		return best;
	}

	// Резервный путь: если строгий поиск не нашёл угол с тем же сидом,
	// сходимся итеративно. Это не так точно, но всё равно компенсирует
	// большую часть разброса, а не оставляет выстрел без предсказания.
	for (int tick_delta = 0; tick_delta <= 1; ++tick_delta) {
		const int use_tick = tick - tick_delta;
		Vector candidate = desired;
		uint32_t seed = 0;

		for (int iteration = 0; iteration < 6; ++iteration) {
			seed = SeedFor(candidate, use_tick);
			if (!seed)
				break;

			float offset_x = 0.f, offset_y = 0.f;
			SpreadOffset(seed, offset_x, offset_y);
			candidate.x = std::remainder(desired.x + offset_y, 360.f);
			candidate.y = std::remainder(desired.y + offset_x, 360.f);
		}

		if (seed) {
			out_seed = seed;
			return candidate;
		}
	}

	out_seed = 0;
	return desired;
}

bool CLegitbot::SpreadOffset(uint32_t seed, float& out_x, float& out_y) const {
	// CS2: CalcSpread получает ComputeRandomSeed()+1 и использует
	// Park-Miller (ran1), а не тир0 CUniformRandomStream.
	CRandomStream stream;
	stream.Seed(static_cast<int>((seed + 1u) & 0x7fffffffu));
	constexpr float circle = 6.2831853071795864769f;
	const bool revolver = ctx.weapon_data && ctx.weapon_data->m_bIsRevolver();

	const float a_raw = stream.RandomFloat(0.f, 1.f);
	const float b = stream.RandomFloat(0.f, circle);
	const float c_raw = stream.RandomFloat(0.f, 1.f);
	const float d = stream.RandomFloat(0.f, circle);

	int item_index = 0;
	if (ctx.weapon && ctx.weapon->m_AttributeManager())
		if (auto item = ctx.weapon->m_AttributeManager()->m_Item())
			item_index = item->m_iItemDefinitionIndex();
	const float recoil_index = ctx.weapon ? ctx.weapon->m_flRecoilIndex() : 0.f;

	auto reshape = [&](float value) {
		if (item_index == 64 && revolver) // R8
			return 1.f - value * value;
		if (item_index == 28 && recoil_index < 3.f) { // Negev
			float result = value;
			for (int count = 3; count > 0;) {
				--count;
				result *= result;
				if (!(static_cast<float>(count) > recoil_index))
					break;
			}
			return 1.f - result;
		}
		return value;
	};

	const float a = reshape(a_raw);
	const float c = reshape(c_raw);

	// out_x = right/yaw, out_y = up/pitch - как в SpreadCache.
	out_x = std::cos(b) * a * shot_inaccuracy + std::cos(d) * c * shot_spread;
	out_y = std::sin(b) * a * shot_inaccuracy + std::sin(d) * c * shot_spread;
	return shot_inaccuracy > 0.f || shot_spread > 0.f;
}

bool CLegitbot::PredictedSpreadHit(const Vector& eye, const Vector& view, C_CSPlayerPawn* target,
	bool penetrating, uint32_t& out_seed, float& out_offset) const {
	if (!target || !ctx.weapon_data || !ctx.pawn)
		return false;

	const int base_tick = ShotTick();
	const float range = ctx.weapon_data->m_flRange();

	// Сид считается из угла команды без aim punch, а направление пули -
	// уже с учётом отдачи (так делает движок).
	Vector shot_view = view;
	if (auto aim_punch = ctx.pawn->m_pAimPunchServices()) {
		Vector punch{};
		aim_punch->ComputeAimPunch(punch, ctx.player_time, true);
		shot_view.x += punch.x;
		shot_view.y += punch.y;
	}
	shot_view.x = std::remainder(shot_view.x, 360.f);
	shot_view.y = std::remainder(shot_view.y, 360.f);

	Vector forward, right, up;
	Math::Angle2Vectors(shot_view, &forward, &right, &up);

	// В субтике атаки сид может быть как tick, так и tick-1. Требуем
	// попадания в обоих случаях - иначе выстрел всё равно будет мазать.
	int hits = 0;
	for (int delta = 0; delta < 2; ++delta) {
		const int tick = base_tick - 1 + delta;
		const uint32_t seed = SeedFor(view, tick);
		out_seed = seed;
		float offset_x = 0.f, offset_y = 0.f;
		if (!SpreadOffset(seed, offset_x, offset_y))
			return false;
		out_offset = std::hypot(offset_x, offset_y);

		const Vector direction = (forward + right * offset_x + up * offset_y).Normalized();
		CAutoWall::Result_t hit;
		if (!AutoWall->Run(eye, eye + direction * range, target, hit, penetrating, range) ||
			hit.damage < MinimumDamage(target))
			return false;

		++hits;
	}

	return hits == 2;
}

std::vector<LagRecord*> CLegitbot::SelectRecords(C_CSPlayerPawn* player, int processing_tick) const {
	std::vector<LagRecord*> records;
	if (!player)
		return records;

	auto found = LagCompensation->lag_records.find(player->GetHandle().ToInt());
	if (found != LagCompensation->lag_records.end()) {
		for (auto it = found->second.rbegin(); it != found->second.rend(); ++it) {
			LagRecord* record = &*it;
			if (!LagCompensation->ValidRecord(record, processing_tick))
				continue;

			records.push_back(record);
			if (records.size() >= 4)
				break;
		}
	}

	if (Config::Get<bool>(Vars::Legitbot::Extrapolation)) {
		if (LagRecord* extrapolated = LagCompensation->GetExtrapolatedRecord(player, processing_tick)) {
			if (LagCompensation->ValidRecord(extrapolated, processing_tick) &&
				std::find(records.begin(), records.end(), extrapolated) == records.end())
				records.insert(records.begin(), extrapolated);
		}
	}

	return records;
}

std::vector<CLegitbot::Point_t> CLegitbot::BuildPoints(LagRecord* record, const Vector& eye) const {
	std::vector<Point_t> points;
	if (!record || record->geometry.empty())
		return points;

	const auto& enabled = Config::Get<std::vector<bool>>(Vars::Legitbot::Hitboxes);
	const auto& multipoints = Config::Get<std::vector<bool>>(Vars::Legitbot::Multipoints);
	auto enabled_group = [&](int group) {
		return group >= 0 && group < static_cast<int>(enabled.size()) && enabled[group];
	};
	auto multipoint_group = [&](int group) {
		return group >= 0 && group < static_cast<int>(multipoints.size()) && multipoints[group];
	};

	Vector view = record->m_vecOrigin + (record->m_vecMins + record->m_vecMaxs) * 0.5f - eye;
	if (view.Normalize() <= 0.0001f)
		view = Vector(1.f, 0.f, 0.f);

	Vector right = view.Cross(Vector(0.f, 0.f, 1.f));
	if (right.Normalize() <= 0.0001f)
		right = view.Cross(Vector(1.f, 0.f, 0.f)).Normalized();

	Vector up = right.Cross(view).Normalized();
	const Vector directions[] = {right, right * -1.f, up, (up + right).Normalized(), (up - right).Normalized()};
	const float head_scale = std::clamp(Config::Get<float>(Vars::Legitbot::PointScale) * 0.01f, 0.f, 1.f) * 0.99f;
	const float body_scale = std::clamp(Config::Get<float>(Vars::Legitbot::BodyPointScale) * 0.01f, 0.f, 1.f) * 0.99f;

	for (size_t i = 0; i < record->geometry.size() && i < record->hitboxes.size(); ++i) {
		const auto& geometry = record->geometry[i];
		const int group = HitboxGroup(record->hitboxes[i]);
		if (!geometry.valid || !enabled_group(group))
			continue;

		points.push_back({geometry.center, {}, 0.f, 0.f, record, static_cast<int>(i), group});
		if (!multipoint_group(group))
			continue;

		const float scale = group == 0 ? head_scale : body_scale;
		if (scale <= 0.f)
			continue;

		const int count = group == 0 ? 5 : 2;
		for (int direction = 0; direction < count; ++direction) {
			const Vector point = geometry.Contour(eye, view, directions[direction], scale);
			if ((point - geometry.center).LengthSqr() > 0.00000001f)
				points.push_back({point, {}, 0.f, 0.f, record, static_cast<int>(i), group});
		}
	}

	return points;
}

float CLegitbot::CalcHitchance(LagRecord* record, const Vector& eye, const Vector& angle, int group) {
	const float required = std::clamp(Config::Get<float>(Vars::Legitbot::Hitchance) * 0.01f, 0.f, 1.f);
	if (required <= 0.f || !record || record->geometry.empty())
		return 1.f;

	if (!(spread_cache.maximum_radius > 0.f))
		return 1.f;

	if (hitchance_record != record || hitchance_revision != record->revision ||
		hitchance_spread_revision != spread_cache.Revision() ||
		hitchance_eye.x != eye.x || hitchance_eye.y != eye.y || hitchance_eye.z != eye.z) {
		const double maximum_spread = std::sqrt(spread_cache.SortedRadiusSquared.back());
		hitchance_rays.resize(record->geometry.size());
		for (size_t i = 0; i < record->geometry.size(); ++i)
			if (record->geometry[i].valid)
				record->geometry[i].PrepareRay(eye, hitchance_rays[i], maximum_spread);

		hitchance_record = record;
		hitchance_revision = record->revision;
		hitchance_spread_revision = spread_cache.Revision();
		hitchance_eye = eye;
	}

	Vector forward, right, up;
	Math::Angle2Vectors(angle, &forward, &right, &up);

	if (!HitboxGeometry_t::CanReachHitchance(record->geometry, hitchance_rays, spread_cache.SortedRadiusSquared,
		forward, right, up, group, required))
		return std::nextafter(required, 0.f);

	desired_hitboxes.clear();
	other_hitboxes.clear();
	for (size_t i = 0; i < record->geometry.size(); ++i) {
		if (!record->geometry[i].valid)
			continue;

		(record->geometry[i].group == group ? desired_hitboxes : other_hitboxes).push_back(i);
	}

	if (desired_hitboxes.empty())
		return 0.f;

	return HitboxPacket::Hitchance(record->geometry, hitchance_rays, desired_hitboxes, other_hitboxes,
		spread_cache.offsets, eye, forward, right, up, required, ctx.weapon_data->m_flRange());
}

bool CLegitbot::EvaluatePoint(C_CSPlayerPawn* player, LagRecord* record, const Point_t& point, const Vector& eye,
	bool penetrating, Target_t& best) const {
	if (!player || !record || !ctx.weapon_data)
		return false;

	const float distance = point.distance;
	const float weapon_range = ctx.weapon_data->m_flRange();
	if (distance <= 1.f || distance > weapon_range)
		return false;

	const float fov_limit = std::clamp(Config::Get<float>(Vars::Legitbot::FieldOfView), 0.f, 180.f);
	// FOV должен совпадать с нарисованным кругом: без огромного margin'а,
	// который раньше позволял аиму брать цели далеко за пределами круга.
	// 1° - только на погрешность округления.
	if (fov_limit < 180.f && point.fov > fov_limit + 1.f)
		return false;

	// Убрана отдельная IsVisible-трасса: при penetrating == false автовалл
	// уже останавливается на первой же стене, поэтому второй вызов был
	// просто двойной трассой на каждую точку.
	CAutoWall::Result_t result;
	const float range = std::min(weapon_range, distance + 64.f);
	if (!AutoWall->Run(eye, point.point, player, result, penetrating, range))
		return false;

	if (result.damage < MinimumDamage(player))
		return false;

	// В Target_t.distance всегда кладём реальную дистанцию, а не "счёт",
	// чтобы сортировка в FindTarget была единой и предсказуемой.
	if (best.player && !(point.fov < best.fov || (point.fov == best.fov && distance < best.distance)))
		return false;

	best.player = player;
	best.record = record;
	best.point = point.point;
	best.angle = point.angle;
	best.damage = result.damage;
	best.distance = distance;
	best.fov = point.fov;
	best.hitbox = point.hitbox;
	best.group = point.group;
	return true;
}

bool CLegitbot::FindTarget(const Vector& eye, const Vector& view, bool penetrating, Target_t& target, int preferred_handle, bool require_hitchance) {
	if (!ctx.pawn || !ctx.weapon_data)
		return false;

	constexpr size_t point_limit = 8;
	const int processing_tick = LagCompensation->GetProcessingTick();
	const float fov_limit = std::clamp(Config::Get<float>(Vars::Legitbot::FieldOfView), 0.f, 180.f);

	// Сначала собираем все прошедшие автовалл кандидаты, и только для
	// лучших по score считаем дорогой хитчанс. Раньше он считался для
	// каждой точки каждой записи — это и было главной причиной просадок.
	std::vector<Target_t> candidates;
	candidates.reserve(64);

	for (size_t index = 0; index < EntityList->player_count; ++index) {
		auto& entry = EntityList->players[index];
		if (!entry.Fill())
			continue;

		C_CSPlayerPawn* player = entry.pawn;
		if (!player || player == ctx.pawn || !player->Alive() || !player->Enemy(ctx.pawn) || player->m_bGunGameImmunity())
			continue;

		auto node = player->m_pGameSceneNode();
		if (!node)
			continue;

		Vector center = node->m_vecAbsOrigin();
		center.z += 36.f;
		const float rough_distance = (center - eye).Length();
		if (rough_distance <= 1.f || rough_distance > ctx.weapon_data->m_flRange())
			continue;

		for (LagRecord* record : SelectRecords(player, processing_tick)) {
			std::vector<Point_t> points = BuildPoints(record, eye);
			if (points.empty()) {
				for (size_t i = 0; i < record->geometry.size() && i < record->hitboxes.size(); ++i) {
					if (!record->geometry[i].valid || HitboxGroup(record->hitboxes[i]) != 0)
						continue;

					points.push_back({record->geometry[i].center, {}, 0.f, 0.f, record, static_cast<int>(i), 0});
					break;
				}
			}

			if (points.empty())
				continue;

			// Угол/FOV/дистанция одной точки считаются один раз и переиспользуются.
			for (Point_t& point : points) {
				const Vector delta = point.point - eye;
				point.distance = delta.Length();
				if (point.distance <= 1.f || point.distance > ctx.weapon_data->m_flRange())
					continue;

				point.angle = DirectionToAngle(delta);
				point.fov = AngleDistance(point.angle, view);
			}

			std::sort(points.begin(), points.end(), [](const Point_t& first, const Point_t& second) {
				return first.fov < second.fov;
			});

			const size_t limit = std::min(points.size(), point_limit);
			for (size_t i = 0; i < limit; ++i) {
				const Point_t& point = points[i];
				if (point.distance <= 1.f || point.distance > ctx.weapon_data->m_flRange())
					continue;

				if (fov_limit < 180.f && point.fov > fov_limit + 1.f)
					continue;

				Target_t candidate{};
				if (EvaluatePoint(player, record, point, eye, penetrating, candidate))
					candidates.push_back(candidate);
			}
		}
	}

	// Главный приоритет - closeness к прицелу. Именно поэтому "голова в
	// FOV, а чит не наводится": раньше сортировка шла по дистанции и аим
	// уходил к ближайшему врагу, игнорируя того, на кого смотрит игрок.
	const int selection = std::clamp(Config::Get<int>(Vars::Legitbot::TargetSelection), 0, 2);
	std::sort(candidates.begin(), candidates.end(), [&](const Target_t& first, const Target_t& second) {
		// mode 1 = чистый FOV, mode 0/2 = FOV как основной ключ, остальное - тай-брейк.
		if (selection != 1 && std::fabs(first.fov - second.fov) <= 2.5f) {
			if (selection == 2 && first.player && second.player)
				return first.player->m_iHealth() < second.player->m_iHealth();
			if (first.distance != second.distance)
				return first.distance < second.distance;
		}
		return first.fov < second.fov;
	});

	const float required = std::clamp(Config::Get<float>(Vars::Legitbot::Hitchance) * 0.01f, 0.f, 1.f);
	const Target_t* first_valid = nullptr;
	const Target_t* preferred = nullptr;

	for (const Target_t& candidate : candidates) {
		// Хитчанс нужен только для решения "стрелять/не стрелять". Для аима
		// и автостопа он не должен ронять цель - иначе прицел "отлипает".
		if (require_hitchance && CalcHitchance(candidate.record, eye, candidate.angle, candidate.group) < required)
			continue;

		if (!first_valid)
			first_valid = &candidate;

		const int handle = candidate.player ? candidate.player->GetHandle().ToInt() : -1;
		if (preferred_handle >= 0 && handle == preferred_handle && candidate.fov <= first_valid->fov + 6.f) {
			preferred = &candidate;
			break;
		}

		// Если предыдущая цель почти в прицеле - не перескакиваем на соседнюю.
		if (first_valid && candidate.fov < first_valid->fov - 2.5f)
			break;
	}

	if (!first_valid)
		return false;

	target = preferred ? *preferred : *first_valid;
	return true;
}

void CLegitbot::UseTopHead(LagRecord* record, const Vector& eye, Target_t& target) const {
	if (!record)
		return;

	float highest = -FLT_MAX;
	int head = -1;
	for (size_t i = 0; i < record->geometry.size() && i < record->hitboxes.size(); ++i) {
		const auto& geometry = record->geometry[i];
		if (!geometry.valid || HitboxGroup(record->hitboxes[i]) != 0)
			continue;

		const float top = std::max(geometry.first.z, geometry.last.z);
		if (top > highest) {
			highest = top;
			head = static_cast<int>(i);
		}
	}

	if (head < 0)
		return;

	const auto& geometry = record->geometry[head];
	target.point = geometry.center;
	target.point.z = highest - 0.01f;
	target.hitbox = head;
	target.group = 0;
	target.angle = DirectionToAngle(target.point - eye);
}

bool CLegitbot::RunKnifebot(CCSGOInput* input, const Vector& eye) {
	if (!Config::Get<bool>(Vars::Legitbot::Knifebot) || !ctx.weapon_data || ctx.weapon_data->m_WeaponType() != WEAPONTYPE_KNIFE)
		return false;
	if (!ctx.pawn || !ctx.controller || !ctx.weapon)
		return false;

	const int processing_tick = LagCompensation->GetProcessingTick();
	for (size_t index = 0; index < EntityList->player_count; ++index) {
		auto& entry = EntityList->players[index];
		if (!entry.Fill())
			continue;

		C_CSPlayerPawn* player = entry.pawn;
		if (!player || player == ctx.pawn || !player->Alive() || !player->Enemy(ctx.pawn) || player->m_bGunGameImmunity())
			continue;

		for (LagRecord* record : SelectRecords(player, processing_tick)) {
			Vector point = record->m_vecOrigin;
			point.z = std::clamp(eye.z, point.z, point.z + record->m_vecMaxs.z) - 0.01f;
			Vector delta = point - eye;
			if (delta.LengthSqr() > 6400.f)
				continue;

			Vector facing;
			Math::Angle2Vectors(Vector(0.f, record->m_angEyeAngles.y, 0.f), &facing);
			Vector relative = record->m_vecOrigin - ctx.pawn->m_pGameSceneNode()->m_vecAbsOrigin();
			relative.z = 0.f;
			const bool backstab = relative.Normalized().Dot(facing) > 0.475f;

			const float now = TICKS_TO_TIME(ctx.controller->m_nTickBase());
			const float next = TICKS_TO_TIME(ctx.weapon->m_nNextPrimaryAttackTick()) + ctx.weapon->m_flNextPrimaryAttackTickRatio() * INTERVAL_PER_TICK;
			const bool first = next + 0.4f < now;
			const int light = backstab ? 90 : (first ? 40 : 25);
			const int heavy = backstab ? 180 : 65;
			const bool stab = heavy >= player->m_iHealth() && light < player->m_iHealth();
			const float range = stab ? 32.f : 48.f;
			const int tick = stab ? ctx.weapon->m_nNextSecondaryAttackTick() : ctx.weapon->m_nNextPrimaryAttackTick();
			const float fraction = stab ? ctx.weapon->m_flNextSecondaryAttackTickRatio() : ctx.weapon->m_flNextPrimaryAttackTickRatio();
			if (Taia_t(ctx.controller->m_nTickBase(), 0.f) < Taia_t(tick, fraction))
				continue;

			CLagRecordScope scope(record);
			if (!scope)
				continue;

			Vector start = eye;
			Vector end = start + delta.Normalized() * range;
			Vector mins(-16.f, -16.f, -18.f), maxs(16.f, 16.f, 18.f);
			CRay ray{};
			ray.Init(mins, maxs);
			auto filter = Physics->MakeFilter(ctx.pawn, 0x1C3003, 3, 15);
			auto trace = Physics->MakeTrace();
			Physics->TraceRay(&ray, &start, &end, &filter, &trace);
			if (trace.entity != player || trace.fraction >= 1.f) {
				CTraceData clipped;
				Vector clip_delta = delta.Normalized() * (range + 18.f);
				auto clip_filter = Physics->MakeFilter(ctx.pawn, 0x1C300B, 3, 15);
				auto target_filter = Physics->MakeFilter(player, 0x1C300B, 3, 15);
				clip_filter.shape_attr.entity_ids_to_ignore[1] = record->hitbox_list_context
					? static_cast<uint32_t>(record->hitbox_list_context)
					: target_filter.shape_attr.entity_ids_to_ignore[0];
				Physics->SetupTrace(&clipped, start, clip_delta, clip_filter, 0);

				if (Physics->ClipTraceToPlayer(&clipped, player)) {
					for (int segment = 0; segment < clipped.filled_segment_count; ++segment) {
						const auto& hit = clipped.trace_segments[segment];
						if (hit.entity_handle != static_cast<uint32_t>(player->GetHandle().ToInt()) || hit.fraction >= trace.fraction)
							continue;

						trace.entity = player;
						trace.fraction = hit.fraction;
						trace.end_pos = start + clip_delta * hit.fraction;
					}
				}
			}

			if (trace.entity != player || trace.fraction >= 1.f || (trace.end_pos - eye).LengthSqr() > range * range)
				continue;

			if (Fire(DirectionToAngle(delta), record))
				return true;
		}
	}

	return false;
}

bool CLegitbot::ApplyAngles(CCSGOInput* input, const Vector& angle) const {
	if (!input || !ctx.cmd || !ctx.base_cmd)
		return false;

	auto viewangles = ctx.base_cmd->mutable_viewangles();
	if (!viewangles)
		return false;

	// Раньше сюда добавляли (текущий_угол - цель) в subtick-дельты. Из-за этого
	// сервер получал ровно тот же итоговый угол, что был до аима — прицел
	// оставался на месте. subtick-дельты — это мышиный ввод тика, его нужно
	// оставить нетронутым и просто заменить базовый угол команды.
	const float pitch = std::remainder(angle.x, 360.f);
	const float yaw = std::remainder(angle.y, 360.f);

	viewangles->set_x(pitch);
	viewangles->set_y(yaw);
	viewangles->set_z(0.f);
	input->current_move_data.viewangles = {pitch, yaw, 0.f};
	// command_angles - тоже копия угла, из которой движок собирает команду.
	input->current_move_data.command_angles = {pitch, yaw, 0.f};

	// В CS2 направление выстрела берётся не из base.viewangles, а из
	// input_history по attack1_start_history_index. Движок успел записать
	// туда углы до нашего хука, поэтому без этой синхронизации прицел
	// двигался, а пуля летела по старому (мышиному) углу.
	for (auto i = 0; i < ctx.cmd->pb.input_history_size(); ++i) {
		auto history = ctx.cmd->pb.mutable_input_history(i);
		if (!history || !history->has_view_angles())
			continue;

		auto history_angles = history->mutable_view_angles();
		history_angles->set_x(pitch);
		history_angles->set_y(yaw);
		history_angles->set_z(0.f);
	}

	return true;
}

bool CLegitbot::Fire(const Vector& angle, LagRecord* record) {
	if (!ctx.cmd || !ctx.weapon || !ctx.weapon->CanShoot())
		return false;

	ctx.cmd->button_state.SetButton(IN_ATTACK, true);

	const int history_index = ctx.cmd->pb.input_history_size() - 1;
	if (history_index < 0)
		return true;

	ctx.cmd->pb.set_attack1_start_history_index(history_index);
	auto history = ctx.cmd->pb.mutable_input_history(history_index);
	if (!history)
		return true;

	auto viewangles = history->mutable_view_angles();
	if (!viewangles)
		return true;

	viewangles->set_x(std::remainder(angle.x, 360.f));
	viewangles->set_y(std::remainder(angle.y, 360.f));
	viewangles->set_z(0.f);

	if (record) {
		history->set_render_tick_count(record->m_nSimulationTick + 1);
		history->set_player_tick_count(ctx.player_time.tick);
		history->set_render_tick_fraction(0.f);
		history->set_player_tick_fraction(ctx.player_time.fraction);
	}

	return true;
}

Vector CLegitbot::Humanize(const Vector& angle) const {
	Vector result = angle;
	const float amount = std::clamp(Config::Get<int>(Vars::Legitbot::Humanization), 0, 100) / 100.f;
	const float jitter = amount * 1.5f;
	if (jitter > 0.f) {
		const float seed = static_cast<float>(ctx.cmd ? ctx.cmd->command_number : 0);
		const float pitch_noise = std::sin(seed * 12.9898f + 78.233f) * 43758.5453f;
		const float yaw_noise = std::sin(seed * 39.3467f + 11.135f) * 24634.6345f;
		result.x += std::remainder(pitch_noise, 2.f) * jitter;
		result.y += std::remainder(yaw_noise, 2.f) * jitter;
	}

	result.x = std::remainder(result.x, 360.f);
	result.y = std::remainder(result.y, 360.f);
	result.z = 0.f;
	return result;
}

bool CLegitbot::ShouldAutoStop(bool shot_possible) {
	peek_ticks = 0;
	if (!ctx.pawn || !ctx.controller)
		return false;

	// Выход из-за угла - стоп нужен, чтобы не стрелять на ходу.
	if (Movement && (Movement->PeekAssistState.active || Movement->PeekAssistState.returning || Movement->PeekAssistState.pending_shot_return)) {
		peek_ticks = 4;
		return true;
	}

	// Прямая трасса без лагкомпа не попадала в хитбоксы. Стоп опирается на
	// цель, которую уже проверил FindTarget с корректным фильтром записи.
	if (!shot_possible)
		return false;

	Vector velocity = ctx.pawn->m_vecVelocity();
	velocity.z = 0.f;
	if (velocity.Length2d() < 40.f)
		return false;

	peek_ticks = 4;
	return true;
}

void CLegitbot::OnPresent() {
	if (!Config::Get<bool>(Vars::Legitbot::ShowFov) || !ctx.pawn || !ctx.pawn->Alive() || !SwapChain)
		return;

	const float fov = std::clamp(Config::Get<float>(Vars::Legitbot::FieldOfView), 0.f, 180.f);
	if (fov <= 0.f || fov >= 180.f)
		return;

	int camera_fov = 90;
	if (auto camera = ctx.pawn->m_pCameraServices())
		camera_fov = camera->m_iFOV();
	if (camera_fov <= 0 || camera_fov > 179)
		camera_fov = 90;

	const float height = static_cast<float>(SwapChain->GetScreenHeight());
	const float width = static_cast<float>(SwapChain->GetScreenWidth());
	// m_iFOV в Source - ГОРИЗОНТАЛЬНЫЙ FOV. Раньше круг считался как
	// вертикальный и получался в aspect раз меньше: голова была видна
	// внутри круга, но аим (который считает углы честно) туда не целился.
	const float radius = width * 0.5f * std::tan(Math::Deg2Rad(fov * 0.5f)) /
		std::tan(Math::Deg2Rad(static_cast<float>(camera_fov) * 0.5f));
	const ImVec2 center(width * 0.5f, height * 0.5f);

	const Color_t color = Config::Get<Color_t>(Vars::Legitbot::FovColor);
	ImGui::GetBackgroundDrawList()->AddCircle(center, radius, IM_COL32(color.R(), color.G(), color.B(), color.A()), 64, 1.f);
}

void CLegitbot::PrepareAutoStop() {
	stop_command = -1;
	if (auto_stop_fire_tick >= 0)
		return;
	if (!Config::Get<bool>(Vars::Legitbot::AutoStop) || !ctx.cmd || !ctx.pawn || !ctx.controller || !ctx.weapon || !ctx.weapon_data ||
		!ctx.pawn->Alive() || !EnginePrediction || ctx.weapon->m_iClip1() <= 0 || ctx.weapon->m_bInReload())
		return;

	const int type = ctx.weapon_data->m_WeaponType();
	if (type < WEAPONTYPE_PISTOL || type > WEAPONTYPE_MACHINEGUN)
		return;
	if ((ctx.pawn->m_fFlags() & FL_FROZEN) || ctx.pawn->m_bIsDefusing() || ctx.pawn->m_nActualMoveType() != MOVETYPE_WALK)
		return;

	const auto& modes = Config::Get<std::vector<bool>>(Vars::Legitbot::AutoStopModes);
	auto mode = [&](int index) { return index < static_cast<int>(modes.size()) && modes[index]; };
	if (!mode(0) && !mode(1) && !mode(2) && !mode(3))
		return;

	// Автостоп не должен работать постоянно: только на входе в пик/выходе
	// на противника (PeekAssist или движение в сторону врага).
	if (!auto_stop_armed)
		return;

	const bool grounded = (EnginePrediction->m_fFlags & FL_ONGROUND) != 0;
	Vector velocity = ctx.pawn->m_vecVelocity();
	velocity.z = 0.f;

	const bool attack = (ctx.cmd->button_state.held & IN_ATTACK) != 0;
	const bool landed = !grounded && mode(3) && EnginePrediction->LandedThisCommand();

	// Ранний стоп срабатывает при выходе на цель ещё до выстрела: требование
	// CanShoot() отсекало почти все случаи (в логе готовность была 0), поэтому
	// Early фактически никогда не работал. Достаточно движения цели.
	bool allowed = false;
	if (grounded)
		allowed = (mode(0) && velocity.Length2d() > 8.f) || (mode(1) && attack && velocity.Length2d() > 8.f);
	else
		allowed = mode(2) || landed;

	if (!allowed) {
		if (FeatureDebug::Throttle(700, 500))
			FeatureDebug::Dump("stop: prepare reject grounded=%d attack=%d vel=%.0f modes=%d%d%d%d\n",
				grounded ? 1 : 0, attack ? 1 : 0, velocity.Length2d(),
				mode(0) ? 1 : 0, mode(1) ? 1 : 0, mode(2) ? 1 : 0, mode(3) ? 1 : 0);
		return;
	}

	auto base = ctx.cmd->pb.mutable_base();
	if (!base)
		return;

	stop_input = {base->forwardmove(), base->leftmove(), 0.f};
	stop_subtick_input.clear();
	stop_subtick_input.reserve(base->subtick_moves_size());
	for (const auto& step : base->subtick_moves())
		stop_subtick_input.push_back({step.analog_forward_delta(), step.analog_left_delta(), step.yaw_delta()});

	stop_command = ctx.cmd->command_number;
	stop_weapon = ctx.weapon->GetHandle().ToInt();
	predicted_stop_command = stop_command;
}

bool CLegitbot::ApplyAutoStop(int tick_offset) {
	if (auto_stop_fire_tick >= 0) {
		stop_command = -1;
		return false;
	}
	if (!ctx.cmd || stop_command != ctx.cmd->command_number || !ctx.weapon || stop_weapon != ctx.weapon->GetHandle().ToInt())
		return false;

	auto base = ctx.cmd->pb.mutable_base();
	if (!base)
		return false;

	base->set_forwardmove(0.f);
	base->set_leftmove(0.f);
	for (auto i = 0uz; i < static_cast<size_t>(base->subtick_moves_size()); ++i) {
		auto step = base->mutable_subtick_moves(static_cast<int>(i));
		step->set_analog_forward_delta(0.f);
		step->set_analog_left_delta(0.f);
	}

	if (FeatureDebug::Throttle(700, 500))
		FeatureDebug::Dump("stop: applied cmd=%d offset=%d fmove=%.0f lmove=%.0f subticks=%d\n",
			ctx.cmd->command_number, tick_offset, stop_input.x, stop_input.y, base->subtick_moves_size());

	if (tick_offset)
		applied_stop_command = ctx.cmd->command_number;
	return true;
}

void CLegitbot::RestoreAutoStop(CUserCommand* cmd) {
	if (!cmd || predicted_stop_command != cmd->command_number)
		return;

	auto base = cmd->pb.mutable_base();
	if (base) {
		base->set_forwardmove(stop_input.x);
		base->set_leftmove(stop_input.y);
		for (auto i = 0uz; i < stop_subtick_input.size() && i < static_cast<size_t>(base->subtick_moves_size()); ++i) {
			auto step = base->mutable_subtick_moves(static_cast<int>(i));
			step->set_analog_forward_delta(stop_subtick_input[i].x);
			step->set_analog_left_delta(stop_subtick_input[i].y);
			step->set_yaw_delta(stop_subtick_input[i].z);
		}
	}

	stop_subtick_input.clear();
	predicted_stop_command = -1;
	// stop_command оставляем: после предикта Movement-хуки ещё могут
	// перезаписать movement, поэтому финальный стоп применяется позже
	// в CreateMove. Сброс произойдёт в следующем PrepareAutoStop().
}

void CLegitbot::OnCreateMove(CCSGOInput* input) {
	cvar(weapon_accuracy_nospread);
	// Не включаем client-side nospread: на обычном сервере он всё равно
	// игнорируется, зато обнуляет нашу предиктируемую точность, из-за
	// чего нечего компенсировать. Seed работает через SpreadOffset().
	if (weapon_accuracy_nospread)
		weapon_accuracy_nospread->SetInt(0);

	if (!input || !ctx.cmd || !ctx.base_cmd || !ctx.controller || !ctx.pawn || !ctx.weapon || !ctx.weapon_data || !ctx.pawn->Alive())
		return;

	if (Config::Get<bool>(Vars::Antiaim::Enable))
		return;

	// Предыдущий выстрел триггера уже ушёл на сервер - снимаем button,
	// чтобы автоматическое оружие не стреляло непрерывно.
	if (trigger_shot_active && !input->primary_attack) {
		ctx.cmd->button_state.SetButton(IN_ATTACK, false);
		trigger_shot_active = false;
	}

	// Стоп после выстрела держим выключенным, пока оружие реально не
	// восстановилось. Для скаута это критично: обычный "снять флаг через
	// тик" позволял застопориться ещё до окончания восстановления.
	bool auto_stop_fired = auto_stop_fire_tick >= 0;
	if (auto_stop_fired) {
		const int since_fire = ctx.cmd->command_number - auto_stop_fire_tick;
		if (since_fire >= 3 && ctx.weapon->CanShoot()) {
			auto_stop_fired = false;
			auto_stop_fire_tick = -1;
		}
	}

	const bool seed_trigger = Config::Get<bool>(Vars::Legitbot::SeedTrigger);
	const bool spread_prediction = Config::Get<bool>(Vars::Legitbot::SpreadPrediction);
	const bool aim_active = Config::Get<bool>(Vars::Legitbot::Enable);
	const bool trigger_active = Config::Get<bool>(Vars::Legitbot::TriggerEnable);
	const bool autostop_enabled = Config::Get<bool>(Vars::Legitbot::AutoStop);
	const bool knifebot_enabled = Config::Get<bool>(Vars::Legitbot::Knifebot);
	if (!aim_active && !trigger_active && !autostop_enabled && !knifebot_enabled)
		return;

	const int weapon_type = ctx.weapon_data->m_WeaponType();
	const bool knife = weapon_type == WEAPONTYPE_KNIFE;
	if (!knife && !IsWeaponReady())
		return;

	if (Config::Get<float>(Vars::Legitbot::Hitchance) > 0.f || seed_trigger || spread_prediction)
		UpdateAccuracy();

	const bool attack_held = (ctx.cmd->button_state.held & IN_ATTACK) != 0 || (input->button_attack && input->button_attack->pressed);
	const bool penetrating = Config::Get<bool>(Vars::Legitbot::AutoWall);
	const int humanization = std::clamp(Config::Get<int>(Vars::Legitbot::Humanization), 0, 100);

	Vector eye = ctx.pawn->GetEyePosition();
	if (!eye.Valid()) {
		auto node = ctx.pawn->m_pGameSceneNode();
		eye = node ? node->m_vecAbsOrigin() + ctx.pawn->m_vecViewOffset() : Vector{};
	}
	if (!eye.Valid())
		return;

	Vector angle = input->current_move_data.viewangles;
	if (!angle.Valid() || (std::fabs(angle.x) < 0.001f && std::fabs(angle.y) < 0.001f)) {
		if (last_valid_view.Valid())
			angle = last_valid_view;
	} else {
		last_valid_view = angle;
	}

	// Взгляд самого игрока до аима: автостоп должен опираться на то,
	// куда смотрит/выходит игрок, а не на наведении аима.
	const Vector player_view = angle;

	if (knife && knifebot_enabled) {
		if (RunKnifebot(input, eye))
			return;
	}

	bool aim_has_target = false;
	Target_t aim_target;
	if (aim_active && !knife && (!Config::Get<bool>(Vars::Legitbot::OnlyOnAttack) || attack_held)) {
		aim_has_target = FindTarget(eye, angle, penetrating, aim_target, aim_last_handle, false);
		if (aim_has_target) {
			aim_lost_ticks = 0;
			const int handle = aim_target.player ? aim_target.player->GetHandle().ToInt() : -1;
			if (handle != aim_last_handle)
				aim_last_handle = handle;

			const float smoothing = std::clamp(Config::Get<float>(Vars::Legitbot::Smoothing), 0.f, 100.f) * 0.01f;
			const float hum = humanization / 100.f;

			// 0% - мгновенно, 100% - очень медленно. Humanization не задерживает
			// наведение: она делает скорость чуть неравномерной и путь слегка
			// изогнутым (меньше похоже на робота), затухая у самой цели.
			float factor = std::max(1.f - smoothing, 0.005f);
			if (hum > 0.f) {
				aim_wobble_phase += 0.23f;
				factor *= 1.f + std::sin(aim_wobble_phase) * hum * 0.35f;
				factor = std::clamp(factor, 0.005f, 1.f);
			}

			Vector base = angle;
			if (aim_smoothed_valid && AngleDistance(base, aim_smoothed_angle) > 25.f)
				base = aim_smoothed_angle;

			Vector smoothed = base;
			smoothed.x += std::remainder(aim_target.angle.x - base.x, 360.f) * factor;
			smoothed.y += std::remainder(aim_target.angle.y - base.y, 360.f) * factor;

			if (hum > 0.f) {
				const float remain = AngleDistance(aim_target.angle, smoothed);
				const float amp = hum * 0.45f * std::clamp(remain / 6.f, 0.f, 1.f);
				smoothed.x += std::sin(aim_wobble_phase * 0.77f + 2.1f) * amp;
				smoothed.y += std::sin(aim_wobble_phase * 0.53f + 0.4f) * amp;
			}
			smoothed.x = std::remainder(smoothed.x, 360.f);
			smoothed.y = std::remainder(smoothed.y, 360.f);
			smoothed.z = 0.f;

			if (ApplyAngles(input, smoothed)) {
				aim_smoothed_angle = smoothed;
				aim_smoothed_valid = true;
				angle = smoothed;
			}
		} else if (++aim_lost_ticks > 3) {
			// Небольшая потеря цели не должна сбрасывать наведение - иначе
			// аим "отлипает" на каждый чих лагкомпа/autowall.
			aim_last_handle = -1;
			aim_smoothed_valid = false;
			aim_lost_ticks = 0;
		}
	}

	// Recoil control: сервер прибавляет aim punch к углам команды, поэтому
	// компенсируем его в том, что отправляем. Работает вместе с аимом и без него.
	const float recoil_control = std::clamp(Config::Get<float>(Vars::Legitbot::RecoilControl), 0.f, 100.f) * 0.01f;
	if (recoil_control > 0.f) {
		Vector punch{};
		if (auto aim_punch = ctx.pawn->m_pAimPunchServices())
			aim_punch->ComputeAimPunch(punch, ctx.player_time, true);

		if (punch.Valid() && (std::fabs(punch.x) > 0.001f || std::fabs(punch.y) > 0.001f)) {
			Vector cmd_angle = angle;
			cmd_angle.x = std::remainder(cmd_angle.x - punch.x * recoil_control, 360.f);
			cmd_angle.y = std::remainder(cmd_angle.y - punch.y * recoil_control, 360.f);
			cmd_angle.z = 0.f;
			ApplyAngles(input, cmd_angle);
		}
	}

	// Цель триггера ищем заранее: автовалл уже подтвердил прострел, поэтому
	// она же служит автостопу признаком "выстрел реально возможен".
	Target_t trigger_target;
	const bool trigger_has_target = trigger_active && FindTarget(eye, angle, penetrating, trigger_target);

	// Автостоп НЕ должен зависеть от включённого аима/триггера: ищем цель
	// отдельно по взгляду самого игрока. Раньше он армился только когда
	// FindTarget вызывался для аима/триггера — при выключенном аиме стоп
	// никогда не срабатывал.
	// Смотрим по ИТОГОВОМУ углу (после аима/рекоила): именно этот угол
	// уходит в команду. Раньше брался player_view до аима, поэтому когда
	// аим работал, стоп видел "цели нет" и никогда не армировался.
	Target_t stop_target;
	const bool stop_has_target = autostop_enabled && !knife &&
		FindTarget(eye, angle, penetrating, stop_target, -1, false);

	// Стоп только перед выстрелом: цель реально простреливается, оружие
	// готово и после предыдущего выстрела прошёл кдаун. Раньше требовался
	// ещё и таймер trigger_start, но он сбрасывается на каждом новом
	// наведении - из-за этого due почти всегда был 0 и стоп не включался.
	const bool weapon_ready = ctx.weapon->CanShoot() ||
		ctx.weapon->m_nNextPrimaryAttackTick() <= ctx.controller->m_nTickBase() + 1;
	const bool shot_due = stop_has_target && !auto_stop_fired && weapon_ready;

	auto_stop_armed = autostop_enabled && !knife && shot_due && ShouldAutoStop(stop_has_target);
	if (!auto_stop_armed) {
		// Иначе ApplyAutoStop может применить stop_command, оставшийся
		// с прошлого тика/после выстрела - именно это и стопило скаут.
		stop_command = -1;
	}
	if (autostop_enabled && !knife)
		ApplyAutoStop(0);

	if (FeatureDebug::Throttle(700, 500)) {
		FeatureDebug::Dump("stop: armed=%d target=%d due=%d trig=%d aim=%d fired=%d vel=%.0f\n",
			auto_stop_armed ? 1 : 0, stop_has_target ? 1 : 0, shot_due ? 1 : 0,
			trigger_has_target ? 1 : 0, aim_has_target ? 1 : 0, auto_stop_fired ? 1 : 0,
			ctx.pawn->m_vecVelocity().Length2d());
	}

	if (FeatureDebug::Throttle(700, 250)) {
		FeatureDebug::Dump("legit: player=%.1f/%.1f view=%.1f/%.1f aim=%d trig=%d seed=%d armed=%d target=%d fov=%.1f smooth=%.0f hum=%d valid=%d\n",
			player_view.x, player_view.y,
			angle.x, angle.y, aim_active ? 1 : 0, trigger_active ? 1 : 0, seed_trigger ? 1 : 0,
			auto_stop_armed ? 1 : 0, aim_has_target ? 1 : 0, aim_has_target ? aim_target.fov : -1.f,
			Config::Get<float>(Vars::Legitbot::Smoothing), humanization, aim_smoothed_valid ? 1 : 0);
	}

	if (!trigger_active) {
		trigger_start = {};
		return;
	}

	if (!trigger_has_target) {
		trigger_start = {};
		return;
	}

	const Target_t& target = trigger_target;

	// Оба режима требуют реального попадания лучом из прицела. Угол выстрела
	// НЕ подменяется: раньше обычный триггер писал в input_history угол цели
	// (Humanize(target.angle)) - это и был сайлент аим. Seed отличается только
	// тем, что разброс обнуляется, поэтому не промахивается в прыжке.
	Vector forward;
	Math::Angle2Vectors(angle, &forward);
	CAutoWall::Result_t hit;
	const float range = ctx.weapon_data->m_flRange();
	if (!AutoWall->Run(eye, eye + forward * range, target.player, hit, penetrating, range) ||
		hit.damage < MinimumDamage(target.player)) {
		trigger_start = {};
		return;
	}

	// Задержка = время, которое прицел уже находится на противнике.
	// Humanization в неё не входит.
	const auto now = std::chrono::steady_clock::now();
	if (trigger_start == std::chrono::steady_clock::time_point{})
		trigger_start = now;

	const int delay = std::max(Config::Get<int>(Vars::Legitbot::TriggerDelay), 0);
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - trigger_start).count();
	if (elapsed >= delay) {
		const uint32_t cmd_seed = static_cast<uint32_t>(ctx.cmd->pb.base().random_seed());
		uint32_t predicted_seed = 0;
		float predicted_offset = 0.f;
		bool seed_ok = true;

		if (seed_trigger || spread_prediction) {
			// Не подменяем угол: считаем, куда реально полетит пуля с
			// серверным сидом, и стреляем только если она попадает.
			seed_ok = PredictedSpreadHit(eye, angle, target.player, penetrating, predicted_seed, predicted_offset);
			if (!seed_ok) {
				if (FeatureDebug::Throttle(700, 300))
					FeatureDebug::Dump("seed: reject cmd=%d seed=%u inacc=%.3f spr=%.3f\n",
						ctx.cmd->command_number, predicted_seed, shot_inaccuracy, shot_spread);
				return;
			}
		}

		FeatureDebug::Dump("fire: cmd=%d seed=%u cmdseed=%u tick=%d sync=%d view=%.1f/%.1f target=%.1f/%.1f dmg=%.0f delay=%d wait=%lld mode=%d/%d inacc=%.3f spr=%.3f off=%.3f\n",
			ctx.cmd->command_number, predicted_seed, cmd_seed, ShotTick(), seed_ok ? 1 : 0,
			angle.x, angle.y, target.angle.x, target.angle.y, hit.damage, delay, elapsed,
			seed_trigger ? 1 : 0, spread_prediction ? 1 : 0, shot_inaccuracy, shot_spread, predicted_offset);

		if (Fire(angle, target.record)) {
			// Один выстрел на выходе задержки, дальше attack снимается.
			trigger_start = {};
			trigger_shot_active = true;
			auto_stop_fire_tick = ctx.cmd->command_number;
		}
	}
}

CLegitbot* Legitbot = new CLegitbot();
