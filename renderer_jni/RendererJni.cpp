#define NOMINMAX
#include <Windows.h>
#include <jni.h>

#include "RendererApi.h"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
struct BridgeRenderer
{
    HMODULE module = nullptr;
    RendererHandle renderer = nullptr;
    CreateRendererFn createRenderer = nullptr;
    RendererFn deleteRenderer = nullptr;
    RendererFn init = nullptr;
    SetUpRenderParametersFn setUpRenderParameters = nullptr;
    RenderFn render = nullptr;
    ResizeViewportFn resizeViewport = nullptr;
    GetImageFn getImage = nullptr;
    GetSliceImageFn getSliceImage = nullptr;
    SetUpSliceStateFn setUpSliceState = nullptr;
    RotateCameraFn rotateCamera = nullptr;

    ~BridgeRenderer()
    {
        if (renderer && deleteRenderer) deleteRenderer(renderer);
        if (module) FreeLibrary(module);
    }
};

std::filesystem::path moduleDirectory()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&moduleDirectory),
        &module);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::string toUtf8(JNIEnv* env, jstring value)
{
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

void throwJava(JNIEnv* env, const std::string& message)
{
    jclass type = env->FindClass("java/lang/IllegalStateException");
    env->ThrowNew(type, message.c_str());
}

template<typename T>
T loadExport(HMODULE module, const char* name)
{
    auto address = GetProcAddress(module, name);
    if (!address) throw std::runtime_error(std::string("Missing renderer export: ") + name);
    return reinterpret_cast<T>(address);
}

BridgeRenderer* fromHandle(jlong handle)
{
    return reinterpret_cast<BridgeRenderer*>(handle);
}

jbyteArray copyImage(JNIEnv* env, const RenderImage& image)
{
    if (!image.front || image.length <= 0) return env->NewByteArray(0);
    jbyteArray result = env->NewByteArray(image.length);
    env->SetByteArrayRegion(result, 0, image.length, static_cast<const jbyte*>(image.front));
    return result;
}
}

extern "C" {
JNIEXPORT jlong JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_create(
    JNIEnv* env, jclass, jstring backend)
{
    try {
        auto bridge = std::make_unique<BridgeRenderer>();
        const std::string selected = toUtf8(env, backend);
        const wchar_t* file = selected == "Vulkan" ? L"renderer_vulkan.dll" : L"renderer_opengl.dll";
        const auto path = moduleDirectory() / file;
        bridge->module = LoadLibraryW(path.c_str());
        if (!bridge->module) throw std::runtime_error("Cannot load renderer backend DLL");

        bridge->createRenderer = loadExport<CreateRendererFn>(bridge->module, "CreateRenderer");
        bridge->deleteRenderer = loadExport<RendererFn>(bridge->module, "DeleteRenderer");
        bridge->init = loadExport<RendererFn>(bridge->module, "Init");
        bridge->setUpRenderParameters = loadExport<SetUpRenderParametersFn>(bridge->module, "SetUpRenderParameters");
        bridge->render = loadExport<RenderFn>(bridge->module, "Render");
        bridge->resizeViewport = loadExport<ResizeViewportFn>(bridge->module, "ResizeViewport");
        bridge->getImage = loadExport<GetImageFn>(bridge->module, "GetImage");
        bridge->getSliceImage = loadExport<GetSliceImageFn>(bridge->module, "GetSliceImage");
        bridge->setUpSliceState = loadExport<SetUpSliceStateFn>(bridge->module, "SetUpSliceState");
        bridge->rotateCamera = loadExport<RotateCameraFn>(bridge->module, "RotateCamera");
        bridge->renderer = bridge->createRenderer();
        if (!bridge->renderer) throw std::runtime_error("CreateRenderer returned null");
        return reinterpret_cast<jlong>(bridge.release());
    }
    catch (const std::exception& ex) {
        throwJava(env, ex.what());
        return 0;
    }
}

JNIEXPORT void JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_destroy(JNIEnv*, jclass, jlong handle)
{
    delete fromHandle(handle);
}

JNIEXPORT void JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_initialize(JNIEnv*, jclass, jlong handle)
{
    auto bridge = fromHandle(handle);
    bridge->init(bridge->renderer);
}

JNIEXPORT void JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_setVolume(
    JNIEnv* env, jclass, jlong handle, jbyteArray data, jint width, jint height, jint depth,
    jint windowWidth, jint windowCenter, jdouble spacing, jdouble thickness)
{
    auto bridge = fromHandle(handle);
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    bridge->setUpRenderParameters(
        bridge->renderer,
        reinterpret_cast<uint16_t*>(bytes),
        width, height, depth, windowWidth, windowCenter, spacing, thickness);
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_resizeViewport(
    JNIEnv*, jclass, jlong handle, jint index, jint width, jint height)
{
    auto bridge = fromHandle(handle);
    bridge->resizeViewport(bridge->renderer, index, width, height);
}

JNIEXPORT void JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_setSlice(
    JNIEnv*, jclass, jlong handle, jint index,
    jfloat ox, jfloat oy, jfloat oz,
    jfloat ux, jfloat uy, jfloat uz,
    jfloat vx, jfloat vy, jfloat vz,
    jfloat centerU, jfloat centerV, jfloat halfU, jfloat halfV)
{
    auto bridge = fromHandle(handle);
    bridge->setUpSliceState(
        bridge->renderer, index, { ox, oy, oz }, { ux, uy, uz }, { vx, vy, vz },
        { centerU, centerV, halfU, halfV });
}

JNIEXPORT void JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_rotate(
    JNIEnv*, jclass, jlong handle, jfloat dx, jfloat dy)
{
    auto bridge = fromHandle(handle);
    bridge->rotateCamera(bridge->renderer, dx, dy);
}

JNIEXPORT void JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_render(
    JNIEnv*, jclass, jlong handle, jint mask)
{
    auto bridge = fromHandle(handle);
    bridge->render(bridge->renderer, mask);
}

JNIEXPORT jbyteArray JNICALL Java_com_pulimed_renderer_nativebridge_NativeRenderer_getImage(
    JNIEnv* env, jclass, jlong handle, jint index)
{
    auto bridge = fromHandle(handle);
    RenderImage image{};
    if (index == 3) bridge->getImage(bridge->renderer, &image);
    else bridge->getSliceImage(bridge->renderer, index, &image);
    return copyImage(env, image);
}
}
