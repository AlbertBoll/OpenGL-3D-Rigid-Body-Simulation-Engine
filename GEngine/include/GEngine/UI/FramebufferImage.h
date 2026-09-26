#pragma once
#include "Core/FrameBuffer.h"
#include "Core/ImageError.h"

namespace GEngine
{
    class RenderTarget;
    class Image;
    namespace UI
    {
        [[nodiscard]] ImageResult RasterImage(const ::GEngine::Image&, float width, float height);

        // Draw the target's resolved color image in the current UI window.
        // Dimensions are logical UI units; backend image identifiers stay private.
        [[nodiscard]] FramebufferResult FramebufferImage(const RenderTarget&, float width, float height);
    }
}
