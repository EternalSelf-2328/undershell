// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace undershell {

class WeatherService;

// A blank place is an indoor scene with simulated rain.
bool dioramaRain(WeatherService* weather, const std::string& place);

}  // namespace undershell
