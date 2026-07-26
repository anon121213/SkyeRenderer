#pragma once

#include "CommandList.h"
#include "Handle.h"
#include "Types.h"

#include <cstdint>
#include <memory>
#include <functional>

namespace Sky::RHI
{

struct FGResource
{
  uint32_t index = 0;
};

struct FGTextureDesc
{
  Format format = Format::Undefined;
  uint32_t width = 0;
  uint32_t height = 0;
};

enum class FGAttachmentType : uint32_t
{
  Color,
  Depth,
};

class FGResources
{
public:
  [[nodiscard]] TextureHandle getTexture(FGResource resource) const noexcept;
};

class PassBuilder
{
public:
  [[nodiscard]] FGResource importSwapchain(SwapchainHandle swapchain);
  [[nodiscard]] FGResource importColorTarget(TextureHandle texture);
  [[nodiscard]] FGResource createTexture(const char* name, const FGTextureDesc& desc);

  FGResource read(FGResource resource);
  FGResource writeColor(FGResource resource);
  FGResource writeDepth(FGResource resource);

private:
  friend class FrameGraph;

  PassBuilder(FrameGraph& graph, uint32_t passIndex) noexcept
    : m_Graph(&graph), m_PassIndex(passIndex) {}

  FrameGraph*  m_Graph    = nullptr;
  uint32_t    m_PassIndex = 0;
};

class Device;
class FrameGraph
{
public:
  explicit FrameGraph(Device& device);
  ~FrameGraph();

  FrameGraph(const FrameGraph&) = delete;
  FrameGraph& operator=(const FrameGraph&) = delete;

  template<typename PassData>
  const PassData& addRasterPass(
    const char* name,
    const std::function<void(PassBuilder&, PassData&)>& setup,
    std::function<void(const PassData&, FGResources&, CommandList&)> execute)
  {
    const uint32_t passIndex = beginPass(name);

    auto data = std::make_unique<PassData>();
    PassData* dataPtr = data.get();

    PassBuilder builder(*this, passIndex);
    setup(builder, *dataPtr);

    auto erasedExecute = [dataPtr, exec = std::move(execute)]
                         (FGResources& res, CommandList& cmd)
    {
      exec(*dataPtr, res, cmd);
    };

    finalizePass(passIndex, std::move(erasedExecute),
             std::unique_ptr<void, void(*)(void*)>(
                 data.release(),
                 [](void* p){ delete static_cast<PassData*>(p); }));

    return *dataPtr;
  }

  void compile();
  void execute(CommandList& cmd);

private:
  friend class PassBuilder;

  struct Pass;
  struct Impl;
  std::unique_ptr<Impl> m_Impl;

  using ErasedExecuteFn = std::function<void(FGResources&, CommandList&)>;
  using ErasedData = std::unique_ptr<void, void(*)(void*)>;

  uint32_t beginPass(const char* name);
  void finalizePass(uint32_t passIndex, ErasedExecuteFn execute, ErasedData data);

  FGResource registerImportedSwapchain(SwapchainHandle swapchainHandle);
  FGResource registerTransientTexture(const char* name, const FGTextureDesc& desc);
  FGResource registerImportedTexture(TextureHandle texture);

  void recordRead(uint32_t passIndex, FGResource resource);
  void recordWrite(uint32_t passIndex, FGResource resource, FGAttachmentType type);
};

}