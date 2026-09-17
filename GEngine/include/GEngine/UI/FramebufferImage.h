#pragma once
#include "Core/FrameBuffer.h"

namespace GEngine
{
    class RenderTarget;
    namespace UI
    {
        // Draw the target's resolved color image in the current UI window.
        // Dimensions are logical UI units; backend image identifiers stay private.
        [[nodiscard]] FramebufferResult FramebufferImage(const RenderTarget&, float width, float height);
    }
}
