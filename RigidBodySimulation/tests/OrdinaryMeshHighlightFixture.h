#pragma once
// Only the opt-in ordinary-frame audit needs repeatable authored UUID ordering.
// The ordinary executable aliases the original factory without a wrapper.
namespace Rbs::OrdinaryHighlightFixture
{
#ifdef GENGINE_MESH_HIGHLIGHT_AUDIT
    inline auto Create(::GEngine::_Scene& scene, const std::string& tag, const std::string& key)
    {
        if (std::getenv("GENGINE_ORDINARY_HIGHLIGHT_OUTPUT"))
        {
            std::uint64_t identity=14695981039346656037ull;
            for (const unsigned char c : key) { identity^=c; identity*=1099511628211ull; }
            return scene.CreateEntityWithUUID(::GEngine::UUID{identity},tag);
        }
        return ::Rbs::ColliderReplay::Create(scene,tag,key);
    }
#else
    using ::Rbs::ColliderReplay::Create;
#endif
}
