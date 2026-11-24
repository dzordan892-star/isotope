#include <shared.h>

void Memory_t::Modules_t::Fill() {
	this->Client = Module_t(xs("client.dll"));
	this->Server = Module_t(xs("server.dll"));
	this->Engine2 = Module_t(xs("engine2.dll"));
	this->Schema = Module_t(xs("schemasystem.dll"));
	this->Input = Module_t(xs("inputsystem.dll"));
	this->Tier0 = Module_t(xs("tier0.dll"));
	this->Navsystem = Module_t(xs("navsystem.dll"));
	this->Rendersystem = Module_t(xs("rendersystemdx11.dll"));
	this->Localize = Module_t(xs("localize.dll"));
	this->Filesystem = Module_t(xs("filesystem_stdio.dll"));
	this->Meshsystem = Module_t(xs("meshsystem.dll"));
	this->Particles = Module_t(xs("particles.dll"));
	this->Panorama = Module_t(xs("panorama.dll"));
	this->Scenesystem = Module_t(xs("scenesystem.dll"));
	this->Materialsystem = Module_t(xs("materialsystem2.dll"));
	this->Resourcesystem = Module_t(xs("resourcesystem.dll"));
	this->Animationsystem = Module_t(xs("animationsystem.dll"));
	this->Soundsystem = Module_t(xs("soundsystem.dll"));
	this->Worldrenderer = Module_t(xs("worldrenderer.dll"));
}

void Memory_t::Modules_t::Clear() {
	this->Client = nullptr;
	this->Server = nullptr;
	this->Engine2 = nullptr;
	this->Schema = nullptr;
	this->Input = nullptr;
	this->Tier0 = nullptr;
	this->Navsystem = nullptr;
	this->Rendersystem = nullptr;
	this->Localize = nullptr;
	this->Filesystem = nullptr;
	this->Meshsystem = nullptr;
	this->Particles = nullptr;
	this->Panorama = nullptr;
	this->Scenesystem = nullptr;
	this->Materialsystem = nullptr;
	this->Resourcesystem = nullptr;
	this->Animationsystem = nullptr;
	this->Soundsystem = nullptr;
	this->Worldrenderer = nullptr;
}

std::vector<int> Module_t::ToBytes(const std::string_view& pattern) {
    std::vector<int> bytes{};
    bytes.reserve(pattern.size() / 2);

    const char* current = pattern.data();
    const char* end = pattern.data() + pattern.size();

    while (current < end) {
        while (current < end && *current == ' ')
            ++current;
        if (current >= end)
            break;

        if (*current == '?') {
            ++current;
            if (current < end && *current == '?')
                ++current;
            bytes.push_back(-1);
            continue;
        }

        char* parse_end = nullptr;
        const auto value = strtoul(current, &parse_end, 16);
        if (parse_end == current)
            break;
        bytes.push_back(static_cast<int>(value));
        current = parse_end;
    }

    return bytes;
}

namespace {
    uint8_t* ScanPatternSafe(uint8_t* base, size_t image_size, const int* pattern, size_t pattern_size) {
        if (!base || !pattern || pattern_size == 0 || pattern_size > image_size)
            return nullptr;

        __try {
            auto* dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
            if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
                return nullptr;

            auto* nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos_header->e_lfanew);
            if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
                return nullptr;

            const size_t size_of_image = nt_headers->OptionalHeader.SizeOfImage;
            if (size_of_image < image_size)
                image_size = size_of_image;
            if (pattern_size > image_size)
                return nullptr;

            for (size_t i = 0; i + pattern_size <= image_size; ++i) {
                bool found = true;
                for (size_t j = 0; j < pattern_size; ++j) {
                    const int byte = pattern[j];
                    if (byte == -1)
                        continue;
                    if (base[i + j] != static_cast<uint8_t>(byte)) {
                        found = false;
                        break;
                    }
                }

                if (found)
                    return base + i;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }

        return nullptr;
    }
}

Address_t Module_t::Find(std::string_view pattern) {
    if (!this->pointer || pattern.empty())
        return nullptr;

    const auto pattern_bytes = this->ToBytes(pattern);
    if (pattern_bytes.empty())
        return nullptr;

    auto* base = reinterpret_cast<uint8_t*>(this->pointer);
    // Upper bound passed for sanity; ScanPatternSafe reads actual SizeOfImage from headers.
    constexpr size_t kMaxImageSize = 0x40000000u;
    return ScanPatternSafe(base, kMaxImageSize, pattern_bytes.data(), pattern_bytes.size());
}