#pragma once
// Backend implementation and validation probes only; never a consumer include.
#include "Core/FrameBuffer.h"
#include <glad/glad.h>
namespace GEngine::FramebufferDetail
{
    struct Backend
    {
        static GLuint Name(const FrameBuffer&);
        static GLuint Color(const FrameBuffer&, std::uint32_t index = 0);
        static GLuint Depth(const FrameBuffer&);
        static GLuint Renderbuffer(const FrameBuffer&);
    };
}
