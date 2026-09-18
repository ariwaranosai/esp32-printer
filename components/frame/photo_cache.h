#pragma once
#include "frame.h"
namespace frame {
// Cache contains only the six-colour photo rectangle; UI is always drawn fresh.
bool render_photo_cached(const std::string &source, Fit fit, const std::string &cache,
                         Canvas &canvas, std::string &error, bool &cache_hit);
}
