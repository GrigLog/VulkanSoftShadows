#pragma once

#include <stdexcept>
#include <string>

#include <vulkan/vulkan.h>

// The debug flags are defined in CMake
#if defined(VSS_VALIDATION) && VSS_VALIDATION
constexpr bool defaultValidationEnabled = true;
#else
constexpr bool defaultValidationEnabled = false;
#endif

#if defined(VSS_DEBUG) && VSS_DEBUG
constexpr bool isDebugBuild = true;
#else
constexpr bool isDebugBuild = false;
#endif
