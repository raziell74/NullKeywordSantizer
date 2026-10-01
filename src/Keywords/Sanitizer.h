#pragma once

#include <string_view>

namespace Keywords
{
	void InstallCopyHook();
	void ScanItems(std::string_view a_reason);
}
