#pragma once

#include "Types.h"

#include <cstdint>

namespace Sky::RHI
{

struct TextureDesc
{
  Format format      = Format::Undefined;
  uint32_t width     = 0;
  uint32_t height    = 0;
  TextureUsage usage = TextureUsage::Sampled;
  uint32_t mipLevels = 1;
};

}