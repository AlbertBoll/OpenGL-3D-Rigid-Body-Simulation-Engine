#pragma once
#include "Renderer/RenderFrame.h"
#include <array>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace GEngine
{
    enum class DebugGeometryCode { InvalidInput, Capacity };
    using DebugGeometryResult = std::expected<void, DebugGeometryCode>;

    // CPU-only, caller-bounded, one-frame scratch. Extraction copies Lines() into
    // the immutable frame; producer reuse cannot change retained frames.
    // World units, linear RGBA, one pixel. Tested uses LEQUAL; no depth writes,
    // shadows or picking. Each primitive appends atomically or returns an error.
    class DebugGeometry final
    {
    public:
        explicit DebugGeometry(std::span<FrameDebugLine> storage) : m_Storage(storage) {}
        std::span<const FrameDebugLine> Lines() const { return m_Storage.first(m_Size); }
        void Clear() { m_Size=0; }
        // Read-only indexed local geometry. Scale must already be in vertices;
        // apply only this rigid pose, once. Validate the whole payload before
        // appending so invalid indices/capacity leave earlier primitives intact.
        [[nodiscard]] DebugGeometryResult IndexedLines(std::span<const glm::vec3> vertices,
            std::span<const std::array<std::size_t,2>> edges,glm::vec3 position,glm::quat orientation,
            glm::vec4 color,DebugDepth depth=DebugDepth::Tested)
        {
            if (edges.size()>m_Storage.size()-m_Size) return std::unexpected(DebugGeometryCode::Capacity);
            if (!Pose(position,orientation) || !Finite(glm::vec3(color)) || !std::isfinite(color.a) ||
                color.a<0 || color.a>1 || (depth!=DebugDepth::Tested && depth!=DebugDepth::Overlay)) return Invalid();
            for (const auto& edge:edges)
                for (auto index:edge)
                    if (index>=vertices.size() || !Finite(position+orientation*vertices[index])) return Invalid();
            for (const auto& edge:edges)
                m_Storage[m_Size++]={position+orientation*vertices[edge[0]],position+orientation*vertices[edge[1]],color,{},depth};
            return {};
        }
        [[nodiscard]] DebugGeometryResult Append(std::span<const FrameDebugLine> lines)
        {
            if (lines.size()>m_Storage.size()-m_Size) return std::unexpected(DebugGeometryCode::Capacity);
            for (const auto& line:lines)
                if (!Finite(line.start) || !Finite(line.end) || !Finite(glm::vec3(line.color)) ||
                    !std::isfinite(line.color.a) || line.color.a<0 || line.color.a>1 ||
                    (line.depth!=DebugDepth::Tested && line.depth!=DebugDepth::Overlay)) return Invalid();
            for (const auto& line:lines) m_Storage[m_Size++]=line;
            return {};
        }
        [[nodiscard]] DebugGeometryResult Line(glm::vec3 a,glm::vec3 b,glm::vec4 color,
                                               DebugDepth depth=DebugDepth::Tested)
        { const FrameDebugLine line{a,b,color,{},depth}; return Append({&line,1}); }
        // Direction carries length; no hidden normalization.
        [[nodiscard]] DebugGeometryResult Ray(glm::vec3 origin,glm::vec3 direction,glm::vec4 color,
                                              DebugDepth depth=DebugDepth::Tested)
        { return Line(origin,origin+direction,color,depth); }
        [[nodiscard]] DebugGeometryResult Box(glm::vec3 minimum,glm::vec3 maximum,
            glm::vec3 position,glm::quat orientation,glm::vec4 color,DebugDepth depth=DebugDepth::Tested)
        {
            if (!Pose(position,orientation) || !Finite(minimum) || !Finite(maximum) ||
                glm::any(glm::greaterThan(minimum,maximum))) return Invalid();
            std::array<glm::vec3,8> vertices;
            for (unsigned i=0;i<8;++i)
                vertices[i]=position+orientation*glm::vec3(i&1?maximum.x:minimum.x,
                    i&2?maximum.y:minimum.y,i&4?maximum.z:minimum.z);
            std::array<FrameDebugLine,12> lines; unsigned n=0;
            for (unsigned i=0;i<8;++i) for (unsigned bit=1;bit<=4;bit<<=1)
                if (!(i&bit)) lines[n++]={vertices[i],vertices[i|bit],color,{},depth};
            return Append(lines);
        }
        [[nodiscard]] DebugGeometryResult Sphere(glm::vec3 center,float radius,glm::vec4 color,
                                                 DebugDepth depth=DebugDepth::Tested)
        {
            if (!Finite(center) || !std::isfinite(radius) || radius<=0) return Invalid();
            std::array<FrameDebugLine,48> lines;
            for (unsigned plane=0;plane<3;++plane) for (unsigned i=0;i<16;++i)
            {
                auto point=[&](unsigned j) {
                    const float angle=float(j%16)*6.283185307179586f/16.f;
                    glm::vec3 p(0); p[(plane+1)%3]=radius*std::cos(angle);
                    p[(plane+2)%3]=radius*std::sin(angle); return center+p;
                };
                lines[plane*16+i]={point(i),point(i+1),color,{},depth};
            }
            return Append(lines);
        }
        [[nodiscard]] DebugGeometryResult Axes(glm::vec3 origin,glm::quat orientation,float length,
                                               DebugDepth depth=DebugDepth::Overlay)
        {
            if (!Pose(origin,orientation) || !std::isfinite(length) || length<=0) return Invalid();
            const std::array<FrameDebugLine,3> lines{{
                {origin,origin+orientation*glm::vec3(length,0,0),{1,0,0,1},{},depth},
                {origin,origin+orientation*glm::vec3(0,length,0),{0,1,0,1},{},depth},
                {origin,origin+orientation*glm::vec3(0,0,length),{0,0,1,1},{},depth}}};
            return Append(lines);
        }
    private:
        static bool Finite(glm::vec3 v)
        { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        static bool Pose(glm::vec3 p,glm::quat q)
        { return Finite(p) && Finite({q.x,q.y,q.z}) && std::isfinite(q.w) && std::abs(glm::dot(q,q)-1.f)<=1e-4f; }
        static DebugGeometryResult Invalid() { return std::unexpected(DebugGeometryCode::InvalidInput); }
        std::span<FrameDebugLine> m_Storage;
        std::size_t m_Size{};
    };
}
