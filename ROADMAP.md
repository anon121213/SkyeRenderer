# SkyRenderer — Roadmap

> Живой документ. Обновляем по мере прогресса. Каждая фаза — законченный кусок работы с ясным результатом.

## Что это

**SkyRenderer** — standalone AAA-квалити low-level render framework на Vulkan. Отдельная git-репа, будет подключаться в SlyEngine как submodule. Позже добавим D3D12 backend.

## Ambition

Цель — сделать fully-fledged production render framework:
- AAA senior-уровень оптимизации
- Modern Vulkan 1.3 практики
- PBR + HDR + Path Tracing + RTX
- GPU-driven rendering, bindless
- Frame Graph с автоматическими барьерами
- Multi-window, multi-queue, multi-thread ready

Timeline (обновлено 2026-07-20 под реальный темп user'а — 14-40ч/нед): core (PBR → path tracer, Phase 4-11) ~1.5-2 мес @40ч/нед; полный scope (+Lumen +Nanite +DX12/DLSS) ~4-7 мес.

---

## Locked architectural decisions

Эти решения зафиксированы. Не пересматриваем без веских причин.

### Namespace conventions

```
Sky::RHI::                       ← весь low-level render API
  ├── Device, Buffer, Texture, Shader, Pipeline, CommandList, Swapchain
  ├── FrameGraph, Queue, Sync (Semaphore, Fence, TimelineSemaphore)
  ├── AccelerationStructure (RT)
  ├── Log::init(), Log::get()
  └── Ref<T>, Scope<T>, CreateRef, CreateScope

Sky::Graphics::                  ← optional high-level facade
  └── Mesh, Material, MeshRenderer, Camera, Scene

Sky::                            ← оставлен для consumer (SlyEngine)
```

**Правило:** ничего renderer-specific напрямую под `Sky::X` — только в подnamespace `Sky::RHI::` или `Sky::Graphics::`. Consumer engine свободно использует `Sky::` root.

### Macro conventions

```
SKY_RHI_TRACE / INFO / WARN / ERROR / CRITICAL     ← наш логгер
SKY_RHI_VK_CHECK                                    ← наш VkResult check

SKY_INFO / ERROR / etc.                             ← свободно для SlyEngine
```

### Two-tier API design

```
┌─ Sky::Graphics ────────────────────────────────┐   (высокоуровневый, opt-in)
│  Mesh, Material, MeshRenderer::submit(...)      │
└─────────────────↓───────────────────────────────┘
                  |
┌─ Sky::RHI ─────────────────────────────────────┐   (низкоуровневый, mandatory)
│  Device, Buffer, Texture, Pipeline, CommandList │
│  Swapchain, FrameGraph, Queue                   │
└─────────────────────────────────────────────────┘
```

Consumer выбирает уровень. ECS системы работают с RHI (handles). Простые случаи через Graphics (RAII).

### Handles vs Objects

- **RHI слой — handle-based** (для ECS-DOTS совместимости, серилизации, backend flexibility)
- **Graphics слой — RAII objects** (для удобства)

```cpp
// RHI — POD handles, идеальны для компонентов ECS
Sky::RHI::BufferHandle vb = device.createBuffer(desc);   // uint64_t под капотом

// Graphics — RAII wrapper над handles
Sky::Graphics::Mesh mesh(device, vertices, indices);
```

### Threading

**Дизайним API multi-thread ready с первого дня, реализуем single-thread first**:

- Thread-safe: `Device.createBuffer/Texture/Shader/Pipeline` (mutex внутри)
- Per-thread: `Device.createCommandList(threadIndex)` (свой pool на поток)
- Single-thread: `Device.submit()`, `present()` — только main thread

Реальный multi-thread recording приходит **не когда захотим, а через Frame Graph** — FG сам распараллелит независимые passes.

### Multi-window pattern

```
Один Device (VkDevice + VmaAllocator + Queues)
   ↓
Много Swapchain (по одному на окно/монитор)
   ↓
Общие ресурсы (buffers, textures) шарятся
```

Не «много Renderer». Один Device — много SwapchainHandle.

### Backend abstraction — postponed

Пока backend один (Vulkan), полиморфизм / PIMPL НЕ вводим. Публичный API остаётся backend-agnostic по семантике (никаких `VkFoo` в сигнатурах), реализация — прямо через Vulkan без промежуточных слоёв. Когда добавим D3D12 — introduce polymorphism inside Device.

### Ray tracing, upscaling & hardware targets (added 2026-07-20)

- **RT — compute/software-based (primary path).** Mac (MoltenVK) не даёт hardware RT в Vulkan (M3 GPU имеет HW RT, но только через Metal — MoltenVK не пробрасывает Vulkan RT extensions). Path tracer (Phase 11) и Lumen-style GI (Phase 12) реализуем через **compute** (свой BVH traversal / SDF tracing). Работает на любом GPU.
- **Hardware RT — опционально, через будущий RTX-ноут.** Планируется купить дешёвый RTX laptop (40-серия ради DLSS 3 Frame Generation) под Windows/DX12 ветку. Тогда тот же path tracer/Lumen ускоряется через `vkCmdTraceRaysKHR`/DXR. API проектируем так, чтобы HW-путь подключался без переписывания.
- **Upscaling: FSR (primary, cross-vendor, compute — идёт на Mac) + DLSS (на RTX-железе).** DLSS = только NVIDIA RTX (нет ни на Mac, ни на текущем Intel-ноуте Ultra 7 155H / iGPU). DLSS 2 — любые RTX; DLSS 3 Frame Gen — только 40-серия; DLSS 3.5 Ray Reconstruction — все RTX. XeSS — cross-vendor опция.
- **Dev-машина для RT/GI — Mac M3 Max** (мощный GPU, compute отлично идёт). Windows-ноут (Intel Ultra 7 155H, iGPU) — только smoke-test DX12 backend, не для тяжёлого GI (будет слайдшоу).

---

## Целевая структура репы

```
SkyRenderer/                                ← standalone framework, git submodule
├── CMakeLists.txt                          ← options: SKY_BACKEND_VULKAN, SKY_BACKEND_D3D12
├── ROADMAP.md                              ← этот документ
│
├── include/
│   ├── skypch.h                            ← PCH
│   ├── Core/
│   │   ├── Core.h                          ← Ref, Scope
│   │   └── Log.h
│   ├── SkyRHI/                             ← ПУБЛИЧНЫЙ RHI API
│   │   ├── Device.h                        ← точка входа
│   │   ├── Buffer.h, Texture.h, Shader.h
│   │   ├── Pipeline.h, CommandList.h
│   │   ├── Queue.h, Sync.h
│   │   ├── Swapchain.h
│   │   ├── FrameGraph.h                    ← сердце архитектуры
│   │   ├── AccelerationStructure.h         ← RT
│   │   └── Types.h                         ← enums (Format, Usage, Layout)
│   └── SkyGraphics/                        ← ПУБЛИЧНЫЙ high-level API
│       ├── Mesh.h, Material.h
│       ├── Camera.h, Scene.h
│       └── MeshRenderer.h
│
├── src/
│   ├── Common/                             ← platform-agnostic
│   │   ├── FrameGraphCompiler.cpp          ← DAG анализ
│   │   ├── FrameGraphScheduler.cpp         ← распараллеливание passes
│   │   ├── TransientAllocator.cpp          ← memory aliasing
│   │   ├── HandleAllocator.cpp             ← lock-free handle pool
│   │   └── Log.cpp
│   ├── Vulkan/                             ← Vulkan backend (internal)
│   │   ├── VulkanDevice.cpp                ← реализует SkyRHI::Device
│   │   ├── VulkanBuffer.cpp, VulkanTexture.cpp
│   │   ├── VulkanShader.cpp, VulkanPipeline.cpp
│   │   ├── VulkanCommandList.cpp
│   │   ├── VulkanSwapchain.cpp
│   │   ├── VulkanFrameGraphBackend.cpp     ← bindings FG для Vulkan
│   │   ├── VulkanInstance.cpp              ← internal helper
│   │   ├── VulkanPhysicalDevice.cpp        ← internal helper
│   │   ├── VulkanSync.cpp                  ← semaphores, fences, timelines
│   │   ├── VulkanAccelStructure.cpp        ← RT (future)
│   │   └── VMAImplementation.cpp
│   └── D3D12/                              ← пусто пока
│
├── SkyApp/                                 ← demo/playground
│   ├── shaders/
│   └── src/
│
├── tests/                                  ← unit + integration (потом)
├── benchmarks/                             ← perf tests (потом)
└── docs/                                   ← спека API, гайды
```

---

## Phase timeline

### ✅ Phase 0: Bootstrap (COMPLETE)

**Что сделано:**
- CMake структура, зависимости (Vulkan SDK, GLFW, VMA, spdlog)
- PCH, namespace scheme, макросы
- VulkanInstance/Device/Surface/Swapchain/RenderPass/Framebuffers/ShaderModule/Pipeline/CommandPool/Renderer
- Треугольник на экране
- Logger (`Sky::RHI::Log`, `SkyRHI` sink)

**Результат:** треугольник рендерится, инфраструктура готова к рефактору.

---

### 🚧 Phase R: RHI Abstraction Refactor (СЛЕДУЮЩАЯ)

**Оценка:** 2-3 сессии (2-3 часа каждая)

**Цель:** Публичный API становится backend-agnostic. `VulkanX` переезжают в `src/Vulkan/` internal. Треугольник продолжает работать через новый API.

**Задачи:**
- [ ] Создать `include/SkyRHI/Types.h` — enums (BackendType, Format, Usage, MemoryType, Layout, ...)
- [ ] Создать `include/SkyRHI/Handle.h` — `BufferHandle`, `TextureHandle`, etc. (POD wrappers over uint64_t)
- [ ] Создать `include/SkyRHI/Device.h` — публичная точка входа
- [ ] Создать `include/SkyRHI/CommandList.h` — recording API
- [ ] Создать `include/SkyRHI/Swapchain.h` — публичный (handle-based)
- [ ] Создать `include/SkyRHI/FrameGraph.h` — заглушка (интерфейс, реализация в Phase 1)
- [ ] Создать `src/Common/HandleAllocator.cpp` — thread-safe handle pool
- [ ] Перенести все `include/SkyRenderer/VulkanX.h` в `src/Vulkan/VulkanX.h` (internal)
- [ ] Реализовать `SkyRHI::Device` через существующие VulkanX классы
- [ ] Реализовать `SkyRHI::CommandList` через `VkCommandBuffer` recording
- [ ] Update `main.cpp` — использовать только `Sky::RHI::Device`, никаких VulkanX
- [ ] Треугольник продолжает работать

**Результат:** Public API — 5-6 headers в `include/SkyRHI/`. Consumer engine взаимодействует только с ним, никаких Vulkan-типов в сигнатурах.

---

### Phase 1: Frame Graph MVP

**Оценка:** 2-3 недели (~10-15 сессий)

**Цель:** Frame Graph работает с single-threaded execution. Треугольник рендерится через FG.

**Задачи:**
- [ ] `FrameGraph::addRasterPass`, `addComputePass`, `addCopyPass`
- [ ] `Builder` API — `readTexture`, `writeTexture`, `createTexture` (transient)
- [ ] DAG анализ — топологическая сортировка passes
- [ ] Автоматические `vkCmdPipelineBarrier` между passes
- [ ] Layout transitions расставляются автоматом
- [ ] `importSwapchainImage` — для внешних ресурсов
- [ ] Треугольник переезжает в один `RasterPass` в FG
- [ ] Тесты корректности FG

**Что НЕ входит в MVP (потом):**
- Memory aliasing (transient allocator)
- Pass reordering для параллелизма
- Async compute scheduling
- Multi-thread recording

**Результат:** Основа для всех дальнейших фаз — FG работает, треугольник через FG.

---

### Phase 2: Buffers & Cube

**Оценка:** 1-2 недели (~5-8 сессий)

**Цель:** Vertex/Index/Uniform buffers работают. Куб в 3D с MVP матрицами.

**Задачи:**
- [ ] `SkyRHI::Buffer` с усlugами `Vertex`, `Index`, `Uniform`, `Storage`
- [ ] `SkyRHI::Device::createBuffer(BufferDesc)` через VMA
- [ ] `SkyRHI::Device::mapBuffer/unmapBuffer` для host-visible
- [ ] `SkyRHI::Device::uploadBufferData` через staging (async transfer queue пока не нужна)
- [ ] `CommandList::bindVertexBuffer`, `bindIndexBuffer`, `drawIndexed`
- [ ] `PipelineDesc.vertexLayout` — vertex input state в pipeline
- [ ] Push constants в pipeline layout
- [ ] Треугольник из vertex buffer (без hardcoded)
- [ ] Depth attachment (transient texture через FG)
- [ ] Куб с MVP матрицами через push constants
- [ ] `SkyGraphics::Camera` — простой view/projection

**Результат:** Крутящийся куб в 3D. Vertex/index buffer API готов.

---

### Phase 3: Textures & Materials

**Оценка:** 2-3 недели

**Цель:** Текстурированный куб. Начало Material system.

**Задачи:**
- [ ] `SkyRHI::Texture` — все типы (2D, 3D, Array, Cubemap)
- [ ] `SkyRHI::Sampler` — filter modes, wrap modes, anisotropy
- [ ] `Device::uploadTextureData` через staging
- [ ] Descriptor set management (`DescriptorSetLayout`, `DescriptorSet`)
- [ ] `Bindless preparation` — descriptor arrays готовы в API, но не активны
- [ ] `Texture loading` через stb_image (в SkyApp, не в library)
- [ ] Текстурированный куб
- [ ] `SkyGraphics::Material` — pipeline + texture bindings

**Результат:** PBR текстуры видны, materials system работает.

---

### Phase 4: PBR Shading

**Оценка:** 2-3 недели

**Цель:** Физически корректное освещение — Cook-Torrance, GGX, Fresnel.

**Задачи:**
- [ ] Vertex format с normal + tangent + UV
- [ ] Normal mapping (tangent space)
- [ ] Metallic-Roughness workflow
- [ ] GGX specular BRDF
- [ ] Fresnel-Schlick approximation
- [ ] Direct lighting (directional + point light в UBO)
- [ ] Простой skybox (cubemap sampling)
- [ ] Демо-сцена: несколько материалов, разные roughness/metallic

**Результат:** Реалистичный shading на кубе / модели.

---

### Phase 5: Async Transfer & Streaming

**Оценка:** 1-2 недели

**Цель:** Загрузка ресурсов в фоне без freeze основного рендера.

**Задачи:**
- [ ] Async transfer queue support в `Device`
- [ ] Timeline semaphores для sync между transfer и graphics
- [ ] `Device::uploadTextureDataAsync` — не блокирует main thread
- [ ] Streaming manager — очередь ассетов, priority-based
- [ ] Демо: подгрузка новых текстур пока пользователь ходит

**Результат:** Плавный стриминг без hitches.

---

### Phase 6: Frame Graph — Advanced

**Оценка:** 2-3 недели

**Цель:** FG становится production-grade.

**Задачи:**
- [ ] Transient resource allocator — memory aliasing между passes
- [ ] Pass culling — dead code elimination если output не используется
- [ ] Async compute pass scheduling — параллельное исполнение compute passes с graphics
- [ ] Multi-thread pass recording — FG сам распараллеливает
- [ ] Cross-frame resource tracking — persistent resources vs transient
- [ ] Statistics/debug view — визуализация DAG в ImGui

**Результат:** FG уровня Frostbite/Granite. Основа для сложных пайплайнов.

---

### Phase 7: Shadow Mapping + IBL

**Оценка:** 2 недели

**Цель:** Полноценные тени + image-based lighting.

**Задачи:**
- [ ] Cascaded shadow maps (3-4 cascades)
- [ ] Shadow pass в FG (пишет в shadow atlas)
- [ ] Poisson disk / PCF filtering в fragment shader
- [ ] Cubemap loading (HDR skybox)
- [ ] Irradiance map generation (compute)
- [ ] Prefiltered specular map generation (compute)
- [ ] BRDF LUT
- [ ] Полноценное IBL освещение в shader

**Результат:** Реалистичное окружение + мягкие тени.

---

### Phase 8: HDR + Post-Processing

**Оценка:** 2 недели

**Цель:** HDR-пайплайн с tone mapping, bloom, TAA.

**Задачи:**
- [ ] HDR intermediate targets (`R16G16B16A16_SFLOAT`)
- [ ] Tone mapping (ACES, Reinhard, filmic — configurable)
- [ ] Bloom (compute-based downsample + upsample chain)
- [ ] TAA (temporal anti-aliasing) с motion vectors
- [ ] Exposure compensation (auto-exposure через histogram)
- [ ] Vignette, color grading

**Результат:** Cinematic look, современный визуал.

---

### Phase 9: GPU-Driven Rendering

**Оценка:** 3-4 недели

**Цель:** Perceived AAA-scale — миллионы объектов, минимум CPU.

**Задачи:**
- [ ] Bindless resources активны — descriptor arrays повсюду
- [ ] `VK_EXT_descriptor_indexing` full support
- [ ] Indirect draw buffers
- [ ] Compute-based frustum culling
- [ ] Compute-based occlusion culling (HiZ)
- [ ] LOD selection на GPU
- [ ] MultiDrawIndirect для батчинга
- [ ] Демо-сцена: 100k+ объектов

**Результат:** CPU почти не работает, GPU-driven pipeline.

---

### Phase 10: Ray Tracing Infrastructure

**Оценка:** 2-3 недели

**Цель:** Готовность к RT — BLAS, TLAS, RT pipeline.

**Задачи:**
- [ ] `SkyRHI::AccelerationStructure` — BLAS, TLAS
- [ ] `SkyRHI::RayTracingPipeline`
- [ ] Shader Binding Table (SBT)
- [ ] `CommandList::traceRays`
- [ ] RT extensions detection и graceful fallback
- [ ] Простой RT hello world — трейс лучей в компьют shader как fallback для Mac (MoltenVK не поддерживает hardware RT)

**Результат:** RT infrastructure готова. На Mac — software raytracing через compute. На Windows/Linux с RTX GPU — hardware RT.

---

### Phase 11: Path Tracer

**Оценка:** 3-4 недели

**Цель:** Progressive path tracer с proper light transport.

**Задачи:**
- [ ] Camera ray generation
- [ ] BVH traversal (hardware если есть, иначе compute)
- [ ] Material evaluation (diffuse, specular, transmissive)
- [ ] Importance sampling
- [ ] Multiple importance sampling (MIS)
- [ ] Russian roulette termination
- [ ] Denoiser (простой A-Trous wavelet, потом можно OIDN если доступен)
- [ ] Progressive accumulation

**Результат:** Photo-realistic path traced scene. Достижение AAA-квалити рендерера.

---

### Phase 12: Lumen-style Real-Time GI

**Оценка:** ~40-70ч

**Цель:** динамическая global illumination в реалтайме **без hardware RT**, сверяемая с path tracer'ом (Phase 11) как ground-truth эталоном.

**Задачи:**
- [ ] Signed Distance Fields генерация — per-mesh SDF (compute bake)
- [ ] Global merged distance field сцены
- [ ] Software ray tracing против SDF (compute) — diffuse GI rays, без треугольного BVH
- [ ] Screen-space traces (по depth/normal) для near field
- [ ] Surface cache / radiance caching — переиспользование освещённости между кадрами
- [ ] Temporal accumulation + denoising
- [ ] Reflections через те же traces
- [ ] Сравнение с path tracer ground truth (валидация качества)

**Результат:** реалтайм динамическая GI — двигаешь свет/объекты, освещение и отражения обновляются. Lumen-style, на compute.

---

### Phase 13: Nanite-style Virtualized Geometry

**Оценка:** ~60-120ч (по сути отдельный проект — держим последним)

**Цель:** виртуализованная геометрия — миллионы/миллиарды треугольников, авто-LOD без швов и попапа.

**Задачи:**
- [ ] Meshlet building — разбиение меша на кластеры ~128 треугольников
- [ ] Cluster quantization / compression — позиции квантованы относительно bounds кластера
- [ ] **Hierarchical LOD DAG** — группировка кластеров, упрощение через границы групп, seamless LOD (ядро Nanite, самое сложное)
- [ ] GPU cluster culling (frustum + occlusion HiZ) — переиспользует Phase 9
- [ ] **Software rasterizer** (compute) для микро-треугольников → visibility buffer (64-bit: depth + cluster/tri ID)
- [ ] Hardware rasterizer для крупных кластеров (гибридный путь)
- [ ] Material pass из visibility buffer (deferred-style shading)
- [ ] Streaming кластеров (подгрузка по нужному LOD)

**Результат:** Nanite-style — детализированные модели без LOD-попапа, полигонаж почти не влияет на перф.

---

## Windows / DX12 ветка (требует RTX-железа)

Планируется дешёвый RTX-ноут (40-серия ради DLSS 3). Делается параллельно/после core. RHI уже backend-agnostic — DX12 это второй backend, не рендерер с нуля. Заодно **«экзамен на самостоятельность»** для user'а (пишет с минимумом ведения).

- [ ] **DX12 backend** — второй backend к существующему RHI API
- [ ] **DLSS** — NVIDIA upscaling (только RTX; 40-серия для Frame Generation)
- [ ] **Hardware RT path** — `vkCmdTraceRaysKHR` / DXR для ускорения path tracer + Lumen на RTX
- [ ] **Full mesh shaders** — на MoltenVK частично (Metal 3), полноценно на Windows/DX12
- [ ] **Descriptor buffer** (`VK_EXT_descriptor_buffer`) — не на MoltenVK, только Windows/DX12

---

## Отложено (когда захочется)

- **FSR 2/3** — cross-vendor upscaler (compute, идёт на Mac); логично после Phase 8 (нужны motion vectors)
- **XeSS** — Intel cross-vendor upscaler (альтернатива/дополнение FSR)
- **VK_KHR_maintenance5** — modern conveniences

---

## Не делаем (out of scope)

- **Audio** — отдельный модуль движка
- **Physics** — отдельная либа (BulletPhysics/JoltPhysics/Rapier)
- **UI** — dear ImGui для debug, custom UI в engine
- **Асинхронный ассет loader** — интерфейс да, реализация в engine
- **Serialization** — engine responsibility

---

## Работа по фазам

**Правила:**
1. Одна фаза не начинается пока не закончена предыдущая (не смешиваем задачи)
2. Треугольник должен работать после каждой фазы (не ломать существующее)
3. Каждая фаза заканчивается **демо** — визуальное подтверждение
4. Если фаза уходит в перебор (>2x оценки) — пауза, анализ, разбитие
5. Тесты — с Phase 3 (когда несколько resource types)
6. Benchmark'и — с Phase 6 (FG performance важна)

## Открытые вопросы

Не забыть обсудить когда придёт время:

- **Shader system**: как компилим шейдеры? glslc в CMake — простой путь. Runtime compilation через shaderc/glslang — сложнее но динамично. Hot-reload шейдеров?
- **Resource lifetime в FG**: когда именно уничтожаются transient textures? После frame или в конце passes?
- **Debug tools**: RenderDoc labels? Nsight Graphics markers? Frame stats overlay в ImGui?
- **Testing strategy**: как тестировать GPU код? Golden image comparison? Reflection-based validation?

---

## Метрики успеха

Renderer считается **production-quality AAA** когда:

- [ ] Path tracer работает и производит convergent images
- [ ] PBR + IBL полноценный
- [ ] Shadow maps + soft shadows
- [ ] GPU-driven rendering — >100k objects at 60fps
- [ ] Frame Graph с memory aliasing + async compute
- [ ] Bindless resources повсеместно
- [ ] Multi-window / multi-swapchain
- [ ] Thread-safe API + FG-based MT recording
- [ ] Vulkan validation clean (no warnings)
- [ ] Cross-platform (Win/Mac/Linux) билдится
- [ ] Portfolio ready — можно показывать на собеседовании senior render programmer

---

## Обновления

**2026-06-08** — Phase 0 complete. Namespace/macros locked. Ready for Phase R.
