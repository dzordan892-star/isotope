#pragma once

#undef max
#undef min

#include <cstdint>
#include <array>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <imgui/imgui.h>

struct Color_t {
    constexpr Color_t() : r(0), g(0), b(0), a(255) {}

    constexpr Color_t(int r, int g, int b, int a = 255) :
        r(static_cast<uint8_t>(std::clamp(r, 0, 255))),
        g(static_cast<uint8_t>(std::clamp(g, 0, 255))),
        b(static_cast<uint8_t>(std::clamp(b, 0, 255))),
        a(static_cast<uint8_t>(std::clamp(a, 0, 255))) {
    }

    constexpr Color_t(float r, float g, float b, float a = 1.0f) :
        r(static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f))),
        g(static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f))),
        b(static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f))),
        a(static_cast<uint8_t>(std::clamp(a * 255.0f, 0.0f, 255.0f))) {
    }

    [[nodiscard]] constexpr uint8_t R() const { return r; }
    [[nodiscard]] constexpr uint8_t G() const { return g; }
    [[nodiscard]] constexpr uint8_t B() const { return b; }
    [[nodiscard]] constexpr uint8_t A() const { return a; }

    [[nodiscard]] constexpr float Rf() const { return r / 255.0f; }
    [[nodiscard]] constexpr float Gf() const { return g / 255.0f; }
    [[nodiscard]] constexpr float Bf() const { return b / 255.0f; }
    [[nodiscard]] constexpr float Af() const { return a / 255.0f; }

    constexpr Color_t& SetR(uint8_t value) { r = value; return *this; }
    constexpr Color_t& SetG(uint8_t value) { g = value; return *this; }
    constexpr Color_t& SetB(uint8_t value) { b = value; return *this; }
    constexpr Color_t& SetA(uint8_t value) { a = value; return *this; }

    constexpr Color_t& SetRf(float value) { r = static_cast<uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f)); return *this; }
    constexpr Color_t& SetGf(float value) { g = static_cast<uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f)); return *this; }
    constexpr Color_t& SetBf(float value) { b = static_cast<uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f)); return *this; }
    constexpr Color_t& SetAf(float value) { a = static_cast<uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f)); return *this; }

    constexpr Color_t(const Color_t& color_second, float a) :
        r(color_second.r),
        g(color_second.g),
        b(color_second.b),
        a(static_cast<uint8_t>(std::clamp(a * 255.0f, 0.0f, 255.0f))) {
    }

    [[nodiscard]] uint32_t ToU32(float alpha_multiplier = 1.0f) const {
        const uint8_t alpha = static_cast<uint8_t>(std::clamp(static_cast<float>(a) * alpha_multiplier, 0.0f, 255.0f));
        return IM_COL32(r, g, b, alpha);
    }
    [[nodiscard]] ImVec4 ToVec4(float alpha_multiplier = 1.0f) const {
        const float alpha = std::clamp(Af() * alpha_multiplier, 0.0f, 1.0f);
        return ImVec4(Rf(), Gf(), Bf(), alpha);
    }
    [[nodiscard]] constexpr uint32_t ToHex() const { return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a); }
    [[nodiscard]] std::array<float, 4> ToArray() const { return { Rf(), Gf(), Bf(), Af() }; }

    static Color_t FromHsv(float hue, float saturation, float value) {
        const float h = std::fmod(hue, 1.0f) * 6.0f;
        const int i = static_cast<int>(std::floor(h));
        const float f = h - i;
        const float p = value * (1.0f - saturation);
        const float q = value * (1.0f - saturation * f);
        const float C_T = value * (1.0f - saturation * (1.0f - f));

        float r, g, b;
        switch (i % 6) {
        case 0: r = value; g = C_T; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = C_T; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = C_T; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
        }
        return Color_t(r, g, b);
    }

    [[nodiscard]] std::tuple<float, float, float> ToHsv() const {
        const float r = Rf(), g = Gf(), b = Bf();
        const float max_val = std::max({ r, g, b });
        const float min_val = std::min({ r, g, b });
        const float delta = max_val - min_val;

        float h = 0.0f;
        if (delta > 0.0f) {
            if (max_val == r)
                h = std::fmod((g - b) / delta, 6.0f);
            else if (max_val == g)
                h = (b - r) / delta + 2.0f;
            else
                h = (r - g) / delta + 4.0f;
            h /= 6.0f;
        }

        const float s = (max_val > 0.0f) ? delta / max_val : 0.0f;
        return { h, s, max_val };
    }

    constexpr bool operator==(const Color_t& other) const { return r == other.r && g == other.g && b == other.b && a == other.a; }
    constexpr bool operator!=(const Color_t& other) const { return !(*this == other); }
    constexpr Color_t operator+(const Color_t& other) const { return Color_t(std::min(255, static_cast<int>(r) + static_cast<int>(other.r)), std::min(255, static_cast<int>(g) + static_cast<int>(other.g)), std::min(255, static_cast<int>(b) + static_cast<int>(other.b)), std::min(255, static_cast<int>(a) + static_cast<int>(other.a))); }

    constexpr Color_t& operator+=(const Color_t& other) {
        r = static_cast<uint8_t>(std::min(255, static_cast<int>(r) + static_cast<int>(other.r)));
        g = static_cast<uint8_t>(std::min(255, static_cast<int>(g) + static_cast<int>(other.g)));
        b = static_cast<uint8_t>(std::min(255, static_cast<int>(b) + static_cast<int>(other.b)));
        a = static_cast<uint8_t>(std::min(255, static_cast<int>(a) + static_cast<int>(other.a)));
        return *this;
    }

    constexpr Color_t operator*(float multiplier) const { return Color_t(static_cast<uint8_t>(std::min(255.0f, static_cast<float>(r) * multiplier)), static_cast<uint8_t>(std::min(255.0f, static_cast<float>(g) * multiplier)), static_cast<uint8_t>(std::min(255.0f, static_cast<float>(b) * multiplier)), static_cast<uint8_t>(std::min(255.0f, static_cast<float>(a) * multiplier))); }

    constexpr Color_t& operator*=(float multiplier) {
        r = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(r) * multiplier));
        g = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(g) * multiplier));
        b = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(b) * multiplier));
        a = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(a) * multiplier));
        return *this;
    }

    constexpr uint8_t& operator[](size_t index) { return index == 0 ? r : (index == 1 ? g : (index == 2 ? b : a)); }

    constexpr const uint8_t& operator[](size_t index) const { return index == 0 ? r : (index == 1 ? g : (index == 2 ? b : a)); }

    ImU32 GetU32(float alpha_multiplier = -1.f) const {
        uint8_t alpha = static_cast<uint8_t>(std::clamp(this->a * alpha_multiplier, 0.0f, 255.0f));
        return IM_COL32(this->r, this->g, this->b, alpha);
    }

    ImVec4 GetVec4(float alpha_multiplier = -1.f) const {
        float alpha = std::clamp(this->Af() * alpha_multiplier, 0.0f, 1.0f);
        return ImVec4(this->Rf(), this->Gf(), this->Bf(), alpha);
    }

    static constexpr Color_t White() { return Color_t(255, 255, 255, 255); }
    static constexpr Color_t Black() { return Color_t(0, 0, 0, 255); }
    static constexpr Color_t Red() { return Color_t(255, 0, 0, 255); }
    static constexpr Color_t Green() { return Color_t(0, 255, 0, 255); }
    static constexpr Color_t Blue() { return Color_t(0, 0, 255, 255); }
    static constexpr Color_t Yellow() { return Color_t(255, 255, 0, 255); }
    static constexpr Color_t Cyan() { return Color_t(0, 255, 255, 255); }
    static constexpr Color_t Magenta() { return Color_t(255, 0, 255, 255); }
    static constexpr Color_t Transparent() { return Color_t(0, 0, 0, 0); }

    uint8_t r, g, b, a;
};

struct Color4_f {
    float r, g, b, a;
};

struct Colors_t {
    ImColor accent{ 152, 182, 248 };

    int current_theme{ 0 };

    ImColor text{ 255, 255, 255 };
    ImColor text_secondary{ 150, 150, 155 };
    ImColor text_faint{ 110, 110, 115 };
    ImColor divider{ 255, 255, 255 };

    struct {
        ImColor background{ 13, 13, 13 };
        ImColor sidebar{ 17, 18, 18 };
        ImColor separator{ 36, 37, 38 };
        ImColor avatar{ 38, 40, 44 };
        ImColor avatar_ring{ 58, 60, 66 };
        ImColor popup{ 22, 23, 24 };
        ImColor card{ 20, 21, 23 };
        ImColor close_btn{ 26, 28, 32 };
        ImColor bar{ 25, 25, 25 };
        ImColor name{ 255, 255, 255 };
        ImColor section_name{ 255, 255, 255 };
    } window;

    struct {
        ImColor background{ 19, 19, 19 };
        ImColor bar{ 19, 19, 19 };
        ImColor name{ 255, 255, 255 };
    } child;

    struct {
        ImColor background{ 25, 25, 25 };
        ImColor text_inactive{ 100, 100, 100 };
        ImColor text{ 255, 255, 255 };
    } section;

    struct {
        ImColor text_inactive{ 100, 100, 100 };
        ImColor text{ 255, 255, 255 };
    } sub_section;

    struct {
        ImColor background{ 33, 33, 33 };
        ImColor circle_active{ 255, 255, 255 };
        ImColor circle_inactive{ 100, 100, 100 };
        ImColor text{ 255, 255, 255 };
    } checkbox;

    struct {
        ImColor background{ 33, 33, 33 };
        ImColor text{ 255, 255, 255 };
        ImColor value{ 100, 100, 100 };
        ImColor circle{ 255, 255, 255 };
    } slider;

    struct {
        ImColor background{ 33, 33, 33 };
        ImColor text{ 255, 255, 255 };
        ImColor value{ 100, 100, 100 };
    } dropdown;

    struct {
        ImColor text{ 255, 255, 255 };
        ImColor text_inactive{ 100, 100, 100 };
    } selectable;

    struct {
        ImColor background{ 33, 33, 33 };
        ImColor value{ 255, 255, 255 };
        ImColor text{ 100, 100, 100 };
    } text_field;

    struct {
        ImColor background{ 33, 33, 33 };
        ImColor text{ 255, 255, 255 };
    } button;

    struct {
        ImColor background{ 33, 33, 33 };
        ImColor text{ 255, 255, 255 };
    } color_edit;

    struct {
        ImColor background{ 33, 33, 33 };
        ImColor text{ 255, 255, 255 };
        ImColor value{ 100, 100, 100 };
    } keybind;

    struct {
        ImColor background{ 25, 25, 25 };
        ImColor text{ 255, 255, 255 };
        ImColor text_inactive{ 100, 100, 100 };
        ImColor hovered{ 35, 35, 35 };
        ImColor active{ 123, 137, 255 };
        ImColor border{ 35, 35, 35 };
    } listbox;
};

inline Colors_t* Colors = new Colors_t();

// Применяет палитру к глобальному набору цветов. 0 - тёмная тема,
// 1 - светлая. Все виджеты берут цвета отсюда, поэтому достаточно
// вызвать это один раз при смене темы (и один раз при старте).
inline void ApplyGuiTheme(int theme) {
    Colors->current_theme = theme;

    if (theme == 1) {
        Colors->accent = ImColor(62, 110, 225);

        Colors->text = ImColor(22, 24, 28);
        Colors->text_secondary = ImColor(96, 100, 108);
        Colors->text_faint = ImColor(130, 134, 142);
        Colors->divider = ImColor(24, 26, 30);

        Colors->window.background = ImColor(242, 243, 246);
        Colors->window.sidebar = ImColor(231, 233, 238);
        Colors->window.separator = ImColor(208, 211, 218);
        Colors->window.avatar = ImColor(214, 217, 223);
        Colors->window.avatar_ring = ImColor(194, 198, 206);
        Colors->window.popup = ImColor(248, 249, 251);
        Colors->window.card = ImColor(236, 238, 243);
        Colors->window.close_btn = ImColor(220, 223, 229);
        Colors->window.bar = ImColor(232, 234, 239);
        Colors->window.name = ImColor(22, 24, 28);
        Colors->window.section_name = ImColor(22, 24, 28);

        Colors->child.background = ImColor(236, 238, 243);
        Colors->child.bar = ImColor(231, 233, 238);
        Colors->child.name = ImColor(22, 24, 28);

        Colors->section.background = ImColor(232, 234, 239);
        Colors->section.text_inactive = ImColor(140, 144, 152);
        Colors->section.text = ImColor(22, 24, 28);

        Colors->sub_section.text_inactive = ImColor(140, 144, 152);
        Colors->sub_section.text = ImColor(22, 24, 28);

        Colors->checkbox.background = ImColor(224, 227, 233);
        Colors->checkbox.circle_active = ImColor(255, 255, 255);
        Colors->checkbox.circle_inactive = ImColor(150, 154, 162);
        Colors->checkbox.text = ImColor(22, 24, 28);

        Colors->slider.background = ImColor(224, 227, 233);
        Colors->slider.text = ImColor(22, 24, 28);
        Colors->slider.value = ImColor(120, 124, 132);
        Colors->slider.circle = ImColor(255, 255, 255);

        Colors->dropdown.background = ImColor(224, 227, 233);
        Colors->dropdown.text = ImColor(22, 24, 28);
        Colors->dropdown.value = ImColor(120, 124, 132);

        Colors->selectable.text = ImColor(22, 24, 28);
        Colors->selectable.text_inactive = ImColor(140, 144, 152);

        Colors->text_field.background = ImColor(224, 227, 233);
        Colors->text_field.value = ImColor(22, 24, 28);
        Colors->text_field.text = ImColor(120, 124, 132);

        Colors->button.background = ImColor(224, 227, 233);
        Colors->button.text = ImColor(22, 24, 28);

        Colors->color_edit.background = ImColor(224, 227, 233);
        Colors->color_edit.text = ImColor(22, 24, 28);

        Colors->keybind.background = ImColor(224, 227, 233);
        Colors->keybind.text = ImColor(22, 24, 28);
        Colors->keybind.value = ImColor(120, 124, 132);

        Colors->listbox.background = ImColor(232, 234, 239);
        Colors->listbox.text = ImColor(22, 24, 28);
        Colors->listbox.text_inactive = ImColor(140, 144, 152);
        Colors->listbox.hovered = ImColor(214, 217, 223);
        Colors->listbox.active = ImColor(62, 110, 225);
        Colors->listbox.border = ImColor(205, 208, 214);
    } else {
        Colors->accent = ImColor(152, 182, 248);

        Colors->text = ImColor(255, 255, 255);
        Colors->text_secondary = ImColor(150, 150, 155);
        Colors->text_faint = ImColor(110, 110, 115);
        Colors->divider = ImColor(255, 255, 255);

        Colors->window.background = ImColor(13, 13, 13);
        Colors->window.sidebar = ImColor(17, 18, 18);
        Colors->window.separator = ImColor(36, 37, 38);
        Colors->window.avatar = ImColor(38, 40, 44);
        Colors->window.avatar_ring = ImColor(58, 60, 66);
        Colors->window.popup = ImColor(22, 23, 24);
        Colors->window.card = ImColor(20, 21, 23);
        Colors->window.close_btn = ImColor(26, 28, 32);
        Colors->window.bar = ImColor(25, 25, 25);
        Colors->window.name = ImColor(255, 255, 255);
        Colors->window.section_name = ImColor(255, 255, 255);

        Colors->child.background = ImColor(19, 19, 19);
        Colors->child.bar = ImColor(19, 19, 19);
        Colors->child.name = ImColor(255, 255, 255);

        Colors->section.background = ImColor(25, 25, 25);
        Colors->section.text_inactive = ImColor(100, 100, 100);
        Colors->section.text = ImColor(255, 255, 255);

        Colors->sub_section.text_inactive = ImColor(100, 100, 100);
        Colors->sub_section.text = ImColor(255, 255, 255);

        Colors->checkbox.background = ImColor(33, 33, 33);
        Colors->checkbox.circle_active = ImColor(255, 255, 255);
        Colors->checkbox.circle_inactive = ImColor(100, 100, 100);
        Colors->checkbox.text = ImColor(255, 255, 255);

        Colors->slider.background = ImColor(33, 33, 33);
        Colors->slider.text = ImColor(255, 255, 255);
        Colors->slider.value = ImColor(100, 100, 100);
        Colors->slider.circle = ImColor(255, 255, 255);

        Colors->dropdown.background = ImColor(33, 33, 33);
        Colors->dropdown.text = ImColor(255, 255, 255);
        Colors->dropdown.value = ImColor(100, 100, 100);

        Colors->selectable.text = ImColor(255, 255, 255);
        Colors->selectable.text_inactive = ImColor(100, 100, 100);

        Colors->text_field.background = ImColor(33, 33, 33);
        Colors->text_field.value = ImColor(255, 255, 255);
        Colors->text_field.text = ImColor(100, 100, 100);

        Colors->button.background = ImColor(33, 33, 33);
        Colors->button.text = ImColor(255, 255, 255);

        Colors->color_edit.background = ImColor(33, 33, 33);
        Colors->color_edit.text = ImColor(255, 255, 255);

        Colors->keybind.background = ImColor(33, 33, 33);
        Colors->keybind.text = ImColor(255, 255, 255);
        Colors->keybind.value = ImColor(100, 100, 100);

        Colors->listbox.background = ImColor(25, 25, 25);
        Colors->listbox.text = ImColor(255, 255, 255);
        Colors->listbox.text_inactive = ImColor(100, 100, 100);
        Colors->listbox.hovered = ImColor(35, 35, 35);
        Colors->listbox.active = ImColor(123, 137, 255);
        Colors->listbox.border = ImColor(35, 35, 35);
    }
}