#include <shared.h>
#include "Movement.h"
#include "../Prediction/Prediction.h"
#include "../Legitbot/Helpers/FeatureDebug.h"
#include "../../SDK/Globals.h"
#include <limits>

void CMovement::BeforePrediction(CUserCommand* cmd) {
	bunny_hop_command = -1;
	jump_bug_command = -1;
	boost_takeoff = false;

	if (!ctx.pawn->Alive())
		return;

	const auto move_type = ctx.pawn->m_nActualMoveType();
	if (move_type == MOVETYPE_LADDER || move_type == MOVETYPE_NOCLIP)
		return;

	const bool jump_bug = Config::Get<bool>(Vars::Movement::JumpBug);
	if (!jump_bug)
		jump_bug_pending_tick = -1;

	if (jump_bug && jump_bug_pending_tick == ctx.controller->m_nTickBase())
		JumpBug(cmd, true);

	if (EnginePrediction->m_nGroundTick == ctx.controller->m_nTickBase() && EnginePrediction->m_flGroundFrac == 0.f) {
		if (EnginePrediction->m_flNextJumpFrac == 0.f)
			BunnyHop(cmd, true);

		if (jump_bug)
			BoostTakeoff(cmd);
	}

	AirStrafer(cmd);
	this->FastStop(cmd);

	if (jump_bug && !Config::Get<bool>(Vars::Movement::Strafer))
		PrepareJumpSubticks(cmd);
}

void CMovement::FastStop(CUserCommand* cmd) {
	constexpr uint64_t move_mask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_JUMP;
	if (!Config::Get<bool>(Vars::Movement::FastStop) || !ctx.weapon || !ctx.weapon_data || this->PeekAssistState.returning ||
		ctx.pawn->m_nActualMoveType() != MOVETYPE_WALK || !(ctx.pawn->m_fFlags() & FL_ONGROUND) ||
		(ctx.pawn->m_fFlags() & FL_FROZEN) || (cmd->button_state.held & move_mask))
		return;

	auto base = cmd->pb.mutable_base();
	if (base->forwardmove() != 0.f || base->leftmove() != 0.f)
		return;

	for (const auto& step : base->subtick_moves()) {
		if ((step.button() & move_mask) || step.analog_forward_delta() != 0.f || step.analog_left_delta() != 0.f)
			return;
	}

	Vector velocity = ctx.pawn->m_vecVelocity();
	velocity.z = 0.f;
	const float speed = velocity.Length2d();
	// Deadzone: не дёргать модельку микродвижениями/остаточной скоростью на месте.
	if (speed <= 12.f)
		return;

	cvar(sv_friction);
	cvar(sv_stopspeed);
	cvar(sv_accelerate);
	cvar(sv_maxspeed);

	auto movement = ctx.pawn->m_pMovementServices();
	const int mode = std::clamp(ctx.weapon->m_weaponMode(), 0, 1);
	float max_speed = std::min(ctx.weapon_data->m_flMaxSpeed()[mode], sv_maxspeed->GetFloat());

	if (movement->m_flMaxspeed() > 0.f)
		max_speed = std::min(max_speed, movement->m_flMaxspeed());

	if ((ctx.pawn->m_fFlags() & FL_DUCKING) || (cmd->button_state.held & IN_DUCK))
		max_speed *= 0.34f;

	const float stamina = std::clamp(1.f - movement->m_flStamina() * 0.01f, 0.f, 1.f);
	const float friction = movement->m_flSurfaceFriction();
	const float remaining = std::max(0.f, speed - std::max(speed, sv_stopspeed->GetFloat()) * sv_friction->GetFloat() * friction * INTERVAL_PER_TICK);
	const float acceleration = sv_accelerate->GetFloat() * max_speed * stamina * stamina * friction * INTERVAL_PER_TICK;
	const float strength = acceleration > 0.f ? std::clamp(remaining / acceleration, 0.f, 1.f) : 0.f;
	const float yaw = base->viewangles().y() * Math::degs;
	const Vector direction = velocity * (-strength / speed);

	const float fm = direction.x * std::cos(yaw) + direction.y * std::sin(yaw);
	const float lm = -direction.x * std::sin(yaw) + direction.y * std::cos(yaw);
	base->set_forwardmove(fm);
	base->set_leftmove(lm);
	this->move_cmd_basis = true;
	this->move_cmd_basis_command = cmd->command_number;

	constexpr uint64_t move_mask_btn = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
	cmd->button_state.held &= ~move_mask_btn;
	cmd->button_state.toggle &= ~move_mask_btn;
	cmd->button_state.scroll &= ~move_mask_btn;
	if (fm > 0.01f)
		cmd->button_state.held |= IN_FORWARD;
	else if (fm < -0.01f)
		cmd->button_state.held |= IN_BACK;
	if (lm > 0.01f)
		cmd->button_state.held |= IN_MOVELEFT;
	else if (lm < -0.01f)
		cmd->button_state.held |= IN_MOVERIGHT;
}

void CMovement::BoostTakeoff(CUserCommand* cmd) {
	auto movement = ctx.pawn->m_pMovementServices();

	const bool jumping = bunny_hop_command == cmd->command_number || ((cmd->button_state.held & IN_JUMP) && !(movement->m_nButtons().held & IN_JUMP) && EnginePrediction->m_flNextJumpFrac == 0.f);

	if (!jumping || (cmd->button_state.held & IN_DUCK) || (EnginePrediction->m_fFlags & FL_DUCKING))
		return;

	const bool stable_ground = std::abs(EnginePrediction->m_vecVelocity.z) < 1.f && EnginePrediction->m_vecVelocity.Length2d() <= 260.f;
	if (!stable_ground)
		return;

	const bool duck_ready = movement->m_flDuckSpeed() >= 5.5f && movement->m_flDuckAmount() == 0.f && !movement->m_bDucking() && !movement->m_bDucked() && !movement->m_bDuckOverride() && !movement->m_duckUntilOnGround();

	if (!duck_ready || !Protobuf->ReserveSubtickMoveSteps(cmd, 2, 0.f))
		return;

	auto press = Protobuf->AddSubtickMoveStep(cmd, 0.f);
	auto release = press ? Protobuf->AddSubtickMoveStep(cmd, 2.f / 64.f) : nullptr;
	if (!press || !release)
		return;

	press->set_button(IN_DUCK);
	press->set_pressed(true);
	release->set_button(IN_DUCK);
	release->set_pressed(false);

	boost_takeoff = true;
}

void CMovement::PrepareJumpSubticks(CUserCommand* cmd) {
	if (!(cmd->button_state.held & IN_JUMP))
		return;

	if (EnginePrediction->m_vecVelocity.z >= 0.f && !boost_takeoff)
		return;

	const int ticks = std::clamp(32 - cmd->pb.base().subtick_moves_size() - 4, 0, 32) & ~1;
	const float delta = std::numeric_limits<float>::min();

	for (int i = 0; i < ticks; ++i) {
		auto subtick = Protobuf->AddSubtickMoveStep(cmd, GetSubtickFraction(i, ticks));
		if (!subtick)
			break;

		subtick->set_analog_forward_delta((i & 1) ? -delta : delta);
	}
}

float CMovement::GetSubtickWhen(CUserCommand* cmd, float when, bool quantize) {
	const float step_when = quantize ? std::nearbyint(when * 64.f) / 64.f : when;

	for (int i = 0; i < cmd->pb.base().subtick_moves_size(); ++i) {
		const float input_when = cmd->pb.base().subtick_moves(i).when();
		if (std::nearbyint(input_when * 64.f) / 64.f == step_when)
			when = std::max(when, input_when);
	}

	return when;
}

bool CMovement::PressJump(CUserCommand* cmd, float when) {
	auto release = Protobuf->AddSubtickMoveStep(cmd, when);
	if (!release)
		return false;

	release->set_button(IN_JUMP);
	release->set_pressed(false);

	auto press = Protobuf->AddSubtickMoveStep(cmd, when);
	if (!press)
		return false;

	press->set_button(IN_JUMP);
	press->set_pressed(true);

	bunny_hop_command = cmd->command_number;
	return true;
}

void CMovement::BunnyHop(CUserCommand* cmd, bool before_prediction) {
	if (!Config::Get<bool>(Vars::Movement::Bunnyhop))
		return;

	if (bunny_hop_command == cmd->command_number)
		return;

	if (!(cmd->button_state.held & IN_JUMP))
		return;

	const int tick = ctx.controller->m_nTickBase() - (before_prediction ? 0 : 1);
	if (jump_bug_pending_tick == tick + 1)
		return;

	int land_tick = EnginePrediction->m_nLastLandedTick;
	float when = EnginePrediction->m_flLastLandedFrac;

	if (EnginePrediction->m_nGroundTick == tick && EnginePrediction->m_flGroundFrac == 0.f) {
		land_tick = tick;
		when = 0.f;
	} else {
		if (EnginePrediction->m_bPredictUnducked && (cmd->button_state.held & IN_DUCK) && !(ctx.pawn->m_fFlags() & FL_DUCKING))
			return;

		if (land_tick < tick && EnginePrediction->m_nGroundTick == tick) {
			land_tick = tick;
			when = EnginePrediction->m_flGroundFrac;
		}
	}

	if (land_tick != tick)
		return;

	if (when < 0.f || when >= 1.f)
		return;

	if (EnginePrediction->m_flNextJumpFrac >= 1.f)
		return;

	if (!Protobuf->ReserveSubtickMoveSteps(cmd, 2, when))
		return;

	when = GetSubtickWhen(cmd, std::max(when, EnginePrediction->m_flNextJumpFrac));

	PressJump(cmd, when);
}

void CMovement::JumpBug(CUserCommand* cmd, bool before_prediction) {
	if (!Config::Get<bool>(Vars::Movement::JumpBug))
		return;

	auto movement = ctx.pawn->m_pMovementServices();

	if (jump_bug_command == cmd->command_number)
		return;

	const int tick = ctx.controller->m_nTickBase() - (before_prediction ? 0 : 1);
	const bool pending = jump_bug_pending_tick == tick;
	if (jump_bug_pending_tick != tick)
		jump_bug_pending_tick = -1;

	if (!(cmd->button_state.held & IN_JUMP))
		return;

	const bool ducked = (EnginePrediction->m_fFlags & FL_DUCKING) != 0;
	float fraction = EnginePrediction->m_flLandingFrac;
	float when = EnginePrediction->m_flLandingEndFrac;

	if (before_prediction) {
		if (!pending || !ducked || EnginePrediction->m_nGroundTick != tick || EnginePrediction->m_flGroundFrac != 0.f)
			return;

		when = EnginePrediction->m_flNextJumpFrac;
		fraction = when;
		if (when >= 1.f)
			return;
	} else {
		if (EnginePrediction->m_vecVelocity.z >= 0.f)
			return;

		if (EnginePrediction->m_nGroundTick == tick && EnginePrediction->m_flGroundFrac == 0.f)
			return;

		if (EnginePrediction->m_nLandingTick != tick)
			return;

		if (EnginePrediction->m_flLandingHeight <= 0.f)
			when = (std::ceil(fraction * 64.f) - 1.f) / 64.f;

		if (pending && EnginePrediction->m_flLandingStartFrac == 0.f)
			when = 0.f;
	}

	if (movement->m_bDuckOverride() || EnginePrediction->m_bLandingDucked)
		return;

	if (!ducked && EnginePrediction->m_flDuckSpeed < 3.5f)
		return;

	if (when < 0.f || when > 1.f || when < EnginePrediction->m_flLandingStartFrac)
		return;

	if (when < EnginePrediction->m_flNextJumpFrac)
		return;

	if (!ducked && when == 0.f)
		return;

	const int buttons = (when == 1.f ? 0 : 3) + ((cmd->button_state.held & IN_DUCK) ? 0 : 1);
	if (!Protobuf->ReserveSubtickMoveSteps(cmd, buttons, fraction))
		return;

	if (!(cmd->button_state.held & IN_DUCK)) {
		auto subtick_move = Protobuf->AddSubtickMoveStep(cmd, 0.f);
		if (!subtick_move)
			return;

		subtick_move->set_button(IN_DUCK);
		subtick_move->set_pressed(true);
	}

	if (when == 1.f) {
		jump_bug_pending_tick = tick + 1;
		return;
	}

	when = GetSubtickWhen(cmd, when, false);

	auto release = Protobuf->AddSubtickMoveStep(cmd, when);
	if (!release)
		return;

	release->set_button(IN_DUCK);
	release->set_pressed(false);

	if (!PressJump(cmd, when))
		return;

	jump_bug_command = cmd->command_number;
	jump_bug_pending_tick = -1;
}

void CMovement::AirStrafer(CUserCommand* cmd) {
	if (!Config::Get<bool>(Vars::Movement::Strafer))
		return;

	if (!Cvar || !ctx.weapon_data || !ctx.pawn->Alive())
		return;

	const auto move_type = ctx.pawn->m_nActualMoveType();
	if (move_type == MOVETYPE_LADDER || move_type == MOVETYPE_NOCLIP)
		return;

	auto movement = ctx.pawn->m_pMovementServices();
	if (EnginePrediction->m_fFlags & FL_ONGROUND) {
		PeekAssistState.stop_latch = false;
		const bool can_autohop = Config::Get<bool>(Vars::Movement::Bunnyhop) && (cmd->button_state.held & IN_JUMP);
		if (bunny_hop_command != cmd->command_number && !boost_takeoff && !can_autohop)
			return;
	}

	const bool peek_returning = PeekAssistState.active && PeekAssistState.returning;

	if (!peek_returning) {
		PeekAssistState.stop_latch = false;
	} else {
		if (PeekAssistState.stop_latch) {
			this->m_bShouldStuck = true;
			return;
		}

		auto node = ctx.pawn->m_pGameSceneNode();
		if (!node)
			return;

		Vector current_pos = node->m_vecAbsOrigin();
		Vector to_target = PeekAssistState.origin - current_pos;
		to_target.z = 0.f;

		float dist_to_target = to_target.Length2d();

		bool should_latch = dist_to_target < 2.0f;

		if (!should_latch) {
			Vector vel2d = EnginePrediction->m_vecVelocity;
			vel2d.z = 0.f;

			Vector dir_to_target = to_target;
			dir_to_target.Normalize();

			float closing_speed = vel2d.Dot(dir_to_target);
			float predicted_travel = closing_speed * INTERVAL_PER_TICK;

			should_latch = (closing_speed > 0.f && predicted_travel >= dist_to_target) || (closing_speed < 0.f && dist_to_target < 15.0f);
		}

		if (should_latch) {
			PeekAssistState.stop_latch = true;
			this->m_bShouldStuck = true;
			return;
		}
	}

	cvar(sv_airaccelerate);
	cvar(sv_air_max_wishspeed);
	cvar(sv_maxspeed);
	cvar(sv_gravity);
	cvar(sv_staminarecoveryrate);
	cvar(sv_enablebunnyhopping);

	if (!sv_airaccelerate || !sv_air_max_wishspeed || !sv_maxspeed || !sv_gravity || !sv_staminarecoveryrate || !sv_enablebunnyhopping)
		return;

	auto base = cmd->pb.mutable_base();
	// Намерение игрока - это кнопки до того, как Rotation переписала их
	// в базис анти-эйма. Из переписанных кнопок страйфер не может понять,
	// куда игрок реально хочет двигаться.
	const auto buttons = (this->real_held_command == cmd->command_number) ? this->real_held : cmd->button_state.held;

	const int ticks = std::clamp(32 - base->subtick_moves_size(), 0, 32);
	if (!ticks)
		return;

	static uint64_t last_buttons = 0;
	static uint64_t last_pressed = 0;
	static bool side_switch = false;

	auto check_button = [&](uint64_t button, uint64_t opposite) {
		if (!(buttons & button))
			last_pressed &= ~button;
		else if (!(last_buttons & button) || !(last_pressed & opposite))
			last_pressed = (last_pressed & ~opposite) | button;
	};

	check_button(IN_MOVELEFT, IN_MOVERIGHT);
	check_button(IN_MOVERIGHT, IN_MOVELEFT);
	check_button(IN_FORWARD, IN_BACK);
	check_button(IN_BACK, IN_FORWARD);
	last_buttons = buttons;

	// База для субтик-дельт - последние импульсы движения (как в velocity),
	// а не кнопки предыдущей команды: их Rotation уже переписала под AA.
	const Vector last_impulses = movement->m_vecLastMovementImpulses();
	Vector2D last_move(last_impulses.x, last_impulses.y);

	float yaw = 0.f;
	const int strafer_mode = std::clamp(Config::Get<int>(Vars::Movement::StraferMode), 0, 2);
	const bool use_movement_keys = (strafer_mode == 1 || strafer_mode == 2);
	const bool use_view_angle = (strafer_mode == 0 || strafer_mode == 2);
	const bool has_move_keys = (buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)) != 0;
	// 100% smooth давал ideal_angle=0 и полностью ломал strafe (move=(1,0)).
	const float strafe_smooth = std::clamp(Config::Get<float>(Vars::Movement::StraferSmooth), 0.f, 85.f) / 100.f;

	if (peek_returning) {
		Vector current_pos = ctx.pawn->m_pGameSceneNode()->m_vecAbsOrigin();
		Vector to_target = PeekAssistState.origin - current_pos;
		to_target.z = 0.f;
		to_target.Normalize();

		yaw = std::remainderf(Math::Rad2Deg(std::atan2(to_target.y, to_target.x)), 360.f);
	} else {
		float offset = 0.f;
		if (last_pressed & IN_MOVELEFT)
			offset = 90.f;
		else if (last_pressed & IN_MOVERIGHT)
			offset = -90.f;

		if (last_pressed & IN_FORWARD)
			offset *= 0.5f;
		else if (last_pressed & IN_BACK)
			offset = -offset * 0.5f + 180.f;

		if (has_move_keys) {
			// Любой режим: если пользователь нажал WASD, направление задаётся
			// этими клавишами относительно реального взгляда. Иначе View Angle
			// режим игнорировал бы ввод и всегда прыгал "вперёд".
			yaw = std::remainderf(this->angle.y + offset, 360.f);
		} else if (use_movement_keys) {
			// Movement Keys / Both без активных клавиш: по скорости, иначе по взгляду.
			const Vector vel = EnginePrediction->m_vecVelocity;
			if (vel.Length2d() > 1.0f)
				yaw = std::remainderf(Math::Rad2Deg(std::atan2(vel.y, vel.x)), 360.f);
			else
				yaw = std::remainderf(this->angle.y, 360.f);
		} else {
			// View Angle без активных клавиш: направление задаётся взглядом.
			yaw = std::remainderf(this->angle.y, 360.f);
		}
	}

	const int weapon_mode = ctx.weapon->m_weaponMode();
	float move_speed = std::min(sv_maxspeed->GetFloat(), 260.f);

	if (movement->m_flMaxspeed() > 0.f)
		move_speed = std::min(move_speed, movement->m_flMaxspeed());

	move_speed = std::min(move_speed, ctx.weapon_data->m_flMaxSpeed()[weapon_mode >= 0 && weapon_mode < 2 ? weapon_mode : 0]);
	if ((ctx.pawn->m_fFlags() & FL_DUCKING) || (buttons & IN_DUCK))
		move_speed *= 0.34f;

	const float air_max_wishspeed = sv_air_max_wishspeed->GetFloat();
	const float air_accel = sv_airaccelerate->GetFloat();
	const float gravity = sv_gravity->GetFloat() * ctx.pawn->m_flActualGravityScale();

	Vector velocity = EnginePrediction->m_vecVelocity;
	if ((EnginePrediction->m_fFlags & FL_ONGROUND) && !sv_enablebunnyhopping->GetBool()) {
		const float limit = movement->m_flMaxspeed() * 1.1f;
		const float speed = velocity.Length();
		if (limit > 0.f && speed > limit)
			velocity *= limit / speed;
	}

	// Движок в воздухе использует реальное трение поверхности (обычно 1.0).
	// Раньше в конце цикла трение искусственно падало до 0.25, пока скорость
	// подъёма лежала в (0, 140] - то есть ровно в верхней половине прыжка
	// (около апекса). Из-за этого страйфер вчетверо недооценивал воздушное
	// ускорение именно там, где должен разгоняться, и "не набирал" скорость.
	// Берём то же трение, что и движок, защищаясь лишь от нуля.
	const float surface_friction = std::max(movement->m_flSurfaceFriction(), 0.01f);
	const float entry_speed = velocity.Length2d();
	const float entry_vz = velocity.z;
	const float base_yaw = std::remainderf(base->viewangles().y(), 360.f);

	int i = 0;
	for (; i < ticks; ++i) {
		const float when = GetSubtickFraction(i, ticks);
		const float next_when = GetSubtickFraction(i + 1, ticks);
		const float frame_time = (std::nearbyint(next_when * 64.f) - std::nearbyint(when * 64.f)) / 4096.f;
		// Формула повторяет движковый AirAccelerate: wishspeed - это полная
		// m_flMaxspeed, а stamina там НЕ участвует. Раньше wishspeed домножался
		// на stamina^2, из-за чего страйфер недооценивал ускорение и недобирал
		// скорость (чем больше прыжков - тем слабее страйф).
		const float wish_speed = move_speed;
		const float max_wishspeed = std::min(wish_speed, air_max_wishspeed);
		const float accel_term = air_accel * wish_speed * surface_friction * frame_time;
		const float speed = velocity.Length2d();
		const float velocity_angle = std::remainder(Math::Rad2Deg(std::atan2(velocity.y, velocity.x)), 360.f);
		const float velocity_delta = std::remainder(yaw - velocity_angle, 360.f);
		float wish_yaw = yaw;

		if (speed > 0.1f) {
			// Физический ideal angle air accel. На boost/no-boost серверах при
			// высокой скорости прямое движение в yaw даёт add_speed<=0, поэтому
			// держим угол относительно velocity, а не yaw цели.
			const float cos_angle = (speed > 1.f)
				? std::clamp((max_wishspeed - accel_term) / speed, -1.f, 1.f)
				: 1.f;
			const float base_ideal_angle = Math::Rad2Deg(std::acos(cos_angle));
			const float ideal_angle = std::clamp(base_ideal_angle, 0.f, 89.f) * (1.f - strafe_smooth);
			const float abs_delta = std::fabsf(velocity_delta);

			if (base_ideal_angle < 1.f) {
				wish_yaw = yaw;
			} else {
				// Всегда выбираем gain-сторону относительно velocity.
				// Это стабильнее на boost-серверах, где прежняя логика
				// "цель близко -> чередуй" могла качать направление.
				wish_yaw = velocity_angle + (velocity_delta >= 0.f ? ideal_angle : -ideal_angle);
			}
		}

		const float wish_radians = Math::Deg2Rad(wish_yaw);
		Vector wish_dir(std::cos(wish_radians), std::sin(wish_radians), 0.f);
		const float wish_length = wish_dir.Length2d();

		if (wish_length >= 0.001f) {
			wish_dir /= wish_length;
			const float add_speed = max_wishspeed - velocity.Dot(wish_dir);
			if (add_speed > 0.f)
				velocity += wish_dir * std::min(accel_term, add_speed);
		}

		// Желаемое направление (wish_yaw) раскладываем по базису углов,
		// которые реально уйдут в команду. Раньше здесь использовался
		// yaw_delta на весь разворот AA (до 180°), и сервер такие дельты
		// не отрабатывал - персонаж с AA не мог страйфить в нужную сторону.
		const float rotation = Math::Deg2Rad(base_yaw - wish_yaw);
		float forward = std::clamp(std::cos(rotation), -1.f, 1.f);
		float left = std::clamp(-std::sin(rotation), -1.f, 1.f);

		if (std::fabsf(forward) < 0.001f)
			forward = 0.f;

		if (std::fabsf(left) < 0.001f)
			left = 0.f;

		auto subtick = Protobuf->AddSubtickMoveStep(cmd, when);
		if (!subtick)
			break;

		subtick->set_analog_forward_delta(forward - last_move.x);
		subtick->set_analog_left_delta(left - last_move.y);

		last_move = { forward, left };
		velocity.z -= gravity * frame_time;
		// Переключаем сторону на каждом сабтике (как в Velocity), иначе все
		// 32 сабтика тика уходят в одну сторону и набор скорости пропадает.
		side_switch = !side_switch;
	}

	if (!i)
		return;

	// Диагностика набора скорости: in - скорость на входе тика, out -
	// скорость, которую страйфер насчитал к концу тика. Если out < in, разгон
	// отрицательный - по этой строке видно, где именно теряется скорость.
	if (FeatureDebug::Throttle(52, 200)) {
		Vector simulated = velocity;
		simulated.z = 0.f;
		const float dbg_ideal = (entry_speed > 1.f && air_max_wishspeed > 0.f)
			? std::clamp(Math::Rad2Deg(std::acos(std::clamp((air_max_wishspeed - (air_accel * move_speed * surface_friction * (1.f / 128.f))) / std::max(entry_speed, 1.f), -1.f, 1.f))), 0.f, 89.f) * (1.f - strafe_smooth)
			: 0.f;
		FeatureDebug::Dump("strafe: cmd=%d ground=%d in=%.0f out=%.0f vz=%.0f fric=%.3f wish=%.0f steps=%d move=(%.2f,%.2f) ideal=%.1f smooth=%.2f",
			cmd->command_number, (EnginePrediction->m_fFlags & FL_ONGROUND) ? 1 : 0,
			entry_speed, simulated.Length2d(), entry_vz, surface_friction, move_speed, i, last_move.x, last_move.y, dbg_ideal, strafe_smooth);
	}

	base->set_forwardmove(last_move.x);
	base->set_leftmove(last_move.y);
	this->move_cmd_basis = true;
	this->move_cmd_basis_command = cmd->command_number;

	cmd->button_state.SetButton(IN_FORWARD, last_move.x > 0.f);
	cmd->button_state.SetButton(IN_BACK, last_move.x < 0.f);
	cmd->button_state.SetButton(IN_MOVELEFT, last_move.y > 0.f);
	cmd->button_state.SetButton(IN_MOVERIGHT, last_move.y < 0.f);
}

void CMovement::AirStuck(CUserCommand* cmd) {
	cvar(sv_airaccelerate);

	if (auto mt = ctx.pawn->m_nActualMoveType(); mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP || mt == MOVETYPE_OBSERVER)
		return;

	if ((EnginePrediction->m_fFlags & FL_ONGROUND) || !this->m_bShouldStuck) {
		this->m_bShouldStuck = false;
		return;
	}

	Vector velocity = EnginePrediction->m_vecAbsVelocity;
	velocity.z = 0.f;

	float speed = velocity.Length2d();
	if (FeatureDebug::Throttle(51, 250))
		FeatureDebug::Dump("airstuck: speed=%.1f stuck=%d flags=%d", speed, this->m_bShouldStuck ? 1 : 0, EnginePrediction->m_fFlags);
	if (speed < 1.0f) {
		cmd->pb.mutable_base()->set_forwardmove(0.f);
		cmd->pb.mutable_base()->set_leftmove(0.f);

		const auto move_mask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
		cmd->button_state.held &= ~move_mask;
		cmd->button_state.toggle &= ~move_mask;
		cmd->button_state.scroll &= ~move_mask;

		this->m_bShouldStuck = false;
		return;
	}

	Vector wish_dir = Vector(-velocity.x / speed, -velocity.y / speed, 0.f);

	float ideal = (std::max(ctx.weapon_data->m_flMaxSpeed()[0], 10.0f)) * (sv_airaccelerate->GetFloat() * ctx.pawn->m_pMovementServices()->m_flSurfaceFriction() * INTERVAL_PER_TICK);

	float ratio = 1.0f;
	if (speed < ideal && ideal > 0.0f)
		ratio = std::clamp(speed / ideal, 0.0f, 1.0f);

	float yaw = Math::Deg2Rad(cmd->pb.mutable_base()->viewangles().y());
	Vector forward = Vector(std::cosf(yaw), std::sinf(yaw), 0.f);
	Vector left = Vector(std::sinf(yaw), -std::cosf(yaw), 0.f);

	float fmove = wish_dir.Dot(forward) * ratio;
	float lmove = wish_dir.Dot(left) * ratio;

	cmd->pb.mutable_base()->set_forwardmove(fmove);
	cmd->pb.mutable_base()->set_leftmove(-lmove);
	this->move_cmd_basis = true;
	this->move_cmd_basis_command = cmd->command_number;

	const auto move_mask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
	cmd->button_state.held &= ~move_mask;
	cmd->button_state.toggle &= ~move_mask;
	cmd->button_state.scroll &= ~move_mask;

	if (fmove > 0.01f)
		cmd->button_state.held |= IN_FORWARD;
	else if (fmove < -0.01f)
		cmd->button_state.held |= IN_BACK;

	if (-lmove > 0.01f)
		cmd->button_state.held |= IN_MOVELEFT;
	else if (-lmove < -0.01f)
		cmd->button_state.held |= IN_MOVERIGHT;

	this->m_bShouldStuck = false;
}

void CMovement::FastLadder(CUserCommand* cmd) {
	if (!Config::Get<bool>(Vars::Movement::FastLadder))
		return;

	if (auto mt = ctx.pawn->m_nActualMoveType(); mt != MOVETYPE_LADDER)
		return;

	auto btns = cmd->button_state.held;
	const float fwd = cmd->pb.mutable_base()->forwardmove();

	bool wanna_up = (btns & IN_FORWARD) && fwd > 0.01f;
	bool wanna_down = (btns & IN_BACK) && fwd < -0.01f;

	if (!wanna_up && !wanna_down)
		return;

	auto modified = this->angle;
	modified.x = 89.0f;

	btns &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);

	if (wanna_up) {
		modified.y -= 90.0f;
		cmd->pb.mutable_base()->set_forwardmove(-1.0f);
		cmd->pb.mutable_base()->set_leftmove(1.0f);
		btns |= (IN_MOVELEFT | IN_BACK);
	} else if (wanna_down) {
		modified.y += 90.0f;
		cmd->pb.mutable_base()->set_forwardmove(1.0f);
		cmd->pb.mutable_base()->set_leftmove(1.0f);
		btns |= (IN_MOVELEFT | IN_FORWARD);
	}

	Math::NormalizeYaw(modified.y);

	cmd->button_state.held = btns;

	cmd->pb.mutable_base()->mutable_viewangles()->set_x(modified.x);
	cmd->pb.mutable_base()->mutable_viewangles()->set_y(modified.y);
}

void CMovement::ResetPeekAssist() {
	PeekAssistState.active = false;
	PeekAssistState.returning = false;
	PeekAssistState.has_moved = false;
	PeekAssistState.was_firing = false;
	PeekAssistState.stall_ticks = 0;
	PeekAssistState.best_distance = 0.f;
	PeekAssistState.origin = {};
	PeekAssistState.pending_shot_return = false;
}

void CMovement::PeekAssist(CUserCommand* cmd) {
	if (!cmd || !ctx.pawn || !ctx.pawn->Alive() || !Config::Get<bool>(Vars::Movement::PeekAssist)) {
		this->ResetPeekAssist();
		return;
	}

	const auto mt = ctx.pawn->m_nActualMoveType();
	if (mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP || mt == MOVETYPE_OBSERVER) {
		this->ResetPeekAssist();
		return;
	}

	auto node = ctx.pawn->m_pGameSceneNode();
	if (!node) {
		this->ResetPeekAssist();
		return;
	}

	const Vector current_origin = node->m_vecAbsOrigin();

	if (!PeekAssistState.active) {
		PeekAssistState.origin = current_origin;
		PeekAssistState.active = true;
		PeekAssistState.returning = false;
		PeekAssistState.has_moved = false;
		PeekAssistState.pending_shot_return = false;
		PeekAssistState.stall_ticks = 0;
		PeekAssistState.best_distance = 0.f;
	}

	if (!PeekAssistState.returning) {
		const float max_dist = Config::Get<float>(Vars::Movement::PeekAssistMaxDistance);

		if (max_dist > 0.f) {
			Vector diff = current_origin - PeekAssistState.origin;
			diff.z = 0.f;

			const float cur_dist = diff.Length2d();

			if (cur_dist > max_dist) {
				diff.Normalize();

				PeekAssistState.origin = current_origin - diff * max_dist;
				PeekAssistState.origin.z = current_origin.z;
			}
		}
	}

	const bool is_firing = ctx.weapon && ctx.weapon->IsFiring();
	const bool shot_triggered = is_firing && !PeekAssistState.was_firing;
	PeekAssistState.was_firing = is_firing;

	constexpr uint64_t move_mask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;

	const bool moving_now = (cmd->button_state.held & move_mask) != 0;

	if (moving_now)
		PeekAssistState.has_moved = true;

	auto types = Config::Get<std::vector<int>>(Vars::Movement::PeekAssistTypes);

	const bool on_key_release = types.at(peek_assist_types_on_key_release) != 0;

	const bool on_shot = types.at(peek_assist_types_on_shot) != 0;

	if (!PeekAssistState.returning) {
		bool start_return = false;

		if (PeekAssistState.pending_shot_return) {
			PeekAssistState.pending_shot_return = false;
			start_return = true;
		}

		if (on_key_release && PeekAssistState.has_moved && !moving_now)
			start_return = true;

		if (on_shot && shot_triggered)
			PeekAssistState.pending_shot_return = true;

		if (start_return) {
			PeekAssistState.returning = true;
			PeekAssistState.best_distance = (PeekAssistState.origin - current_origin).Length2d();
			PeekAssistState.stall_ticks = 0;
		}
	}

	if (!PeekAssistState.returning)
		return;

	Vector delta = PeekAssistState.origin - current_origin;
	delta.z = 0.f;

	const float dist = delta.Length2d();

	if (dist < PeekAssistState.best_distance - 0.5f) {
		PeekAssistState.best_distance = dist;
		PeekAssistState.stall_ticks = 0;
	} else {
		PeekAssistState.stall_ticks++;
	}

	const bool on_ground = (ctx.pawn->m_fFlags() & FL_ONGROUND) != 0;

	if ((on_ground && dist < 10.f) || PeekAssistState.stall_ticks > 64) {
		PeekAssistState.returning = false;
		PeekAssistState.has_moved = false;
		PeekAssistState.pending_shot_return = false;
		PeekAssistState.stall_ticks = 0;

		cmd->pb.mutable_base()->set_forwardmove(0.f);
		cmd->pb.mutable_base()->set_leftmove(0.f);

		cmd->button_state.held &= ~move_mask;
		cmd->button_state.toggle &= ~move_mask;
		cmd->button_state.scroll &= ~move_mask;
		return;
	}

	if (!on_ground)
		return;

	delta.Normalize();

	const auto& base = cmd->pb.base();
	Vector move_angle(base.viewangles().x(), base.viewangles().y(), 0.f);
	if (base.subtick_moves_size() && base.subtick_moves(base.subtick_moves_size() - 1).when() == 1.f) {
		for (const auto& step : base.subtick_moves()) {
			move_angle.x += step.pitch_delta();
			move_angle.y += step.yaw_delta();
		}
	}

	Vector forward, right;
	Math::Angle2Vectors(move_angle, &forward, &right);
	forward.z = 0.f;
	right.z = 0.f;
	forward.Normalize();
	right.Normalize();

	float fmove = delta.Dot(forward);
	float lmove = delta.Dot(right);

	const float slow_factor = std::clamp(dist / 32.f, 0.3f, 1.f);

	fmove *= slow_factor;
	lmove *= slow_factor;

	cmd->pb.mutable_base()->set_forwardmove(fmove);
	cmd->pb.mutable_base()->set_leftmove(-lmove);

	cmd->button_state.held &= ~move_mask;
	cmd->button_state.toggle &= ~move_mask;
	cmd->button_state.scroll &= ~move_mask;

	if (fmove > 0.01f)
		cmd->button_state.held |= IN_FORWARD;
	else if (fmove < -0.01f)
		cmd->button_state.held |= IN_BACK;

	if (-lmove > 0.01f)
		cmd->button_state.held |= IN_MOVELEFT;
	else if (-lmove < -0.01f)
		cmd->button_state.held |= IN_MOVERIGHT;
}

void CMovement::OnPresent() {
	const bool should_show = Config::Get<bool>(Vars::Movement::PeekAssist) && ctx.pawn && ctx.pawn->Alive() && this->PeekAssistState.active && !this->PeekAssistState.origin.Zero();

	this->PeekAssistState.animation = std::clamp(this->PeekAssistState.animation + (should_show ? 1.f : -1.f) * 6.f * ImGui::GetIO().DeltaTime, 0.f, 1.f);
	if (this->PeekAssistState.animation <= 0.f || !ctx.pawn || !ctx.pawn->Alive())
		return;

	static Vector origin{};
	if (should_show)
		origin = this->PeekAssistState.origin;

	const Vector2D center = Draw->WorldToScreen(origin);
	if (center.x == -1.f && center.y == -1.f)
		return;

	const float animation = this->PeekAssistState.animation * this->PeekAssistState.animation * (3.f - 2.f * this->PeekAssistState.animation);
	const float radius = 20.f * animation;
	constexpr int segments = 48;
	ImVec2 points[segments];

	for (int i = 0; i < segments; ++i) {
		const float angle = Math::_pi2 * static_cast<float>(i) / static_cast<float>(segments);
		const Vector point{ origin.x + std::cos(angle) * radius, origin.y + std::sin(angle) * radius, origin.z };
		const Vector2D screen = Draw->WorldToScreen(point);
		if (screen.x == -1.f && screen.y == -1.f)
			return;

		points[i] = { screen.x, screen.y };
	}

	const auto color = Config::Get<Color_t>(Vars::Movement::PeekAssistColor);
	const ImU32 inner = IM_COL32(color.R(), color.G(), color.B(), static_cast<uint8_t>(color.A() * animation));
	const ImU32 outer = IM_COL32(color.R(), color.G(), color.B(), 0);
	auto draw_list = ImGui::GetBackgroundDrawList();
	for (int i = 0; i < segments; ++i)
		draw_list->AddTriangleFilledMulticolor({ center.x, center.y }, points[i], points[(i + 1) % segments], inner, outer, outer);
}

void CMovement::CaptureRawInput(CCSGOInput* input) {
	// Читаем WASD из самого ввода, а не из команды: к моменту Rotation()
	// button_state уже может быть переписан анти-эймом/страйфером, из-за чего
	// режим "Movement Keys" переставал видеть нажатые клавиши.
	uint64_t held = 0;
	auto pressed = [](CButtonData* button) { return button && button->pressed; };
	if (pressed(input ? input->button_forward : nullptr))
		held |= IN_FORWARD;
	if (pressed(input ? input->button_back : nullptr))
		held |= IN_BACK;
	if (pressed(input ? input->button_moveleft : nullptr))
		held |= IN_MOVELEFT;
	if (pressed(input ? input->button_moveright : nullptr))
		held |= IN_MOVERIGHT;

	this->real_held = held;
	this->real_held_command = ctx.cmd ? ctx.cmd->command_number : -1;
}

void CMovement::Rotation() {
	const bool cmd_basis = this->move_cmd_basis && this->move_cmd_basis_command == ctx.cmd->command_number;
	this->move_cmd_basis = false;

	if (auto mt = ctx.pawn->m_nActualMoveType(); mt == MOVETYPE_LADDER || mt == MOVETYPE_NOCLIP || mt == MOVETYPE_OBSERVER)
		return;

	if (cmd_basis)
		return;

	const float forward_move = ctx.base_cmd->forwardmove();
	const float side_move = ctx.base_cmd->leftmove();

	if (forward_move == 0.0f && side_move == 0.0f)
		return;

	// Углы, которые уйдут в команду (после анти-эйма/десинка).
	Vector wish_angle = Vector(ctx.base_cmd->viewangles().x(), ctx.base_cmd->viewangles().y(), ctx.base_cmd->viewangles().z());
	wish_angle.x = isfinite(wish_angle.x) ? std::remainder(wish_angle.x, 360.f) : 0.f;
	wish_angle.y = isfinite(wish_angle.y) ? std::remainder(wish_angle.y, 360.f) : 0.f;
	wish_angle.z = 0.0f;

	// Реальные углы игрока (куда он смотрит и куда хочет идти).
	Vector view_angle = this->angle;
	view_angle.x = isfinite(view_angle.x) ? std::remainder(view_angle.x, 360.f) : 0.f;
	view_angle.y = isfinite(view_angle.y) ? std::remainder(view_angle.y, 360.f) : 0.f;
	view_angle.z = 0.0f;

	// Горизонтальный базис зависит только от yaw. Если анти-эйм его не меняет -
	// ничего пересчитывать не нужно (иначе накапливались ошибки округления).
	if (std::fabs(std::remainder(wish_angle.y - view_angle.y, 360.f)) < 0.001f)
		return;

	Vector new_forward, new_right, new_up;
	Math::Angle2Vectors(wish_angle, &new_forward, &new_right, &new_up);

	Vector old_forward, old_right, old_up;
	Math::Angle2Vectors(view_angle, &old_forward, &old_right, &old_up);

	new_forward.z = 0.0f;
	new_right.z = 0.0f;
	old_forward.z = 0.0f;
	old_right.z = 0.0f;

	new_forward.Normalize();
	new_right.Normalize();
	old_forward.Normalize();
	old_right.Normalize();

	// Желаемое направление в мире считается по реальным углам, а затем
	// раскладывается по базису углов из команды.
	const Vector intent = old_forward * forward_move - old_right * side_move;
	const float intent_length = intent.Length();
	if (intent_length <= 0.0001f)
		return;

	float fixed_forward = new_forward.Dot(intent);
	float fixed_side = -new_right.Dot(intent);

	// Сохраняем направление при диагональном вводе: независимый clamp
	// каждой оси менял угол движения (персонаж уезжал в сторону от
	// намерения). Игра сама ограничивает модуль до 1, поэтому просто
	// масштабируем обе компоненты одинаково.
	const float magnitude = std::sqrt(fixed_forward * fixed_forward + fixed_side * fixed_side);
	if (magnitude > 1.0f) {
		fixed_forward /= magnitude;
		fixed_side /= magnitude;
	}

	ctx.base_cmd->set_forwardmove(fixed_forward);
	ctx.base_cmd->set_leftmove(fixed_side);

	// Кнопки должны соответствовать пересчитанным move-значениям всегда,
	// не только на земле: иначе в воздухе сервер видит старые W/A/S/D
	// и персонаж тянет в сторону анти-эйма.
	constexpr uint64_t move_mask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
	ctx.cmd->button_state.held &= ~move_mask;
	ctx.cmd->button_state.toggle &= ~move_mask;
	ctx.cmd->button_state.scroll &= ~move_mask;

	if (ctx.base_cmd->forwardmove() > 0.0f)
		ctx.cmd->button_state.held |= IN_FORWARD;
	else if (ctx.base_cmd->forwardmove() < 0.0f)
		ctx.cmd->button_state.held |= IN_BACK;

	if (ctx.base_cmd->leftmove() > 0.0f)
		ctx.cmd->button_state.held |= IN_MOVELEFT;
	else if (ctx.base_cmd->leftmove() < 0.0f)
		ctx.cmd->button_state.held |= IN_MOVERIGHT;
}

CMovement* Movement = new CMovement();
