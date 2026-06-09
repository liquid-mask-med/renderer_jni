#pragma once

#include <cstdint>

struct RenderImage
{
    int width;
    int height;
    void* front;
    void* back;
    int length;
};

struct Vec3
{
    float x;
    float y;
    float z;
};

using RendererHandle = void*;
using CreateRendererFn = RendererHandle(*)();
using RendererFn = void(*)(RendererHandle);
using RenderFn = void(*)(RendererHandle, int);
using ResizeViewportFn = void(*)(RendererHandle, int, int, int);
using GetImageFn = void(*)(RendererHandle, RenderImage*);
using GetSliceImageFn = void(*)(RendererHandle, int, RenderImage*);
using SetUpRenderParametersFn = void(*)(RendererHandle, uint16_t*, int, int, int, int, int, double, double);
using SetUpSliceStateFn = void(*)(RendererHandle, int, Vec3, Vec3, Vec3);
using RotateCameraFn = void(*)(RendererHandle, float, float);

