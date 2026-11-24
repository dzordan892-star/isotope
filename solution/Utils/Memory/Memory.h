#pragma once
#include <libloaderapi.h>

struct Address_t {
    Address_t() : pointer(0x0) {}

    template <typename t = uintptr_t>
    Address_t(t p) {
        this->pointer = reinterpret_cast<uintptr_t>(p);
    }

    Address_t Add(ptrdiff_t offset) {
        if (this->pointer)
            this->pointer += offset;
        return *this;
    }

    Address_t Sub(ptrdiff_t offset) {
        if (this->pointer)
            this->pointer -= offset;
        return *this;
    }

    Address_t Deref() {
        if (!this->pointer) {
            this->pointer = 0;
            return *this;
        }

        __try {
            this->pointer = *reinterpret_cast<uintptr_t*>(this->pointer);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            this->pointer = 0;
        }
        return *this;
    }

    Address_t Rel(int offset, int size) {
        if (!this->pointer) {
            this->pointer = 0;
            return *this;
        }

        __try {
            this->pointer += *reinterpret_cast<int32_t*>(this->pointer + offset);
            this->pointer += size;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            this->pointer = 0;
        }
        return *this;
    }

    Address_t Abs(int pre, int post) {
        if (!this->pointer) {
            this->pointer = 0;
            return *this;
        }

        __try {
            this->pointer += pre;
            this->pointer += sizeof(int32_t) + *reinterpret_cast<int32_t*>(this->pointer);
            this->pointer += post;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            this->pointer = 0;
        }
        return *this;
    }

    template<typename t>
    t As() const { return reinterpret_cast<t>(this->pointer); }

    uintptr_t Get() const { return this->pointer; }
    explicit operator bool() const { return this->pointer != 0; }
private:
    uintptr_t pointer;
};

struct Module_t {
    Module_t() = default;
    Module_t(const char* name) { this->pointer = name ? GetModuleHandleA(name) : nullptr; }

    void* Handle() const { return this->pointer; }
    bool Valid() const { return this->pointer != nullptr; }

    static std::vector<int> ToBytes(const std::string_view& pattern);
    Address_t Find(std::string_view pattern);

    template <typename t>
    t FindInterface(const char* name) const {
        if (!this->pointer || !name)
            return nullptr;

        using fn = t(*)(const char*, int*);
        const auto func = reinterpret_cast<fn>(GetProcAddress(reinterpret_cast<HMODULE>(this->pointer), xs("CreateInterface")));
        if (!func)
            return nullptr;

        return func(name, nullptr);
    }
private:
    void* pointer = nullptr;
};

struct Memory_t {
	struct Vtable_t {
        template<typename type>
        [[nodiscard]] type Get(void* class_ptr, unsigned int index) const {
            if (!class_ptr)
                return type{};
            return reinterpret_cast<type>((*static_cast<uintptr_t**>(class_ptr))[index]);
        }

        template<typename type = void, typename... args>
        [[nodiscard]] type Execute(void* class_ptr, unsigned int index, args... arg) const {
            if (!class_ptr) {
                if constexpr (std::is_void_v<type>)
                    return;
                else
                    return type{};
            }

            auto func = reinterpret_cast<type(__thiscall*)(void*, args...)>((*static_cast<uintptr_t**>(class_ptr))[index]);
            return func(class_ptr, arg...);
        }
	} Vtable;

    struct Modules_t {
        Module_t Client, Server, Engine2, Schema, Tier0, Input, Navsystem, Rendersystem, Localize, Filesystem, Meshsystem, Particles, Panorama, Scenesystem, Materialsystem, Resourcesystem, Animationsystem, Soundsystem, Worldrenderer;
        
        void Fill();
        void Clear();
    } Modules;
};

inline Memory_t* Memory = new Memory_t();