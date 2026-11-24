#pragma once

// Проверка, что указатель действительно указывает на CMaterial2.
// ResourceSystem иногда отдаёт заглушку вместо скомпилированного ресурса
// (например, при загрузке имени без суффикса "_c"), и рендер падает на
// вызове виртуального метода такого объекта. Проверяем vtable: она должна
// лежать в образе materialsystem2.dll, а слот +0x110 (его дёргает рендер
// скайбокса) обязан быть непустым.
struct MaterialImageRange_t {
	uintptr_t base = 0;
	uintptr_t end = 0;
};

inline const MaterialImageRange_t& GetMaterialImageRange() {
	static MaterialImageRange_t range = []() {
		MaterialImageRange_t value{};

		void* handle = Memory->Modules.Materialsystem.Handle();
		if (!handle)
			return value;

		__try {
			auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(handle);
			auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uintptr_t>(handle) + dos->e_lfanew);
			value.base = reinterpret_cast<uintptr_t>(handle);
			value.end = value.base + nt->OptionalHeader.SizeOfImage;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}

		return value;
	}();

	return range;
}

inline bool IsValidMaterial(const void* pointer) {
	if (!pointer || reinterpret_cast<uintptr_t>(pointer) < 0x10000)
		return false;

	__try {
		const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(pointer);
		if (vtable < 0x10000)
			return false;

		const auto& range = GetMaterialImageRange();
		if (range.base && (vtable < range.base || vtable >= range.end))
			return false;

		return *reinterpret_cast<const uintptr_t*>(vtable + 0x110) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

struct CMaterialParameter {
	Vector4D vec;
	void* texture_value;
	char pad[0x10];
	const char* parameter_name;
	const char* value_text;
	int64_t value;
};

struct CMaterial2 {
	virtual const char* GetName() = 0;
	virtual const char* GetNameWithMod() = 0;
	virtual void Func2() = 0;
	virtual void Func3() = 0;
	virtual bool IsLoaded() = 0;

	CMaterialParameter* FindParameter(const char* name) {
		using fn_find_parameter = CMaterialParameter*(__fastcall*)(CMaterial2*, const char*);
		static const auto find_parameter = Memory->Modules.Materialsystem.Find(xs("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 59 ? 48 8B F2 48 63 79 ? 48 C1 E7 ? 48 03 FB 48 3B DF")).As<fn_find_parameter>();
		return find_parameter(this, name);
	}

	void* vmt;
	char pad0[0x10];
	int parameter_count;
	char pad1[0x04];
	CMaterialParameter* parameters;
	char pad2[0x3F0];
	int num_renderables_to_draw;
	char pad3[0x04];
	void* mesh_draw_primitives;
};

struct CMaterialSystem {
	void CreateMaterial(CStrongHandle<CMaterial2>* out_mat, const char* material_name, CKeyValues3* key_values) { return Memory->Vtable.Execute<void>(this, 29, out_mat, material_name, key_values, 0, 0, 0, 0, 0, 1); }
};