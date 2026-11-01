#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <TextPrimitives.hpp>

// DXC requires the Windows COM types and SAL annotations before its declarations.
#include <Windows.h>
#include <Unknwn.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#endif
