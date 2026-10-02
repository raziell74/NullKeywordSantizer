#include "PCH.h"

#include "Keywords/Sanitizer.h"

#include "Settings/Settings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ranges>
#include <string_view>
#include <vector>

namespace
{
	using CopyComponent_t = void (*)(RE::BGSKeywordForm*, RE::BaseFormComponent*);

	CopyComponent_t             g_originalCopy{ nullptr };
	std::vector<std::uintptr_t> g_itemKeywordVtables{};

	constexpr std::array<RE::FormType, 11> kItemTypes{
		RE::FormType::Armor,
		RE::FormType::Weapon,
		RE::FormType::Ammo,
		RE::FormType::Misc,
		RE::FormType::KeyMaster,
		RE::FormType::SoulGem,
		RE::FormType::Book,
		RE::FormType::Note,
		RE::FormType::AlchemyItem,
		RE::FormType::Ingredient,
		RE::FormType::Scroll
	};

	struct CompleteObjectLocator
	{
		std::uint32_t signature;
		std::uint32_t offset;
		std::uint32_t cdOffset;
		std::int32_t  typeDescriptor;
		std::int32_t  classDescriptor;
		std::int32_t  self;
	};

	[[nodiscard]] RE::BGSKeywordForm* KeywordFormOf(RE::TESForm* a_form)
	{
		if (auto* ammo = a_form->As<RE::TESAmmo>()) {
			return ammo->AsKeywordForm();
		}
		return a_form->As<RE::BGSKeywordForm>();
	}

	[[nodiscard]] bool IsBlankKeyword(const RE::BGSKeyword* a_keyword)
	{
		return !a_keyword || a_keyword->formEditorID.empty();
	}

	[[nodiscard]] const RE::TESForm* OwningForm(const RE::BGSKeywordForm* a_component)
	{
		if (!a_component) {
			return nullptr;
		}

		const auto vtable = *reinterpret_cast<const std::uintptr_t*>(a_component);
		if (!vtable) {
			return nullptr;
		}

		const auto* col = *reinterpret_cast<const CompleteObjectLocator* const*>(vtable - sizeof(void*));
		if (!col || col->signature != 1 || col->offset == 0 || col->offset >= 0x300) {
			return nullptr;
		}

		return reinterpret_cast<const RE::TESForm*>(
			reinterpret_cast<const std::byte*>(a_component) - col->offset);
	}

	[[nodiscard]] const char* FormTypeLabel(RE::FormType a_type)
	{
		switch (a_type) {
		case RE::FormType::Keyword:
			return "KYWD";
		case RE::FormType::Armor:
			return "ARMO";
		case RE::FormType::Weapon:
			return "WEAP";
		case RE::FormType::Ammo:
			return "AMMO";
		case RE::FormType::Misc:
			return "MISC";
		case RE::FormType::KeyMaster:
			return "KEYM";
		case RE::FormType::SoulGem:
			return "SLGM";
		case RE::FormType::Book:
			return "BOOK";
		case RE::FormType::Note:
			return "NOTE";
		case RE::FormType::AlchemyItem:
			return "ALCH";
		case RE::FormType::Ingredient:
			return "INGR";
		case RE::FormType::Scroll:
			return "SCRL";
		default:
			return "????";
		}
	}

	void LogRemoval(const RE::BGSKeyword* a_keyword, const RE::TESForm* a_item)
	{
		if (Settings::Get().level > spdlog::level::debug) {
			return;
		}

		const auto        itemId = a_item ? a_item->GetFormID() : 0u;
		const auto        itemType = a_item ? a_item->GetFormType() : RE::FormType::None;
		const char*       itemName = a_item ? a_item->GetName() : nullptr;
		if (!itemName || itemName[0] == '\0') {
			itemName = "<no name>";
		}

		if (!a_keyword) {
			SKSE::log::debug(
				"Removed keyword <none>\n"
				"    from:  {:08X} ({})\n"
				"    name:  {}",
				itemId,
				FormTypeLabel(itemType),
				itemName);
			return;
		}

		const char* editorId = a_keyword->GetFormEditorID();
		if (!editorId || editorId[0] == '\0') {
			editorId = "<blank>";
		}

		SKSE::log::debug(
			"Removed keyword {:08X} ({})\n"
			"    editorID:  {}\n"
			"    flags:     {:08X}\n"
			"    from:      {:08X} ({})\n"
			"    name:      {}",
			a_keyword->GetFormID(),
			FormTypeLabel(a_keyword->GetFormType()),
			editorId,
			a_keyword->GetFormFlags(),
			itemId,
			FormTypeLabel(itemType),
			itemName);
	}

	std::uint32_t RemoveBlankKeywords(RE::BGSKeywordForm* a_keywords, const RE::TESForm* a_item)
	{
		if (!a_keywords || !a_keywords->keywords || a_keywords->numKeywords == 0) {
			return 0;
		}

		std::uint32_t write = 0;
		std::uint32_t removed = 0;
		for (std::uint32_t read = 0; read < a_keywords->numKeywords; ++read) {
			auto* keyword = a_keywords->keywords[read];
			if (!IsBlankKeyword(keyword)) {
				a_keywords->keywords[write++] = keyword;
			} else {
				LogRemoval(keyword, a_item);
				++removed;
			}
		}

		a_keywords->numKeywords = write;
		return removed;
	}

	[[nodiscard]] std::optional<std::uint8_t> DecodeRelocatableLength(const std::uint8_t* a_code)
	{
		std::size_t i = 0;
		bool        operandSize16 = false;
		for (int prefix = 0; prefix < 14; ++prefix) {
			const auto b = a_code[i];
			if (b == 0x66) {
				operandSize16 = true;
				++i;
				continue;
			}
			if (b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36 || b == 0x64 || b == 0x65) {
				++i;
				continue;
			}
			break;
		}

		bool rexW = false;
		if ((a_code[i] & 0xF0) == 0x40) {
			rexW = (a_code[i] & 0x08) != 0;
			++i;
		}

		const auto opcode = a_code[i++];
		if (opcode == 0xE8 || opcode == 0xE9 || opcode == 0xEB || opcode == 0x0F || (opcode & 0xF0) == 0x70) {
			return std::nullopt;
		}

		if (opcode >= 0x50 && opcode <= 0x5F) {
			return static_cast<std::uint8_t>(i);
		}
		if (opcode == 0x90 || opcode == 0xC3) {
			return static_cast<std::uint8_t>(i);
		}

		bool         hasModrm = false;
		std::uint8_t imm = 0;
		if (opcode >= 0xB8 && opcode <= 0xBF) {
			imm = rexW ? 8 : (operandSize16 ? 2 : 4);
			i += imm;
			return i <= 15 ? std::optional<std::uint8_t>{ static_cast<std::uint8_t>(i) } : std::nullopt;
		}

		switch (opcode) {
		case 0x80:
		case 0x82:
		case 0x83:
		case 0xC0:
		case 0xC1:
		case 0xC6:
			hasModrm = true;
			imm = 1;
			break;
		case 0x81:
		case 0xC7:
			hasModrm = true;
			imm = operandSize16 ? 2 : 4;
			break;
		case 0xF6:
		case 0xF7:
			hasModrm = true;
			imm = opcode;
			break;
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x20:
		case 0x21:
		case 0x22:
		case 0x23:
		case 0x28:
		case 0x29:
		case 0x2A:
		case 0x2B:
		case 0x30:
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x38:
		case 0x39:
		case 0x3A:
		case 0x3B:
		case 0x63:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
		case 0x88:
		case 0x89:
		case 0x8A:
		case 0x8B:
		case 0x8D:
		case 0x8F:
		case 0xD0:
		case 0xD1:
		case 0xD2:
		case 0xD3:
		case 0xFE:
		case 0xFF:
			hasModrm = true;
			break;
		default:
			return std::nullopt;
		}

		if (!hasModrm) {
			return std::nullopt;
		}

		const auto modrm = a_code[i++];
		const auto mod = static_cast<std::uint8_t>(modrm >> 6);
		const auto reg = static_cast<std::uint8_t>((modrm >> 3) & 7);
		const auto rm = static_cast<std::uint8_t>(modrm & 7);
		if (mod != 3 && rm == 4) {
			const auto sib = a_code[i++];
			const auto base = static_cast<std::uint8_t>(sib & 7);
			if (mod == 0 && base == 5) {
				i += 4;
			}
		} else if (mod == 0 && rm == 5) {
			return std::nullopt;
		}

		if (mod == 1) {
			i += 1;
		} else if (mod == 2) {
			i += 4;
		}

		if (opcode == 0xF6) {
			imm = reg == 0 ? 1 : 0;
		} else if (opcode == 0xF7) {
			imm = reg == 0 ? (operandSize16 ? 2 : 4) : 0;
		}

		i += imm;
		if (i == 0 || i > 15) {
			return std::nullopt;
		}
		return static_cast<std::uint8_t>(i);
	}

	[[nodiscard]] bool DecoderAcceptsKnownPrologue()
	{
		const std::uint8_t movRsp[] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
		const std::uint8_t pushRbx[] = { 0x40, 0x53 };
		const std::uint8_t subRsp[] = { 0x48, 0x83, 0xEC, 0x20 };
		const auto         mov = DecodeRelocatableLength(movRsp);
		const auto         push = DecodeRelocatableLength(pushRbx);
		const auto         sub = DecodeRelocatableLength(subRsp);
		return mov == 5 && push == 2 && sub == 4;
	}

	[[nodiscard]] std::optional<std::uint8_t> RelocatablePrefix(std::uintptr_t a_target)
	{
		if (!DecoderAcceptsKnownPrologue()) {
			return std::nullopt;
		}

		const auto* code = reinterpret_cast<const std::uint8_t*>(a_target);
		std::uint8_t total = 0;
		while (total < 5) {
			const auto length = DecodeRelocatableLength(code + total);
			if (!length || *length == 0 || total + *length > 24) {
				return std::nullopt;
			}
			total = static_cast<std::uint8_t>(total + *length);
		}
		return total;
	}

	[[nodiscard]] bool IsItemKeywordComponent(const RE::BGSKeywordForm* a_component)
	{
		if (!a_component) {
			return false;
		}

		const auto vtable = *reinterpret_cast<const std::uintptr_t*>(a_component);
		return std::ranges::find(g_itemKeywordVtables, vtable) != g_itemKeywordVtables.end();
	}

	void CopyComponentHook(RE::BGSKeywordForm* a_self, RE::BaseFormComponent* a_rhs)
	{
		if (g_originalCopy) {
			g_originalCopy(a_self, a_rhs);
		}

		if (!Settings::Get().enabled || !IsItemKeywordComponent(a_self)) {
			return;
		}

		RemoveBlankKeywords(a_self, OwningForm(a_self));
	}

	template <std::size_t N>
	void CollectItemVtables(const std::array<REL::VariantID, N>& a_vtables, std::uintptr_t a_copyComponent)
	{
		for (const auto& id : a_vtables) {
			const auto address = REL::Relocation<std::uintptr_t>(id).address();
			if (!address) {
				continue;
			}

			const auto slot = *reinterpret_cast<const std::uintptr_t*>(address + (3 * sizeof(void*)));
			if (slot == a_copyComponent) {
				g_itemKeywordVtables.push_back(address);
			}
		}
	}

	void RejectPatch(std::string_view a_reason)
	{
		SKSE::log::warn(
			"CopyComponent 5-byte patch was rejected ({}); keyword copies will be cleaned on the next scan",
			a_reason);
	}
}

namespace Keywords
{
	void ScanItems(std::string_view a_reason)
	{
		if (!Settings::Get().enabled) {
			return;
		}

		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			SKSE::log::warn("Keyword scan ({}) skipped; data handler is not ready", a_reason);
			return;
		}

		std::uint32_t removed = 0;
		std::uint32_t items = 0;
		for (const auto type : kItemTypes) {
			for (auto* form : data->GetFormArray(type)) {
				if (!form) {
					continue;
				}

				auto* keywords = KeywordFormOf(form);
				if (!keywords) {
					continue;
				}

				const auto count = RemoveBlankKeywords(keywords, form);
				if (count != 0) {
					removed += count;
					++items;
				}
			}
		}

		SKSE::log::info("Removed {} blank keywords from {} items ({})", removed, items, a_reason);
	}

	void InstallCopyHook()
	{
		if (!Settings::Get().enabled) {
			return;
		}

		const REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BGSKeywordForm[0] };
		if (!vtbl.address()) {
			RejectPatch("keyword vtable is unavailable");
			return;
		}

		const auto target = *reinterpret_cast<const std::uintptr_t*>(vtbl.address() + (3 * sizeof(void*)));
		if (!target) {
			RejectPatch("CopyComponent address is null");
			return;
		}

		const auto stolen = RelocatablePrefix(target);
		if (!stolen) {
			RejectPatch("prologue is not a relocatable 5-byte patch site");
			return;
		}

		g_itemKeywordVtables.clear();
		CollectItemVtables(RE::TESObjectARMO::VTABLE, target);
		CollectItemVtables(RE::TESObjectWEAP::VTABLE, target);
		CollectItemVtables(RE::TESAmmo::VTABLE, target);
		CollectItemVtables(RE::TESObjectMISC::VTABLE, target);
		CollectItemVtables(RE::TESKey::VTABLE, target);
		CollectItemVtables(RE::TESSoulGem::VTABLE, target);
		CollectItemVtables(RE::TESObjectBOOK::VTABLE, target);
		CollectItemVtables(RE::AlchemyItem::VTABLE, target);
		CollectItemVtables(RE::IngredientItem::VTABLE, target);
		CollectItemVtables(RE::ScrollItem::VTABLE, target);
		std::ranges::sort(g_itemKeywordVtables);
		const auto unique = std::ranges::unique(g_itemKeywordVtables);
		g_itemKeywordVtables.erase(unique.begin(), unique.end());

		if (g_itemKeywordVtables.empty()) {
			RejectPatch("no inventory item keyword vtables use CopyComponent");
			return;
		}

		SKSE::AllocTrampoline(64);
		auto& trampoline = SKSE::GetTrampoline();

		constexpr std::size_t absJmp = 14;
		auto*                 cave = static_cast<std::uint8_t*>(trampoline.allocate(*stolen + absJmp));
		if (!cave) {
			RejectPatch("trampoline is out of space");
			return;
		}

		std::memcpy(cave, reinterpret_cast<const void*>(target), *stolen);
		cave[*stolen + 0] = 0xFF;
		cave[*stolen + 1] = 0x25;
		cave[*stolen + 2] = 0;
		cave[*stolen + 3] = 0;
		cave[*stolen + 4] = 0;
		cave[*stolen + 5] = 0;
		const auto resume = target + *stolen;
		std::memcpy(cave + *stolen + 6, &resume, sizeof(resume));
		g_originalCopy = reinterpret_cast<CopyComponent_t>(cave);

		if (*stolen > 5) {
			std::uint8_t nops[24]{};
			const auto   tail = static_cast<std::size_t>(*stolen - 5);
			std::fill_n(nops, tail, static_cast<std::uint8_t>(0x90));
			if (!REL::safe_write(target + 5, nops, tail, reinterpret_cast<const void*>(target + 5), tail)) {
				g_originalCopy = nullptr;
				RejectPatch("tail nop was rejected");
				return;
			}
		}

		trampoline.write_branch<5>(target, &CopyComponentHook);
		SKSE::log::info("Hooked BGSKeywordForm::CopyComponent at {:X} ({} bytes)", target, *stolen);
	}

}
