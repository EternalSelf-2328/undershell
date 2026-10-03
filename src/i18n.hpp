// SPDX-License-Identifier: GPL-3.0-or-later
// The interface language: English or Spanish. "system" (the default) follows
// the locale (LC_ALL, LC_MESSAGES, LANG: es_* is Spanish, anything else
// English); [general] language = "en" | "es" pins it.
#pragma once

#include <string>

namespace undershell {

void setUiLanguage(const std::string& lang);  // system | en | es
[[nodiscard]] const std::string& uiLanguage();
[[nodiscard]] bool spanishUi();
// dates follow the interface when it is pinned, else LC_TIME
[[nodiscard]] bool spanishDates();
// what an option value reads as in the inspector ("inside_out" -> "Inside out"
// / "De dentro afuera"); the config keeps the value itself
[[nodiscard]] std::string optionLabel(const std::string& value, bool es);

}  // namespace undershell
