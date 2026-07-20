#pragma once

// Canonical public RHI surface. Render modules should include this header or a
// narrower public header and must not include backend-specific declarations.
#include "drivers/rhi/rhi_capabilities.h"
#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_command_descriptors.h"
#include "drivers/rhi/rhi_descriptors.h"
#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_public_definitions.h"
#include "drivers/rhi/rhi_queue.h"
#include "drivers/rhi/rhi_resource.h"
#include "drivers/rhi/rhi_result.h"
#include "drivers/rhi/rhi_viewport_context.h"
