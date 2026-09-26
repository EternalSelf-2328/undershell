// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace undershell {
// docs/OPTIONS.md, generated from schema.hpp (tests/test_docs.cpp keeps the
// committed file in sync): `undershell --doc > docs/OPTIONS.md`
std::string optionsMarkdown();
}  // namespace undershell
