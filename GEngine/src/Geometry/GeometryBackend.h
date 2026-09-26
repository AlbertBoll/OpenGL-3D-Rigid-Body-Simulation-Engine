#pragma once
// Backend-private native inspection for retained geometry; normal consumers never include this header.
#include "Geometry/Geometry.h"
#include <glad/glad.h>
namespace GEngine::GeometryDetail
{
    struct BackendAccess
    {
        static GLuint VertexArray(const Geometry& geometry) noexcept { return geometry.m_Vao; }
        template<class T> static GLuint AttributeBuffer(const Buffer::Attribute<T>& attribute) noexcept
        { return attribute.m_BufferRef; }
    };
}
