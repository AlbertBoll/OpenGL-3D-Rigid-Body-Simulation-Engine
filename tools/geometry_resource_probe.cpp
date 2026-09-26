#ifdef GEOMETRY_ACCESS_SCHEMA_ONLY
#include "Core/Utility.h"
#include "Geometry/Geometry.h"
#include <type_traits>
template<class T> concept NativeArrayGetter=requires(const T& value){value.GetVAO();};
template<class T> concept NativeBufferGetter=requires(const T& value){value.GetBufferRef();};
template<class T> concept NativeBufferField=requires(T& value){value.m_BufferRef;};
static_assert(!NativeArrayGetter<GEngine::Geometry>);
static_assert(!NativeBufferGetter<GEngine::Buffer::Attribute<GEngine::Math::Vec3f>>);
static_assert(!NativeBufferField<GEngine::Buffer::Attribute<GEngine::Math::Vec3f>>);
static_assert(std::is_copy_constructible_v<GEngine::Buffer::Attribute<GEngine::Math::Vec3f>>);
#else
// Separate TU: Geometry's legacy Attribute template and BufferLayout's Attribute
// are distinct existing APIs which cannot be included together.
#include "gepch.h"
#include "Geometry/Geometry.h"
#include "../GEngine/src/Geometry/GeometryBackend.h"
#include "Shapes/Cone.h"
#include "Shapes/Prism.h"
#include "Shapes/Pyramid.h"
#include "Shapes/Cylinder.h"
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<GEngine::Geometry>
    && !std::is_copy_assignable_v<GEngine::Geometry>
    && !std::is_move_constructible_v<GEngine::Geometry>
    && !std::is_move_assignable_v<GEngine::Geometry>);


namespace
{
    void Require(bool valid, const char* reason) { if (!valid) throw std::runtime_error(reason); }

    // Chain the enclosing real-GL observer so every name is still checked there.
    struct Names
    {
        inline static PFNGLGENBUFFERSPROC generate;
        inline static PFNGLDELETEBUFFERSPROC retire;
        inline static std::unordered_set<GLuint> live;
        inline static unsigned created = 0, deleted = 0;
        inline static bool valid = true;
        static void APIENTRY Generate(GLsizei n, GLuint* ids)
        {
            generate(n, ids);
            for (int i=0; i<n; ++i) { valid &= ids[i] && live.insert(ids[i]).second; ++created; }
        }
        static void APIENTRY Retire(GLsizei n, const GLuint* ids)
        {
            for (int i=0; i<n; ++i) if (ids[i]) { valid &= live.erase(ids[i]) == 1; ++deleted; }
            retire(n, ids);
        }
        Names()
        {
            live.clear(); created=deleted=0; valid=true;
            generate=glad_glGenBuffers; retire=glad_glDeleteBuffers;
            glad_glGenBuffers=Generate; glad_glDeleteBuffers=Retire;
        }
        ~Names() { glad_glGenBuffers=generate; glad_glDeleteBuffers=retire; }
        void Verify() const { Require(valid && live.empty() && created == deleted, "Geometry exact buffer retirement failed"); }
    };

    void TransformOwnership()
    {
        using namespace GEngine;
        using namespace GEngine::Math;
        Names names;
        GLuint vao=0;
        {
            Geometry geometry;
            vao=::GEngine::GeometryDetail::BackendAccess::VertexArray(geometry);
            const std::vector<Vec3f> positions{{1,2,3},{-2,1,4},{3,-1,2}};
            const std::vector<Vec3f> normals{{1,0,0},{0,1,0},{0,0,1}};
            const std::vector<Vec3f> colors{{1,0,0},{0,1,0},{0,0,1}};
            const std::vector<Vec2f> uvs{{0,0},{1,0},{0,1}};
            geometry.AddAttributes(positions,colors,uvs,normals);
            auto& position=std::get<Buffer::Attribute<Vec3f>>(geometry.GetAttributes().at(0));
            auto& normal=std::get<Buffer::Attribute<Vec3f>>(geometry.GetAttributes().at(3));
            normal.b_Normalized=true; // Existing Attribute state must survive the transform.
            position.AssociateSlot(0); normal.AssociateSlot(3);
            const auto positionName=::GEngine::GeometryDetail::BackendAccess::AttributeBuffer(position), normalName=::GEngine::GeometryDetail::BackendAccess::AttributeBuffer(normal);
            GLint beforeArray=0, beforeVao=0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&beforeArray); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&beforeVao);
            Mat4 transform{1.f};
            transform[0]=Vec4f{0,2,0,0}; transform[1]=Vec4f{-3,0,0,0};
            transform[2]=Vec4f{0,0,4,0}; transform[3]=Vec4f{5,6,7,1};
            geometry.ApplyTransform(transform);
            geometry.ApplyTransform(transform,3,true);
            geometry.ApplyTransform(transform,99); // Existing missing-location no-op.
            geometry.ApplyTransform(transform,2); // Existing non-Vec3 attribute no-op.
            Require(::GEngine::GeometryDetail::BackendAccess::AttributeBuffer(position)==positionName && ::GEngine::GeometryDetail::BackendAccess::AttributeBuffer(normal)==normalName
                && normal.b_Normalized && !position.b_Normalized && Names::created==4 && Names::deleted==0,
                "Transform replaced the existing Attribute owner/state");
            GLint afterArray=0, afterVao=0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&afterArray); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&afterVao);
            Require(beforeArray==afterArray && beforeVao==afterVao,"CPU transform changed native bindings");
            std::vector<Vec3f> expectedPositions, expectedNormals;
            for (auto p:positions) expectedPositions.push_back(Vec3f(transform*Vec4f(p,1.f)));
            for (auto n:normals) expectedNormals.push_back(glm::mat3(transform)*n);
            Require(position.m_Data==expectedPositions && normal.m_Data==expectedNormals,
                "Position/normal CPU transform changed payload semantics");
            Require(std::get<Buffer::Attribute<Vec3f>>(geometry.GetAttributes().at(1)).m_Data==colors
                && std::get<Buffer::Attribute<Vec2f>>(geometry.GetAttributes().at(2)).m_Data==uvs
                && geometry.GetAttributes().size()==4 && geometry.GetVerticesCount()==3 && geometry.GetIndicesCount()==0,
                "Transform changed other attributes/layout/counts");
            for (unsigned slot:{0u,3u})
            {
                auto& attribute=std::get<Buffer::Attribute<Vec3f>>(geometry.GetAttributes().at(slot));
                attribute.AssociateSlot(slot);
                GLint size=0,type=0,normalized=0,stride=0,name=0;
                glGetVertexAttribiv(slot,GL_VERTEX_ATTRIB_ARRAY_SIZE,&size);
                glGetVertexAttribiv(slot,GL_VERTEX_ATTRIB_ARRAY_TYPE,&type);
                glGetVertexAttribiv(slot,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&normalized);
                glGetVertexAttribiv(slot,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stride);
                glGetVertexAttribiv(slot,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&name);
                Require(size==3 && type==GL_FLOAT && stride==0 && normalized==(slot==3)
                    && static_cast<GLuint>(name)==::GEngine::GeometryDetail::BackendAccess::AttributeBuffer(attribute),"Transformed rendering attribute layout/owner changed");
                glBindBuffer(GL_ARRAY_BUFFER,::GEngine::GeometryDetail::BackendAccess::AttributeBuffer(attribute));
                std::vector<Vec3f> actual(3);
                glGetBufferSubData(GL_ARRAY_BUFFER,0,actual.size()*sizeof(Vec3f),actual.data());
                Require(actual==(slot==0?expectedPositions:expectedNormals),"Transformed GPU payload differs from CPU data");
            }
        }
        names.Verify();
        Require(!glIsVertexArray(vao),"Transform geometry VAO survived destruction");
        std::cout << "[PASS] Geometry transform position/normal CPU+GPU payload/layout/preserved-owner exact-buffers=4\n";
    }

    template<class Shape> void CappedShape(unsigned caps)
    {
        Names names;
        {
            Shape shape;
            Require(Names::valid && Names::created==4*(caps+1) && Names::deleted==4*caps && Names::live.size()==4,
                "Temporary cap buffers were replaced or survived cap destruction");
            Require(shape.GetAttributes().size()==4 && shape.GetVerticesCount()>0,"Capped shape lost its rendering layout");
        }
        names.Verify();
    }
}

void GeometryOwnership()
{
    const auto require = [](bool valid, const char* reason) { if (!valid) throw std::runtime_error(reason); };
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        GLuint vao = 0, vbo = 0, ebo = 0;
        {
            GEngine::Geometry geometry;
            vao = ::GEngine::GeometryDetail::BackendAccess::VertexArray(geometry);
            const std::vector<GEngine::Math::Vec3f> points{ {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
            geometry.AddAttributes(points);
            auto& attribute = std::get<GEngine::Buffer::Attribute<GEngine::Math::Vec3f>>(
                geometry.GetAttributes().at(0));
            attribute.LoadData(); attribute.AssociateSlot(0); vbo = ::GEngine::GeometryDetail::BackendAccess::AttributeBuffer(attribute);
            const std::vector<unsigned> indices{ 0, 1, 2 };
            geometry.AddIndices(indices);
            GLint bound = 0; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound);
            const auto replaced = static_cast<GLuint>(bound);
            geometry.AddIndices(indices);
            glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound); ebo = static_cast<GLuint>(bound);
            require(ebo != replaced && !glIsBuffer(replaced), "Geometry replacement retained the old EBO");
            geometry.BindVBO(); glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bound);
            require(static_cast<GLuint>(bound) == vbo, "Geometry treated EBO as an attribute VBO");
            std::vector<unsigned> actual(indices.size());
            glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices.size() * sizeof(unsigned), actual.data());
            require(actual == indices && geometry.GetIndicesCount() == 3 && geometry.GetVerticesCount() == 3,
                "Geometry index replacement lost payload/counts");
        }
        require(!glIsVertexArray(vao) && !glIsBuffer(vbo) && !glIsBuffer(ebo),
            "Geometry failed to retire VAO/VBO/EBO");
    }
    TransformOwnership();
    CappedShape<GEngine::Shape::Cone>(1);
    CappedShape<GEngine::Shape::Prism>(2);
    CappedShape<GEngine::Shape::Pyramid>(1);
    CappedShape<GEngine::Shape::Cylinder>(2);
    std::cout << "[PASS] Geometry temporary capped-shape retirement caps=6 exact-buffers=40\n";
    std::cout << "[PASS] Geometry EBO replacement/single-owner/attribute-binding/destruction cycles=3\n";
}

#endif
