#include <shared.h>

void CGui::RenderLegitbotContent() {
	const std::vector<std::string> hitboxes = { xs("Head"), xs("Chest"), xs("Stomach"), xs("Arms"), xs("Legs"), xs("Feet") };
	const std::vector<std::string> stop_modes = { xs("Early"), xs("Between Shots"), xs("In Air"), xs("On Land") };
	Color_t* fov_color[1] = { &Config::Get<Color_t>(Vars::Legitbot::FovColor) };

	this->BeginGroup();
	{
		this->BeginChild(xs("Aimbot"));
		{
			this->Checkbox(xs("Enabled"), &Config::Get<bool>(Vars::Legitbot::Enable));
			this->SliderFloat(xs("Field of View"), &Config::Get<float>(Vars::Legitbot::FieldOfView), 0.f, 180.f, xs("%.0f°"));
			this->SliderFloat(xs("Smoothing"), &Config::Get<float>(Vars::Legitbot::Smoothing), 0.f, 100.f, xs("%.0f%%"));
			this->SliderFloat(xs("Recoil Control"), &Config::Get<float>(Vars::Legitbot::RecoilControl), 0.f, 100.f, xs("%.0f%%"));

			this->BeginChild(xs("Humanization"));
			{
				this->SliderInt(xs("Humanization"), &Config::Get<int>(Vars::Legitbot::Humanization), 0, 100, xs("%d%%"));
			}
			this->EndChild();

			this->Dropdown(xs("Target"), &Config::Get<int>(Vars::Legitbot::TargetSelection), { xs("Crosshair"), xs("Field of View"), xs("Lowest Health") });
			this->Checkbox(xs("Visibility Check"), &Config::Get<bool>(Vars::Legitbot::VisibilityCheck));
			this->Checkbox(xs("Auto Wall"), &Config::Get<bool>(Vars::Legitbot::AutoWall));
			this->Checkbox(xs("Scoped Only"), &Config::Get<bool>(Vars::Legitbot::ScopeOnly));
			this->Checkbox(xs("Only On Attack"), &Config::Get<bool>(Vars::Legitbot::OnlyOnAttack));
			this->Checkbox(xs("Show FOV"), &Config::Get<bool>(Vars::Legitbot::ShowFov));
			this->ColorEdit(xs("FOV Color"), fov_color, 1);

			this->MultiDropdown(xs("Hitboxes"), &Config::Get<std::vector<bool>>(Vars::Legitbot::Hitboxes), hitboxes);
			this->MultiDropdown(xs("Multipoint"), &Config::Get<std::vector<bool>>(Vars::Legitbot::Multipoints), hitboxes, [this]() {
				this->SliderFloat(xs("Head Scale"), &Config::Get<float>(Vars::Legitbot::PointScale), 0.f, 100.f, xs("%.0f%%"));
				this->SliderFloat(xs("Body Scale"), &Config::Get<float>(Vars::Legitbot::BodyPointScale), 0.f, 100.f, xs("%.0f%%"));
			});

			this->SliderFloat(xs("Hit Chance"), &Config::Get<float>(Vars::Legitbot::Hitchance), 0.f, 100.f, xs("%.0f%%"));

			const char* damage_fmt = (Config::Get<int>(Vars::Legitbot::MinimumDamage) >= 100)
				? xs("Lethal") : xs("%d");
			this->SliderInt(xs("Minimum Damage"), &Config::Get<int>(Vars::Legitbot::MinimumDamage), 0, 200, damage_fmt);

			this->CheckboxEx(xs("Extrapolation"), &Config::Get<bool>(Vars::Legitbot::Extrapolation), false, nullptr, 0, [this]() {
				this->SliderInt(xs("Extrapolation Ticks"), &Config::Get<int>(Vars::Legitbot::ExtrapolationTicks), 1, 8, xs("%d"));
			});
		}
		this->EndChild();
	}
	this->EndGroup();

	this->Sameline();

	this->BeginGroup();
	{
		this->BeginChild(xs("Triggerbot"));
		{
			this->Checkbox(xs("Enabled"), &Config::Get<bool>(Vars::Legitbot::TriggerEnable));
			this->SliderInt(xs("Delay"), &Config::Get<int>(Vars::Legitbot::TriggerDelay), 0, 500, xs("%d ms"));
			this->Checkbox(xs("Visibility Check"), &Config::Get<bool>(Vars::Legitbot::TriggerVisibility));
			this->Checkbox(xs("Auto Wall"), &Config::Get<bool>(Vars::Legitbot::AutoWall));
			this->Checkbox(xs("Seed Trigger"), &Config::Get<bool>(Vars::Legitbot::SeedTrigger));
			this->Checkbox(xs("Spread Prediction"), &Config::Get<bool>(Vars::Legitbot::SpreadPrediction));

			this->Checkbox(xs("Knifebot"), &Config::Get<bool>(Vars::Legitbot::Knifebot));

			this->CheckboxEx(xs("Auto Stop"), &Config::Get<bool>(Vars::Legitbot::AutoStop), false, nullptr, 0, [this, stop_modes]() {
				this->MultiDropdown(xs("Modes"), &Config::Get<std::vector<bool>>(Vars::Legitbot::AutoStopModes), stop_modes);
			});
		}
		this->EndChild();
	}
	this->EndGroup();
}
