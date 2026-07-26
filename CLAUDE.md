# SkyRenderer — Claude Session Bootstrap

Этот файл читается в начале каждой сессии Claude чтобы восстановить контекст. **Он не про пользователя — он про меня (Claude), чтобы я знал что делать и как.**

## Проект в двух строках

**SkyRenderer** — standalone AAA-квалити low-level render framework на Vulkan. Standalone git-репа. Подключится в `SlyEngine` (движок пользователя) как git submodule. Цель — production-quality, portfolio-ready для позиции render programmer в AAA.

## Полный roadmap

**Читай `ROADMAP.md`** — там всё: locked architectural decisions, целевая структура репы, 12 фаз с задачами, timeline, метрики "готово". Это авторитетный документ.

## Code conventions (обязательно)

**Читай `~/.claude/projects/-Users-artur-Projects-TestVulkanRenderWithClaude/memory/code_conventions.md`**. Ключевое:
- **Все комменты в коде — только на English.** Русский OK только в docs/README/CLAUDE.md/chat.
- **Every enum value MUST have a comment** — inline описание после каждого value.
- `Sky::RHI::` namespace, `SKY_RHI_*` macros, `enum class : uint32_t`, `[[nodiscard]]`, `explicit`, m_PascalCase fields, camelCase methods.
- Handle-based RHI, RAII Graphics.

## Как я работаю с этим пользователем

**Читай `~/.claude/projects/-Users-artur-Projects-TestVulkanRenderWithClaude/memory/teaching_approach.md`**. Ключевые пункты:

1. **Я учитель, не code generator.** User учится Vulkan и C++, хочет уметь сам. Если я даю готовый код и он копирует — учёбы не будет.
2. **Что даю готовым:** инфраструктура (CMake, PCH, макросы), boilerplate, массовые механические рефакторы.
3. **Что даю как ТЗ (user пишет сам):** Vulkan логика, RAII wrappers, C++ паттерны которым он учится.
4. **Формат ТЗ:** концепт «зачем это» → спецификация «что должно быть» → подсказки «на что обратить внимание». Не диктовать код, оставить логику.
5. **После user кода:** проверяю через Read, отдельно 🔴 критические баги / 🟡 стилевые. Объясняю **почему** — не просто «должно быть X».
6. **Explanations:** развёрнуто с аналогиями (Unity, C#, OpenGL, Hazel). User просит объяснять «почему так, а не как иначе».
7. **User часто ошибается:** путает похожие имена (`deviceCount` vs `devicesCount`), забывает `{}` инициализацию структур, забывает `#include` явно, инвертирует boolean в `if/throw`, локальная переменная вместо `&m_Field` в `vkCreate*`.

## Locked architectural decisions

### Namespace

```
Sky::RHI::             ← весь наш renderer (Device, Buffer, Texture, CommandList, FrameGraph, ...)
Sky::Graphics::        ← optional high-level facade (Mesh, Material, MeshRenderer)
Sky::                  ← оставлен для consumer SlyEngine, не занимать
```

Ничего renderer-specific напрямую под `Sky::X` (без подnamespace) — только в `Sky::RHI::` или `Sky::Graphics::`.

### Macros

```
SKY_RHI_TRACE / INFO / WARN / ERROR / CRITICAL     ← наш логгер
SKY_RHI_VK_CHECK(expr, "msg")                       ← VkResult check
```

Никогда не использовать `SKY_INFO` etc. без префикса `_RHI_` — оставлен для engine.

### Two-tier API

```
Sky::Graphics ─────── (opt-in, RAII, для простых случаев)
       ↓ uses
Sky::RHI ──────────── (mandatory, handle-based, для ECS/DOTS)
```

### RHI = handle-based

```cpp
Sky::RHI::BufferHandle vb = device.createBuffer({...});   // POD, uint64_t
```

**Не** ownership-based (`std::unique_ptr<Buffer>`) в RHI. RAII только в Graphics.

### Threading

- API **thread-safe с дня 1** (mutex вокруг resource creation)
- Реализация **single-threaded** пока (до Phase 6 — Frame Graph advanced)
- Реальный MT recording приходит через FG, user сам `std::thread` не пишет

### Multi-window

**Один Device, много Swapchain**. Не «много Renderer».

### Backend polymorphism

**Пока не вводим.** Только Vulkan. Публичный API backend-agnostic по семантике, реализация прямо через Vulkan. Полиморфизм добавим когда придёт D3D12.

## Current state (обновлять при каждой фазе)

**Phase 0 (Bootstrap) — DONE ✅**
- Треугольник рендерится
- CMake, PCH, spdlog (`SPDLOG_USE_STD_FORMAT ON`), VMA v3.3.0, GLFW 3.4
- VulkanX классы: Instance, Device (+ VMA), Surface, Swapchain, RenderPass, Framebuffers, ShaderModule, Pipeline, CommandPool, Renderer

**Phase R (RHI Abstraction Refactor) — DONE ✅**
- Public API surface: только `include/SkyRHI/{Device,Types,Handle}.h`
- Все VulkanX переехали в `src/Vulkan/` (h+cpp вместе, internal)
- Core/Log переехали в `src/Core/` (internal)
- PCH в `src/skypch.h` — `PRIVATE` в CMake
- `Sky::RHI::Device` с PIMPL — hide VulkanX from consumer
- `Sky::RHI::Types.h` — 25+ enums (BackendType, Format, BufferUsage, MemoryType, ShaderStage, etc.)
- `Sky::RHI::Handle<Tag>` — phantom type pattern, 12 handle types
- Consumer main.cpp — 40 строк, без VulkanX includes
- Треугольник продолжает работать

**Phase 1 (Frame Graph MVP) — DONE ✅ (2026-07-13)**
- `include/SkyRHI/CommandList.h` — recording API (setViewport/setScissor/bindPipeline/draw), void* opaque backend, private ctor через friend
- `include/SkyRHI/Swapchain.h` — public handle-based, composite VulkanSwapchainEntry в pool
- `include/SkyRHI/FrameGraph.h` + `src/Common/FrameGraph.cpp` — **Variant A (templated PassData, Frostbite/UE RDG-style)**, type erasure через graph-owned PassData
- FrameGraph compiler: DAG + refcount culling + Kahn's topo sort
- FrameGraph execute: resource realization (imported swapchain → backbuffer), layout tracking, автоматические vkCmdPipelineBarrier, dynamic rendering per pass
- **src/Common/HandleAllocator.h** — thread-safe pool, generation counter (32:32 packed uint64_t)
- **Dynamic rendering** (не VkRenderPass) — VK_KHR_dynamic_rendering, VkPipelineRenderingCreateInfoKHR, explicit barriers
- **volk** meta-loader — все extension-функции runtime-loaded (`#include <volk.h>`, VK_NO_PROTOTYPES)
- **Validation layers** — VK_LAYER_KHRONOS_validation + debug messenger в VulkanInstance
- Frame lifecycle в Device::Impl (beginFrame/endFrame). VulkanRenderer/RenderPass/Framebuffers **удалены**
- Треугольник рендерится через настоящий FG, validation чистая

**Phase 2 (Buffers + Cube) — DONE ✅ (2026-07-19)**
- Buffers через VMA (createBuffer, uploadBufferData via staging, mapBuffer), VulkanBuffer RAII
- Vertex + index buffers, `CommandList::bindVertexBuffer/bindIndexBuffer/drawIndexed`
- **Public pipeline + shader creation** — createShader (bytecode), createGraphicsPipeline (GraphicsPipelineDesc: vertex layout, depth, push constants). Format→VkFormat translation.
- Push constants + MVP (glm в SkyApp, Y-flip, depth 0-1). CommandList::pushConstants + bound layout tracking.
- **Depth attachment** — первый transient в FG. VulkanImage (VkImage+view+VMA), deferred deletion (Impl::frameTransients, clear на beginFrame). FG execute обрабатывает Depth writes (realize создаёт transient, не resolve).
- **Path B — все хаки Phase 1 удалены.** Consumer-driven: beginFrame/endFrame/execute(FrameGraph&). Consumer создаёт shaders/pipeline/VB/IB и сам декларирует passes. Device ctor чистый. swapchainFormat/swapchainExtent геттеры.
- **Крутящийся 3D куб** (8 верш + 36 индексов, depth перекрытие граней), validation чистая

**Phase 3 (Textures & Descriptors) — DONE ✅ (2026-07-25)**
- `SkyRHI::Texture` (VulkanImage — 2D), `Sampler` (VulkanSampler RAII)
- `uploadTextureData` через staging (image copy + layout transitions via immediateSubmit)
- **Descriptor sets** — DescriptorSetLayout, DescriptorPool (100 each: sampler/UBO/storage), DescriptorSet; updateDescriptorSetTexture. VulkanDescriptorSet dtor пустой (pool frees all).
- Текстурированный куб (procedural checkerboard) — работал, потом заменён реальной моделью в Phase 4

**Phase 4 (PBR Shading) — DONE ✅ (2026-07-25)**
- Vertex format: pos + uv + normal + tangent (vec4, w=handedness)
- **Cook-Torrance BRDF** — GGX (D) / Smith-Schlick (G) / Fresnel-Schlick (F), вынесено в `shade()` функцию
- **Metallic-Roughness workflow** (glTF-конвенция: G=roughness, B=metallic)
- **Normal mapping** — tangent space, TBN, Gram-Schmidt re-ortho, handedness; тангенты вычисляются (Lengyel) если нет в файле
- **Direct lighting** — directional (sun) + point (attenuation 1/d²), через per-frame scene UBO (std140 + `updateDescriptorSetBuffer`)
- **glTF загрузка** — tinygltf в `SkyApp/src/GltfModel.{h,cpp}`, DamagedHelmet.glb, node-transform baking, image decode. Загрузчик = consumer-scaffolding (в движке будет своя asset-система, не RHI).
- Реальная PBR-модель (14556 verts) рендерится, validation чистая

**Phase 5 (Async Transfer) — DONE ✅ (2026-07-26)**
- Transfer queue в VulkanDevice — dedicated DMA если есть, иначе fallback на graphics (на MoltenVK — fallback, одно семейство). `transferQueue()`/`transferFamilyIndex()`/`hasDedicatedTransfer()`
- Timeline semaphore (Vulkan 1.2 core, включена фича): `transferTimeline` + `transferValue`
- `uploadTextureDataAsync` — async submit на transfer-очередь с timeline-сигналом (без waitIdle), staging в `pendingUploads` (unique_ptr, отложенная чистка)
- `collectFinishedTransfers` (on beginFrame) — **non-blocking poll** `vkGetSemaphoreCounterValue`, чистит завершённые. `flushTransfers` (blocking) — для старта.
- **НЕ реализовано (TODO для dedicated-железа, напр. RTX):** queue ownership transfer transfer→graphics + отдельный transfer command-pool. На MoltenVK не нужно (одно семейство → cmd из graphics-pool submit'ится в тот же family).
- Streaming manager (политика: что/когда/evict) — **движковое, не RHI** (осознанно отложено). RHI даёт механизм, политику строит consumer.

**ПОРЯДОК ИЗМЕНЁН: 7 идёт РАНЬШЕ 6.** Причина: Phase 6 (FG advanced: aliasing/async-compute/MT) оптимизирует много-пассовый пайплайн, которого пока нет (1 pass). Phase 7 создаёт пассы (shadow, IBL-compute, skybox) → потом Phase 6 их оптимизирует. Сначала workload, потом оптимизатор.

**ImGui интеграция — DONE ✅ (2026-07-26)** — фундамент debug-UI (твики Phase 7, frame debugger позже).
- ImGui 1.91.5 через FetchContent (нет CMakeLists → static lib target `imgui` из core + imgui_impl_glfw + imgui_impl_vulkan). `IMGUI_IMPL_VULKAN_USE_VOLK` (мы на volk).
- Целиком в SkyApp (app имеет GLFW + volk через skypch). RHI-ядро от ImGui НЕ зависит.
- RHI даёт: (1) `include/SkyRHI/ImGuiSupport.h` — `imguiVulkanInitInfo(Device&)` (raw хендлы, friend Device, сегрегированный debug-хедер), (2) `Device::recordOverlay(callback)` — backbuffer PRESENT→COLOR→dynamic rendering(LOAD)→callback(VkCommandBuffer как void*)→PRESENT.
- App: ImGui_ImplVulkan_Init с `UseDynamicRendering=true` + `PipelineRenderingCreateInfo`. **ImGui 1.91.5 требует свой descriptor pool** (auto-pool `DescriptorPoolSize` появился позже) — создаём в app. Per-frame: NewFrame → UI → после execute: `ImGui::Render()` + `recordOverlay(ImGui_ImplVulkan_RenderDrawData)`. Демо: FPS + слайдеры Sun/Point intensity.
- Мелочь: дубликат-линк glfw warning (линкуется напрямую + через imgui PUBLIC) — безобиден.

**Phase 7 (Shadows + IBL) — DONE ✅ (2026-07-26)**
- **Shadows** ✅ — shadow map (D32 2048², depth-only пайплайн: colorFormat=Undefined→0 color attachments), `renderShadowMap` рендерит глубину модели глазами света в frame cmd ПЕРЕД основным пассом (обход FG getTexture-заглушки), основной шейдер: light-space lookup + PCF 3×3 + bias. `lightVP` в scene UBO. Тень применяется только к солнцу (point+IBL не затеняются). RHI: createTexture depth-aspect по формату, transitionImageLayout + aspect param.
  - **Слабые тени = не баг:** IBL заливает затенённую зону (тень гасит только sun), + self-shadowing (нет пола). Усиление: sun-доминантный свет / ground plane / AO на ambient — тюнинг/доп-фичи.
- **Skybox** ✅ — equirect HDR (venice_sunset_4k.hdr, RGBA32F), fullscreen-triangle пасс, reconstruction луча через inverse(proj·view), рисуется в одном пассе перед моделью (FG всегда CLEAR → 2 пасса в один target нельзя, это Phase 6). depth-флаги пайплайна (depthTest/depthWrite) добавлены для skybox.
- **IBL** ✅ (split-sum) — три бэйка НА СТАРТЕ (не per-frame FG, env статична):
  - `renderToTexture` (Device) — bake fullscreen-шейдера в текстуру (+ mipLevel param, per-mip view). Инфра бэйка.
  - BRDF LUT (RG16F 512²), Irradiance (RGBA16F 64×32, свёртка), Prefiltered (RGBA16F 256×128, 6 мипов, GGX per-roughness).
  - triangle.frag: diffuse IBL (irradiance×albedo) + specular IBL (prefiltered×(F·brdf.x+brdf.y)), textureLod по roughness. Металл отражает окружение.
  - Всё 2D equirect (без кубмапов). VulkanImage: mipLevels + createMipView. toVkFormat += RG16/R16_SFLOAT.
  - **Урок:** GPU command buffer имеет лимит времени (Metal watchdog) — тяжёлый bake (много сэмплов из 4k env) его превышал → device lost. Огрубили сэмплинг (irradiance delta 0.05, prefilter 128 samples).
- **Осталось: Shadows** — shadow map pass + PCF. (Cascaded — если захочется.)

**НЕ сделано (осознанно):** RGBA16F для env (сейчас RGBA32F 134МБ — работает, но жирно); tone mapping (Phase 8) — яркий HDR может пересвечивать.

**Phase 6 (Frame Graph — Advanced) — после 7**
transient aliasing, pass culling, **async compute** (много очередей), multi-thread recording. + рефактор FG execute (color/depth special-case → табличный realize()).

**Frame Debugger — после Phase 6** (идея пользователя, отличная). Т.к. FG знает пассы по именам → auto-обёртка каждого в GPU timestamp queries (VkQueryPool) + **timeline-визуализация** (Gantt: пасс = полоска, позиция = старт на GPU, ширина = длительность; много дорожек с async compute). Окупается до RTX (Phase 10-11). Не «циферки», а полноценный GPU timeline profiler (уровень UE GPU Visualizer / RenderDoc timeline).

**Отложено осознанно (не хаки):**
- **Skybox → перенесён в Phase 7** — неотделим от IBL; HDR equirect грузится один раз для неба + IBL (irradiance/prefiltered). Отдельный skybox без IBL = чистая косметика.
- **Мульти-меш / мульти-материал модели** — loader берёт первый примитив; полноценно = descriptor-set-на-материал + draw-loop, либо bindless (Phase 9). Не блокер.
- **SkyGraphics::Mesh/Material/Camera** — RAII high-level слой; строим когда модель устаканится (rule of three).
- FG execute всё ещё special-case'ит Color/Depth attachment — обобщить при случае.
- FGResources::getTexture — stub; FG обрабатывает только writes (reads — когда понадобятся).

**Модель/куб должны продолжать работать после каждой фазы.**

**CMake/build нюансы (2026-07-25):**
- Deps через FetchContent; `FETCHCONTENT_UPDATES_DISCONNECTED ON` в root (иначе shallow-clone git-update ломается на reconfigure).
- SkyApp линкует `glm::glm` **явно** (транзитивно было хрупко).
- tinygltf header-only через `${tinygltf_SOURCE_DIR}` include; impl-defines в GltfModel.cpp.
- **НЕ смешивать** homebrew-cmake с CLion-ninja на одном build-dir → конфликт генераторов. CLion сам держит `cmake-build-debug` (Ninja).

## Ключевые файлы

**Публичный API (currently):**
- `include/skypch.h` — PCH
- `include/Core/Core.h` — `Sky::RHI::Ref<T>`, `Sky::RHI::Scope<T>`
- `include/Core/Log.h` — `Sky::RHI::Log`, макросы `SKY_RHI_*`

**Внутренний код (Phase R переезд):**
- `include/SkyRenderer/*.h` (10 файлов) — все переедут в `src/Vulkan/`

**App (не library):**
- `SkyApp/src/main.cpp` — demo, использует renderer
- `SkyApp/src/Window.h/cpp` — GLFW wrapper (app-side)
- `SkyApp/shaders/` — GLSL, компилятся glslc через CMake

## Стэк зависимостей

| Библиотека | Версия | Через | Комментарий |
|---|---|---|---|
| Vulkan SDK | 1.4.350.0 | `find_package` | ~/VulkanSDK/, env vars настроены |
| spdlog | v1.15.0 | FetchContent | **обязательно `SPDLOG_USE_STD_FORMAT ON`** — иначе Apple Clang ломается на fmt v11 consteval |
| VMA | v3.3.0 | FetchContent | В `src/Vulkan/VMAImplementation.cpp` — единственный TU с `#define VMA_IMPLEMENTATION` |
| GLFW | 3.4 | FetchContent | Только в SkyApp, не в SkyRenderer library |

## macOS quirks

- **VK_KHR_portability_enumeration** — обязательный instance extension
- **VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR** — обязательный instance flag
- **VK_KHR_portability_subset** — обязательный device extension (MoltenVK экспортирует)
- **VK_KHR_swapchain** — device extension (пропустил в начале, было `Driver's function pointer was NULL`)
- **MoltenVK не поддерживает hardware ray tracing** → RT на Mac через compute-based path tracer

## Как начать сессию

1. **Прочти этот файл**
2. **Прочти ROADMAP.md** — актуальные фазы и задачи
3. **Прочти memory files** — user preferences, teaching approach
4. Спроси user куда двигаться: продолжить текущую phase или ревью существующего кода
5. **Помни:** ТЫ УЧИТЕЛЬ. Не выдавай готовый код без ТЗ, если user не попросил явно "давай код".

## Обновления этого файла

Каждый раз когда:
- Завершается фаза → обновить "Current state"
- Меняются locked decisions → обновить соответствующую секцию
- User озвучивает предпочтения → добавить в teaching approach
- Обнаруживается новый quirk → добавить в quirks

Не боятся редактировать. Этот файл — living document.
