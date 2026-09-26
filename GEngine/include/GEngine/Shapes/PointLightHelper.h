#pragma once
#include "Shapes/Sphere.h"
#include "Shapes/PointLightSegments.h"

namespace GEngine::Shape
{
    // Retained private geometry authoring bridge. Validate caller-supplied float
    // counts with PointLightSegments::Create before acquiring geometry resources.
    class PointLightHelper : public Sphere
    {
    public:
        explicit PointLightHelper(float size = 0.5f, PointLightSegments segments = {}) :
            Sphere(size, segments.Radius(), segments.Height()) {}
    };
}
