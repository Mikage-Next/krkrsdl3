#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "PlatformView.h"
#include "LayerRenderOperation.h"
#include "LayerTransition.h"
#include "LayerShrink.h"
#include "LayerSpanComposite.h"
#include "AsyncLayerReadback.h"
#include "LayerUploadBatch.h"
#include "LayerHotspotContext.h"

//---------------------------------------------------------------------------
// TVPCompositor
//---------------------------------------------------------------------------
// 通用合成器，用来管理所有非Layer/Bitmap系列的渲染，包括上屏Windows/Surface、emote-GPU加速
//---------------------------------------------------------------------------
namespace krkrsdl3
{
struct TVPLayerGlyphUploadInfo {
    uint64_t textureID=0, bytes=0;
    int width=0,height=0;
    const char* reason="unsupported";
    bool targetWritten=false; // also observable if a post-encoding Submit throws
};
// GPU mesh-deformation input: one entry per surface of an Emote node chain,
// innermost last. The vertex shader evaluates the same chain as the CPU path
// (emoteSurfaceEvaluator). Matrices are column-major. Keeping this
// representation renderer-neutral lets plugins use an accelerated path without
// exposing their own types here.
struct TVPMeshDeformSurface
{
    float attach[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float originX = 0, originY = 0, width = 0, height = 0;
    // attach * (coord, 0, 1); the w component is always 0 in the chain.
    float positionX = 0, positionY = 0, positionZ = 0, reserved0 = 0;
    float angle = 0, zoomX = 1, zoomY = 1, slantX = 0;
    float slantY = 0, offsetX = 0, offsetY = 0, reserved1 = 0;
    int32_t type = 0;
    uint32_t inheritMask = 0;
    uint32_t transformOrder = 0x03020100; // four operation bytes, first in the low byte
    uint32_t reserved2 = 0;
    float controlPts[32] = {0};
};
static_assert(sizeof(TVPMeshDeformSurface) == 272, "GPU deformation surface layout");

//---------------------------------------------------------------------------
// 渲染后端抽象层（合并接口）
//
// 目标：让"窗口贴图合成"这一层不再依赖具体图形 API。
// 当前实现的后端：
//   - "software" : SDL 软渲染（各平台入口通过 PlatformView.h 的平台函数提供）
//   - "opengl"   : OpenGL 3.3 Core / OpenGL ES 3.0（桌面 glad / 移动 GLES3）
//   - "vulkan"   : Vulkan 1.0（SDL3 窗口表面，位于 environ/sdl3/render/）
//   - "metal"    : 原生 Metal（Apple SDL3 窗口与离屏渲染）
//
// 每个后端只有一个实现类，同时承担两个角色（合并设计）：
//   - 窗口贴图合成（上屏）：BeginFrame/EndFrame + *WindowTexture 方法
//   - 2D 网格渲染（供 emoteplayer 等插件）：离屏目标 + 一般贴图 + DrawMesh
//
// 贴图区分（接口上显式区分两类贴图）：
//   - 窗口贴图（CreateWindowTexture 等）：上屏合成用。
//     SW=SDL_Texture（经平台钩子）；GPU=与一般贴图同一实现。
//   - 一般贴图（CreateTexture 等）：离屏网格绘制用。
//     SW=CPU buffer；GPU=与窗口贴图同一实现。
//   对 GPU 后端而言两类贴图就是同一个函数；对软件后端实现不同，
//   因此接口上仍保留两套方法名。
//
// 设计要点（为 Metal 预留）：
//   1. 贴图句柄为不透明 void* —— GL 存内部贴图对象，Vulkan 存 VulkanTexture*，
//      Metal 可存 id<MTLTexture> 的堆包装对象，无需改接口。
//   2. 贴图更新以"整张 CPU 像素 + pitch"为单位 —— Metal 的 staging buffer
//      上传路径可直接映射到该接口。
//   3. 帧控制只有 BeginFrame/EndFrame —— Metal 的 CAMetalLayer drawable
//      获取与 present 封装在 EndFrame 内，对上层不可见。
//   4. 绘制以"目标矩形像素坐标"为单位，letterbox 计算由合成器统一完成，
//      所有后端行为一致。
//---------------------------------------------------------------------------
class iTVPRenderBackend
{
public:
    virtual ~iTVPRenderBackend() = default;

    // 后端标识："software" / "opengl" / "vulkan" / "metal"
    virtual const char* GetName() const = 0;
    // 是否硬件加速（用于日志与信息展示）
    virtual bool IsHardware() const { return false; }

    // ---- 帧控制（由 TVPRenderOnce 统一调度）----
    virtual void BeginFrame(int winWidth, int winHeight) = 0;
    virtual void EndFrame() = 0; // 提交并呈现（SwapBuffers / Present / vkQueuePresent）
    // Lifecycle invalidation is optional; conservative static-present reuse
    // must refresh after expose, resize, resume, capture and session changes.
    virtual void InvalidatePresentation(const char* reason) {}

    // ---- 窗口贴图管理（上屏合成用；SW=SDL_Texture，GPU=与一般贴图同一实现）----
    // 句柄为后端持有的不透明指针，nullptr 表示无效
    virtual void* CreateWindowTexture(int width, int height) = 0;
    virtual void UpdateWindowTexture(void* handle, const uint8_t* buff, int width, int height, int pitch) = 0;
    virtual void DestroyWindowTexture(void* handle) = 0;
    // 以像素坐标绘制窗口贴图（位置与尺寸已由合成器计算，含 letterbox）
    virtual void DrawWindowTexture(void* handle, float posX, float posY, float width, float height) = 0;

    // ---- 2D 网格渲染（离屏目标 + 一般贴图 + 蒙版/混合 + DrawMesh）----
    // 渲染目标（离屏）：GL= FBO，软渲染= CPU 缓冲，Vulkan= 离屏图像
    virtual void* CreateTarget(int width, int height) = 0;
    virtual void DestroyTarget(void* target) = 0;
    // 绑定当前绘制目标（内部完成视口/深度/混合等状态设置）
    virtual void SetTarget(void* target) = 0;
    // 清屏：clearColor=true 清颜色+深度；false 仅清深度（软件后端无深度，此时不清屏）
    virtual void ClearTarget(bool clearColor) = 0;
    // 回读 CPU 像素（调用期间像素有效，Unlock 后失效）
    virtual uint8_t* LockTarget(void* target, int& pitch) = 0;
    virtual void UnlockTarget(void* target) = 0;
    // 目标作为可采样纹理使用（GPU 纹理可直接作为绘制目标；返回的句柄
    // 可传给 DrawMesh 的 texture 参数与 SetMask）。GL 返回 FBO 颜色纹理，
    // Vulkan 返回目标自身（其图像视图），软渲染返回目标自身（其缓冲）。
    virtual void* GetTargetTexture(void* target) = 0;
    // 直接更新目标内容（整幅上传；用于 GPU 纹理的像素上传）。
    // GL 走 glTexSubImage2D，Vulkan 走 staging buffer + copy，
    // 软渲染直接写缓冲。调用后目标内容立即可作为采样/回读源。
    virtual void UpdateTargetTexture(void* target,
                                     const uint8_t* pixels,
                                     int width,
                                     int height,
                                     int pitch) = 0;

    // 一般贴图（SW=CPU buffer；GPU=与窗口贴图同一实现）
    virtual void* CreateTexture(int width, int height) = 0;
    virtual void UpdateTexture(void* texture, const uint8_t* pixels, int width, int height, int pitch) = 0;
    virtual void DestroyTexture(void* texture) = 0;
    // 上传契约（可选优化，带默认实现）：把 pixels 的 (x,y,w,h) 区域"入队"到 texture。
    // 语义与 UpdateTexture 一致：返回后数据对本帧后续绘制可见，但不保证 GPU 已完成，
    // 调用方可立即复用/释放 pixels 缓冲。后端须把同步内部消化（§3 上传契约）。
    // 默认实现忽略 x/y 走整幅 UpdateTexture —— GL/SW 行为与性能完全不变；
    // 支持子矩形的后端（Vulkan）覆盖此方法，把上传攒进暂存环、帧末一次性提交。
    virtual void UploadTextureRect(void* texture, int x, int y, int width, int height,
                                   const uint8_t* pixels, int pitch)
    {
        (void)x;
        (void)y;
        UpdateTexture(texture, pixels, width, height, pitch);
    }
    // 读取贴图像素（SW=CPU 缓冲零拷贝；GPU 后端回读后返回同一格式）。
    // 约定：RGBA 字节序、自上而下行序，返回值在下次上传/回读前有效；nullptr 表示不支持。
    // 测试宿主截屏即经此取帧（见 tests/autotest.md）。
    virtual uint8_t* LockTexture(void* texture, int& pitch) { return nullptr; }

    // ---- 统一绘制状态与原语 ----
    // 蒙版：绑定蒙版目标（alpha >= 128 为有效像素）；nullptr 关闭蒙版
    virtual void SetMask(void* maskTarget) = 0;

    // 一套混合枚举容纳两套语义（数值区间分离，因为 0/1/3/4/6 在两边含义不同）。
    // 后端按区间选各自管线/shader，保证 emote 与 RenderManager(bm*) 的公式都逐像素保持。
    enum BlendMode
    {
        // emoteplayer 约定
        BM_NORMAL = 0,    // 普通 alpha（alpha 通道走 MAX 方程）
        BM_MULTIPLY = 1,  // 乘色（1 与 4 等价）
        BM_ADD = 3,       // 加色
        BM_SKIP = 6,      // 跳过绘制
        BM_SOLID = 21,    // 纯色替换，uniformColor 有效

        // 软件 RenderManager bm* 约定（从 0x100 起避免与 emote 数值冲突）
        BM_COPY = 0x100,
        BM_ALPHA = 0x101,
        BM_CONSTALPHA = 0x102,
        BM_ADD_RM = 0x103,
        BM_SUB = 0x104,
        BM_MUL_RM = 0x105,
        BM_MUL_HDA = 0x106,
        BM_FILL = 0x107,
        BM_COPYCOLOR = 0x108,
        BM_COPYOPAQUE = 0x109,
        BM_COPYMASK = 0x10A,
    };
    static bool IsRMBlend(int mode) { return mode >= BM_COPY; }

    // Mesh blends for DrawDeviceD3D's transformed pictures (DrawMesh with
    // destination alpha held), between the emote and RenderManager ranges.
    // Backends without them draw the ordinary alpha blend, as for any unknown
    // emote mode.
    enum MeshLayerBlendMode
    {
        MBM_LAYER_COPY = 100, // dest.RGB = src.RGB, constant opacity
        MBM_LAYER_ADD,        // dest.RGB = sat(dest + src * opa)
        MBM_LAYER_SUB,        // dest.RGB = sat(dest - (1 - src) * opa)
        MBM_LAYER_MUL,        // dest.RGB = dest * (1 - (1 - src) * opa)
        MBM_LAYER_ADDALPHA,   // premultiplied: dest.RGB = dest * (1 - src.a * opa) + src * opa
        MBM_LAYER_END
    };

    // 统一混合状态：emote 与 bm* 两套语义共用此入口。
    // opacity: 0..1（emote 走 DrawMesh/DrawRect 的 opacity；bm* 路径后端换算成 0..255）。
    // uniformColor: BM_SOLID / BM_FILL 使用，可为 nullptr；colorModulation: 采样后乘色。
    virtual void SetBlend(int mode,
                          float opacity = 1.0f,
                          const float* uniformColor = nullptr,
                          const float* colorModulation = nullptr) = 0;

    // 任意网格：vertices 交错 (x, y, u, v)，每顶点 4 个 float（NDC + UV）。
    // emote PSB 的中性 frame.color 是 0xff808080，调用方应传 identity，不要把 128/255 当染色。
    virtual void DrawMesh(const float* vertices,
                          int vertexCount,
                          const uint16_t* indices,
                          int indexCount,
                          void* texture,
                          float opacity,
                          const float* colorModulation = nullptr) = 0;

    // Optional regular-grid deformation path. divX/divY define a static UV grid;
    // the backend evaluates the surface chain per vertex. False means callers
    // must use the CPU-generated DrawMesh path.
    virtual bool SupportsMeshDeformation() const { return false; }
    virtual bool DrawDeformedMesh(int divX,
                                  int divY,
                                  const TVPMeshDeformSurface* surfaces,
                                  int surfaceCount,
                                  void* texture,
                                  float opacity,
                                  const float* colorModulation = nullptr)
    {
        return false;
    }

    // 轴对齐矩形：在当前目标上绘制源贴图的 (uv0..uv1) 区域到像素矩形 (x,y,w,h)。
    // 与 DrawMesh 是同级原语（不是"另一个渲染器"）：矩形可由后端走更省的管线。
    // 坐标约定：内容 y 向下，内容顶(t=0) 映射到 NDC -1（三后端回读一致）。
    virtual void DrawRect(void* texture,
                          float x,
                          float y,
                          float w,
                          float h,
                          float u0 = 0.0f,
                          float v0 = 0.0f,
                          float u1 = 1.0f,
                          float v1 = 1.0f) = 0;

    // 批绘制（现代原语）：同一 texture、当前 SetBlend 状态下，一次提交 count 个
    // 四边形。vertices 为 count*4 个交错 (x,y,u,v)（NDC + UV），indices 为空时
    // 用默认 (0,1,2,2,3,0) 逐 quad 展开。语义 = 同一批 texture/blend 的多个
    // DrawMesh 合并，避免每 sprite 一次 descriptor/pipeline bind（VK 热路径）。
    // 默认实现逐 quad 回退到 DrawMesh —— GL/SW 行为不变；VK 覆盖为单次 draw。
    virtual void DrawQuads(void* texture,
                           const float* vertices,
                           int quadCount,
                           const uint16_t* indices = nullptr,
                           int indexCount = 0);

    // 后端信息采集（GL 的厂商/版本/扩展日志等），无操作默认实现
    virtual void FetchInfo() {}

    // Optional ordinary-Layer resources. Rectangles use integer pixels, not
    // normalized Emote UVs. Unsupported backends keep software RenderManager.
    virtual bool SupportsLayerOperations() const { return false; }
    virtual bool SupportsLayerTransitions() const { return false; }
    virtual bool OperateLayerTransition(const TVPLayerTransitionOperation&, void*, void*, void*) { return false; }
    virtual TVPLayerTransitionResult LastLayerTransitionResult() const { return TVPLayerTransitionResult::PipelineUnavailable; }
    virtual bool SupportsLayerShrinks() const { return false; }
    virtual bool SupportsLayerShrink64() const { return false; }
    virtual bool OperateLayerShrink(const TVPLayerShrinkOperation&, void*, void*) { return false; }
    virtual TVPLayerShrinkResult LastLayerShrinkResult() const { return TVPLayerShrinkResult::BackendFailure; }
    virtual bool SupportsLayerSpanComposition() const { return false; }
    virtual bool OperateLayerSpanComposite(const TVPLayerSpanCompositePacket&,void*) { return false; }
    virtual TVPLayerSpanCompositeResult LastLayerSpanCompositeResult() const { return TVPLayerSpanCompositeResult::BackendFailure; }
    virtual TVPLayerParameterUploadStats GetLayerParameterUploadStats() const { return {}; }
    virtual bool SetLayerAlphaTables(const uint8_t*, const uint8_t*) { return false; }
    virtual bool SetLayerPsTables(const uint8_t* softLight, const uint8_t* colorDodge,
                                  const uint8_t* colorBurn) { return false; }
    virtual void* CreateLayerTexture(int, int, TVPLayerTextureFormat) { return nullptr; }
    virtual void DestroyLayerTexture(void*) {}
    virtual void SetLayerDiagnosticIdentity(void*, const layer_hotspot::Identity&) {}
    virtual bool UpdateLayerTexture(void*, const uint8_t*, int, const TVPLayerRect&) { return false; }
    // Copies the borrowed R8 bytes synchronously, then draws using the ordinary
    // integer operator. False guarantees no glyph target write; exceptions must
    // propagate without replay. Atlas resources never escape this backend.
    virtual bool OperateLayerGlyph(const TVPLayerOperation&,void*,const TVPLayerRect&,
        const uint8_t*,int,int,int,int,TVPLayerGlyphUploadInfo&) { return false; }
    virtual void ResetLayerGlyphResources() {}
    virtual void RecordLayerGlyphRejection(layer_upload::GlyphReject) {}
    // Fast full-surface copy from an offscreen render target into a Layer texture.
    // Backends that cannot guarantee identical RGBA8 dimensions return false.
    virtual bool CopyTargetToLayerTexture(void*, void*) { return false; }
    // Same-size RGBA targets; preserve destination pixels outside region.
    virtual bool CopyTargetToLayerTextureRegion(void*, void*, const TVPLayerRect&) { return false; }
    // Swaps the GPU images behind two same-size RGBA render targets (Layer
    // textures included) without copying: each handle keeps its identity and
    // takes the other's pixels. False leaves both unchanged.
    virtual bool ExchangeTargetTextures(void*, void*) { return false; }
    // A target's resource identity and write count, for checking later that
    // its pixels are unchanged. False when the handle is unknown.
    virtual bool GetTargetContentVersion(void*, uint64_t&, uint64_t&) { return false; }
    virtual bool ReadLayerTexture(void*, std::vector<uint8_t>&, int&) { return false; }
    virtual bool ReadLayerTextureRegion(void*, const TVPLayerRect&, std::vector<uint8_t>&, int&) { return false; }
    // Last synchronous texture read only; querying this never blocks. Zero is
    // also the default for unsupported backends, not an estimated wall time.
    virtual uint64_t GetLastReadbackWaitNanoseconds() const { return 0; }
    virtual std::shared_ptr<AsyncLayerPresentation> GetCurrentLayerPresentation() const { return {}; }
    virtual uint64_t GetLastLayerPresentationSerial() const { return 0; }
    // Encodes into normal frame work; never submits or waits for the GPU.
    virtual bool RequestLayerTextureRegionRead(void*, const TVPLayerRect&,
                                               const std::shared_ptr<AsyncLayerReadback>&) { return false; }
    virtual bool OperateLayerRect(const TVPLayerOperation&, void*, const TVPLayerRect&,
                                  void*, const TVPLayerRect&, int) { return false; }
    // Prepared software warp subset: source snapshot on alias, then blend the
    // previous target over the full clip (including transparent warp borders).
    // Supported methods/flags are declared by TVPLayerOperationSupportsAffine.
    virtual bool OperateLayerAffine(const TVPLayerOperation&, void*, const TVPLayerAffineCopy&,
                                    void*, int) { return false; }
    // Batch is ordered and transactional: false leaves the real target intact.
    // Each aliased warp sees the result of preceding quads in this batch.
    virtual bool OperateLayerPerspective(const TVPLayerOperation&, void*,
                                         const TVPLayerPerspectiveQuad*, std::size_t,
                                         void*, int) { return false; }
    // Two-source ordinary Layer operation. Kept separate from OperateLayerRect
    // so existing backends do not need to change their single-source ABI.
    virtual bool OperateLayerRectDualSource(const TVPLayerOperation&, void*, const TVPLayerRect&,
                                            void*, const TVPLayerRect&,
                                            void*, const TVPLayerRect&) { return false; }
    // Universal transitions read two RGBA sources and an R8 rule map.
    // Other backends may keep the existing software path.
    virtual bool OperateLayerRectTripleSource(const TVPLayerOperation&, void*, const TVPLayerRect&,
                                              void*, const TVPLayerRect&,
                                              void*, const TVPLayerRect&,
                                              void*, const TVPLayerRect&) { return false; }

    // Duration of the last completed GPU submission, excluding queue wait.
    // Negative means unsupported/not available; reading never waits for GPU.
    virtual double GetGpuSubmissionTimeMilliseconds() const { return -1.0; }
    // Main-thread waits for command-buffer capacity and a presentation surface
    // during the last frame. Negative means unsupported; included in step time.
    virtual double GetPresentationWaitTimeMilliseconds() const { return -1.0; }

    // Optional screenshot: top-down RGBA8, allocated only on request.
    // Unsupported backends retain the host screenshot path.
    virtual bool CaptureFrame(std::vector<uint8_t>&, int&, int&, int&)
    {
        return false;
    }
    // 当前混合状态里的 opacity / 染色。DrawQuads 不带这两个参数（它们属于
    // "当前状态"），默认实现必须从这里取——否则批绘制会把半透明图层按
    // 全不透明画出来（曾导致标题画面的淡入蒙层直接盖住标题内容）。
    float GetBlendOpacity() const { return blendOpacity_; }
    const float* GetBlendModulation() const { return blendModulation_; }

protected:
    // 由各后端 SetBlend 写入；默认值等价于"不透明、无染色"。
    float blendOpacity_ = 1.0f;
    float blendModulation_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

//---------------------------------------------------------------------------
// 后端注册表：静态注册 + 平台探测，供 -render 参数选择与日志使用
//---------------------------------------------------------------------------
struct TVPRenderBackendDesc
{
    const char* name;        // 命令行使用的名字（"software"/"opengl"/"vulkan"）
    const char* description; // 人可读描述
    bool (*probe)();         // 平台可用性探测，nullptr 表示编译进来即可用
    iTVPRenderBackend* (*create)(); // 工厂；部分后端需要平台参数（如 SDL 窗口），
                                    // 由入口直接构造，此时可为 nullptr
};
void TVPRegisterRenderBackend(const TVPRenderBackendDesc& desc);
// 画面比例：false=保持比例 letterbox（默认），true=把比 16:9 窄的横屏画面拉伸到 16:9 后再 letterbox
void TVPSetStretchMode(bool enable);
bool TVPGetStretchMode();
// GPU 后端每发出一次 draw call 计数一次；累计值供宿主算每帧 draw call 数
// 定义在头文件里，独立的 Metal 后端测试不链接 TVPCompositor.cpp 也能用
inline std::atomic<uint64_t> TVPDrawCallCount{0};
inline void TVPCountDrawCall() { TVPDrawCallCount.fetch_add(1, std::memory_order_relaxed); }
inline uint64_t TVPGetDrawCallCount() { return TVPDrawCallCount.load(std::memory_order_relaxed); }
// 探测通过的可用后端名列表
std::vector<std::string> TVPListRenderBackends();
bool TVPRenderBackendAvailable(const std::string& name);

//---------------------------------------------------------------------------
// 全局当前后端（同时承担窗口合成与 2D 网格渲染，插件经此获取渲染器）
//---------------------------------------------------------------------------
void TVPSetRenderBackend(iTVPRenderBackend* backend);
iTVPRenderBackend* TVPGetRenderBackend();
void TVPInvalidatePresentation(const char* reason);

struct TVPEmoteCaptureStats {
    uint64_t calls = 0;
    uint64_t cpuFallbacks = 0;
    uint64_t cpuBytes = 0;
    uint64_t gpuCopies = 0;
    uint64_t gpuBytes = 0;
    uint64_t skipped = 0;
    uint64_t regionPixels = 0;
    uint64_t fullPixels = 0;
};
void TVPRecordEmoteCaptureCall();
void TVPRecordEmoteCaptureCPUFallback(uint64_t bytes);
void TVPRecordEmoteCaptureGPUCopy(uint64_t bytes);
void TVPRecordEmoteCaptureSkip();
void TVPRecordEmoteCaptureRegion(uint64_t pixels, uint64_t fullPixels);
TVPEmoteCaptureStats TVPGetEmoteCaptureStats();
void TVPResetEmoteCaptureStats();

struct TVPRuntimeProfileStats {
    uint64_t emoteProgressCalls = 0;
    uint64_t emoteProgressTimeNS = 0;
    uint64_t emotePrepareCalls = 0;
    uint64_t emotePrepareTimeNS = 0;
    uint64_t emoteDrawCalls = 0;
    uint64_t emoteDrawTimeNS = 0;
    uint64_t emoteCaptureProfileCalls = 0;
    uint64_t emoteCaptureTimeNS = 0;
    // progress() parts: timeline/eye controls, the physics anchor sample
    // (builds the sample tree on a new player) and the spring update.
    // The slowest single progress() call is a high-water mark.
    uint64_t emoteProgressControlTimeNS = 0;
    uint64_t emoteProgressSampleTimeNS = 0;
    uint64_t emoteProgressPhysicsTimeNS = 0;
    uint64_t emoteProgressMaxTimeNS = 0;
    uint64_t emoteIconTextures = 0;
    uint64_t emoteIconTextureBytes = 0;
    uint64_t emoteIconTextureShares = 0;

    // M6.2 Emote prepare breakdown. Node time is local work only (recursive
    // children/submotions are measured by their own node records). Shape/mesh
    // build time is intentionally a subset of node time.
    uint64_t emotePrepareTransformTimeNS = 0;
    uint64_t emotePrepareMotionProgressTimeNS = 0;
    uint64_t emotePrepareSnapshotTimeNS = 0;
    uint64_t emoteNodeProgressCalls = 0;
    uint64_t emoteNodeProgressTimeNS = 0;
    uint64_t emoteSubmotionCreates = 0;
    uint64_t emoteSubmotionRebuildTimeNS = 0;
    uint64_t emoteShapeBuildCalls = 0;
    uint64_t emoteShapeBuildTimeNS = 0;
    uint64_t emoteShapeVertices = 0;
    uint64_t emoteMeshBuildCalls = 0;
    uint64_t emoteMeshBuildTimeNS = 0;
    uint64_t emoteMeshVerticesBuilt = 0;
    uint64_t emoteDeformedVerticesBuilt = 0;
    uint64_t emoteGPUDeformDraws = 0;
    uint64_t emoteGPUDeformVertices = 0;

    // Per-MikageKRKRStep Emote draw cadence. These distinguish multiple
    // legitimate players from redundant repeated draws of the same player.
    uint64_t emoteRenderSteps = 0;
    uint64_t emotePlayerDraws = 0;
    uint64_t emoteDistinctPlayerDraws = 0;
    uint64_t emoteRepeatedPlayerDraws = 0;
    uint64_t emoteDistinctTargets = 0;
    uint64_t emoteMaxDrawsPerStep = 0;
    uint64_t emoteMaxPlayersPerStep = 0;
    uint64_t emoteMaxDrawsPerPlayerStep = 0;

    uint64_t meshDrawCalls = 0;
    uint64_t meshVertices = 0;
    uint64_t meshIndices = 0;
    uint64_t meshCPUTimeNS = 0;
    uint64_t meshValidationTimeNS = 0;
    uint64_t meshBufferAllocations = 0;
    uint64_t meshBufferBytes = 0;
    uint64_t meshBufferAllocationTimeNS = 0;
    uint64_t metalSubmits = 0;
    uint64_t metalSyncWaits = 0;
    uint64_t metalSyncWaitTimeNS = 0;
    uint64_t metalQueueWaitTimeNS = 0;

    // Command-generation costs, accumulated for the current game session.
    uint64_t metalRenderEncoders = 0;
    uint64_t metalComputeEncoders = 0;
    uint64_t metalBlitEncoders = 0;
    uint64_t metalLayerRectSnapshots = 0;
    uint64_t metalLayerRectSnapshotBytes = 0;
    uint64_t metalSurfaceUploadBytes = 0;
    uint64_t emoteMaskClears = 0;
    uint64_t emoteMaskDraws = 0;
    uint64_t emoteUniqueMaskGroups = 0;
    uint64_t emoteLayerGPUCopies = 0;
    uint64_t emoteLayerGPUCopyBytes = 0;
    uint64_t emoteLayerCPUReadbacks = 0;
    uint64_t emoteLayerCPUReadbackBytes = 0;
    uint64_t emoteLayerCPUReadbackTimeNS = 0;
    // Emote frames that reached their layer without a canvas copy: rendered in
    // place, or a D3D-adaptor capture that exchanged textures.
    uint64_t emoteLayerDirectDraws = 0;

    // Persistent transient-upload arena statistics.
    uint64_t metalRingBytes = 0;
    uint64_t metalRingSuballocs = 0;
    uint64_t metalRingSuballocTimeNS = 0;
    uint64_t metalRingWraps = 0;
    uint64_t metalRingStallTimeNS = 0;
    uint64_t metalRingHighWaterBytes = 0;
    uint64_t metalRingFallbackAllocations = 0;
    uint64_t metalRingFallbackBytes = 0;
};
void TVPRecordEmoteProgress(uint64_t nanoseconds);
void TVPRecordEmoteProgressDetail(uint64_t controlNS, uint64_t sampleNS, uint64_t physicsNS);
// Live E-mote icon textures (count and bytes change by the deltas) and icons
// that reused another player's texture of the same model.
void TVPRecordEmoteIconTexture(int64_t textures, int64_t bytes);
void TVPRecordEmoteIconTextureShare();
void TVPRecordEmotePrepare(uint64_t nanoseconds);
void TVPRecordEmoteDraw(uint64_t nanoseconds);
void TVPRecordEmoteCaptureTime(uint64_t nanoseconds);

// Detailed M6.2 prepare profiling accumulates in thread-local storage during one
// prepareFrame and commits once, avoiding atomics in the per-node hot path.
void TVPBeginEmotePrepareDetail();
void TVPRecordEmoteNodeProgress(uint64_t nanoseconds);
void TVPRecordEmoteSubmotionRebuild(uint64_t nanoseconds, uint64_t creations);
void TVPRecordEmoteShapeBuild(uint64_t nanoseconds, uint64_t vertices);
void TVPRecordEmoteMeshBuild(uint64_t nanoseconds, uint64_t vertices,
                             uint64_t deformedVertices);
void TVPRecordEmoteGPUDeform(uint64_t vertices);

// Mikage host brackets one SDL_AppIterate call with these. EmotePlayer records
// its identity and render target when an actual render request is made.
void TVPBeginRuntimeStep();
void TVPRecordEmotePlayerDraw(uintptr_t playerIdentity, uintptr_t targetIdentity);
void TVPRecordEmoteMaskGroup(uint64_t identityHash);
void TVPRecordEmoteMaskClear();
void TVPRecordEmoteMaskDraw();
void TVPRecordEmoteLayerGPUCopy(uint64_t bytes);
void TVPRecordEmoteLayerCPUReadback(uint64_t bytes, uint64_t nanoseconds);
void TVPRecordEmoteLayerDirectDraw();
void TVPEndRuntimeStep();

void TVPCommitEmotePrepareDetail(uint64_t transformTimeNS,
                                 uint64_t motionProgressTimeNS,
                                 uint64_t snapshotTimeNS);

void TVPRecordMeshDraw(uint64_t vertices, uint64_t indices, uint64_t cpuTimeNS,
                       uint64_t validationTimeNS, uint64_t bufferAllocations,
                       uint64_t bufferBytes, uint64_t bufferAllocationTimeNS);
void TVPRecordMetalSubmit();
void TVPRecordMetalRenderEncoder();
void TVPRecordMetalComputeEncoder();
void TVPRecordMetalBlitEncoder();
void TVPRecordMetalLayerRectSnapshot(uint64_t bytes);
void TVPRecordMetalSurfaceUpload(uint64_t bytes);
void TVPRecordMetalSyncWait(uint64_t nanoseconds);
void TVPRecordMetalQueueWait(uint64_t nanoseconds);
void TVPRecordMetalRingSuballoc(uint64_t bytes, uint64_t highWaterBytes,
                                uint64_t nanoseconds);
void TVPRecordMetalRingWrap();
void TVPRecordMetalRingStall(uint64_t nanoseconds);
void TVPRecordMetalRingFallback(uint64_t bytes);
TVPRuntimeProfileStats TVPGetRuntimeProfileStats();
void TVPResetRuntimeProfileStats();

// 销毁当前后端并清空指针
void TVPShutdownRenderBackend();

//---------------------------------------------------------------------------
// Windows Compositor
//---------------------------------------------------------------------------
void fetchGLInfo();
TVPSprite* KRKR_Get_Current_Sprite();
// 贴图管理
void TVPJoinTexture(TVPSprite* sp);
void TVPDepartTexture(TVPSprite* sp);
void TVPResetCompositorSessionState();
// 渲染函数
void TVPRenderOnce(int winWidth, int winHeight);
void TVPCreateTexture(TVPSprite& sp);
void TVPUpdateTexture(TVPSprite* sp, uint8_t* buff, int width, int height, int pitch);
void TVPDestroyTexture(TVPSprite* sp);
// 清理合成器中所有窗口贴图
void TVPClearAllTexture();
} // namespace krkrsdl3
