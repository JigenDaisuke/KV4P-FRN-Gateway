#pragma once
#include "gateway.h"
#include "settings.h"

namespace webui {
void begin(Settings& s, Gateway& gw);
void loop();
}  // namespace webui
