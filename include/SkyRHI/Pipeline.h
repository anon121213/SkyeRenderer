#pragma once

#include "Types.h"
#include "Handle.h"

#include <cstdint>
#include <vector>

namespace Sky::RHI
{

struct VertexAttribute
{
  uint32_t location = 0;                 // matches layout(location=N) in shader
  Format   format   = Format::Undefined; // e.g. RGB32_SFLOAT for vec3
  uint32_t offset   = 0;                 // byte offset within the vertex
};

struct GraphicsPipelineDesc
{
  ShaderHandle vertexShader;
  ShaderHandle fragmentShader;

  uint32_t                     vertexStride = 0;                  // bytes per vertex (0 = no vertex input)
  std::vector<VertexAttribute> vertexAttributes;
  Format                       colorFormat = Format::Undefined;   // color attachment format (dynamic rendering)
  Format                       depthFormat = Format::Undefined;
  uint32_t                     pushConstantSize = 0;              // bytes of push constant data (0 = none)
  DescriptorSetLayoutHandle descriptorSetLayout;                  // optional — 0 = no descriptors
  bool depthTest  = true;   // default — как сейчас
  bool depthWrite = true;
};

}