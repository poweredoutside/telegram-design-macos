#pragma once

namespace Core::Telega {

enum class MaterialOption {
	GoogleSans,
	Grouped,
	RoundedSelection,
	FloatingField,
	RoundedMenu,
	Expressive,
	Switches,
	MutedSwitches,
	Sliders,
};

[[nodiscard]] bool PreferredMaterialOption(MaterialOption option);
void SetPreferredMaterialOption(MaterialOption option, bool enabled);

} // namespace Core::Telega
