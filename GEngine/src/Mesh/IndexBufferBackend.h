#pragma once
// Backend-private inspection; never included by normal consumer headers.
#include "Mesh/IndexBuffer.h"
#include <glad/glad.h>
namespace GEngine::IndexBufferDetail
{
    struct BackendAccess
    {
        static GLuint BufferName(const Buffer::IndexBuffer& buffer) noexcept
        { return buffer.m_IndexBufferRef; }
    };
}
