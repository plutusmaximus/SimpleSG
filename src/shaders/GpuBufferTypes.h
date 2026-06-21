#pragma once

#include "GpuHelper.h"

// Strongly-typed GPU storage buffer classes.
using WorldTransformBuffer = SemanticGpuBuffer<ShaderInterop::WorldTransform>;
using ClipSpaceBuffer = SemanticGpuBuffer<ShaderInterop::ClipSpaceTransform>;
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties>;
using CameraParamsBuffer = SemanticGpuBuffer<ShaderInterop::CameraParams>;
using MaterialConstantsBuffer = SemanticGpuBuffer<ShaderInterop::MaterialConstants>;