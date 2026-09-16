// Separate TU: Geometry's legacy Attribute template and BufferLayout's Attribute
// are distinct existing APIs which cannot be included together.
#include "gepch.h"
#include "Geometry/Geometry.h"
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<GEngine::Geometry>
    && !std::is_copy_assignable_v<GEngine::Geometry>
    && !std::is_move_constructible_v<GEngine::Geometry>
    && !std::is_move_assignable_v<GEngine::Geometry>);

void GeometryOwnership()
{
    const auto require = [](bool valid, const char* reason) { if (!valid) throw std::runtime_error(reason); };
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        GLuint vao = 0, vbo = 0, ebo = 0;
        {
            GEngine::Geometry geometry;
            vao = geometry.GetVAO();
            const std::vector<GEngine::Math::Vec3f> points{ {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
            geometry.AddAttributes(points);
            auto& attribute = std::get<GEngine::Buffer::Attribute<GEngine::Math::Vec3f>>(
                geometry.GetAttributes().at(0));
            attribute.LoadData(); attribute.AssociateSlot(0); vbo = attribute.GetBufferRef();
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
    std::cout << "[PASS] Geometry EBO replacement/single-owner/attribute-binding/destruction cycles=3\n";
}
