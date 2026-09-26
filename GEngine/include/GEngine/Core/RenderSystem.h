#pragma once
#include<vector>
#include"Scene/_Entity.h"
#include"RenderTarget.h"

namespace GEngine
{
	class _Scene;
	

    enum class RenderSystemRangeCode { ViewportExtent, VertexCount };
    struct RenderSystemRangeError
    {
        RenderSystemRangeCode code;
        std::string_view operation, message;
        float extent = 0;
        unsigned axis = 0;
        std::size_t vertexCount = 0;
    };
    using RenderSystemError = std::variant<RenderTransformQueryError, RenderSystemRangeError, FramebufferError>;
    using RenderSystemResult = std::expected<void, RenderSystemError>;

	struct RenderStats_
	{
		int m_ArrayDrawCall{};
		int m_ElementsDrawCall{};
		int m_ArrayInstancedDrawCall{};
		int m_ElementsInstancedDrawCall{};
		int m_VerticeCount{};
		int m_IndicesCount{};
	};

	struct RenderParam_
	{
		Vec4f ClearColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		bool bClearColorBit{ true };
		bool bClearDepthBit{ true };
		bool bClearStencilBit{ false };
		bool bEnableDepthTest{ true };


	};

	//using namespace Camera;
	namespace Camera
	{
		class _EditorCamera;
	}

	namespace Asset
	{
		class Shader;
	}
	
	using namespace Camera;
	using namespace Asset;

	class RenderTarget;

	class RenderSystem
	{
	public:

		[[nodiscard]] static RenderSystemResult CascadedShadowPreRender(_Scene* scene);
		[[nodiscard]] static RenderSystemResult PointShadowPreRender(_Scene* scene, Shader* point_shadow_depth_shader, const std::vector<Mat4>& shadowTransforms, const Vec3f& lightPos, float far_plane);
		[[nodiscard]] static RenderSystemResult MousePickPreRender(_Scene* scene, const _EditorCamera& camera, Shader* mouse_pick_shader);
	
		[[nodiscard]] static RenderSystemResult CascadedShadowSceneRender(_Scene* scene, _EditorCamera& camera, const std::vector<float>& shadowCascadeLevels, float far_plane);
		[[nodiscard]] static RenderSystemResult SceneRender(_Scene* scene, _EditorCamera& camera);
		static void BeginFinalRender(_EditorCamera& camera, RenderTarget* target = nullptr, const Vec4f& color = { 0.1f, 0.1f, 0.1f, 1.f });
		static void BeginRender(_EditorCamera& camera, const Vec4f& color = {0.1f, 0.1f, 0.1f, 1.f});
		static void Initialize(const Vec3f& clearColor = { 0.1f, 0.1f, 0.1f });
		static void Set(const RenderParam_& param);
		static void Clear(const Vec3f& clearColor = { 0.1f, 0.1f, 0.1f });
		[[nodiscard]] static RenderSystemResult OnMouseClicked(_Scene* scene, const MousePickFrameBuffer& fb);
		[[nodiscard]] static RenderSystemResult OnMouseClicked(_Scene* scene, const MousePickFrameBuffer& fb, const Vec2f& min_bound, const Vec2f& max_bound);
		static void OnMouseClicked(_Scene* scene, const RenderTarget& fb, const Vec2f& min_bound, const Vec2f& max_bound);
		static void VisualizeDebugBoundingVolume(_Scene* scene, _EditorCamera& camera, Shader* debug_shader, DebugAABBBoundingBoxComponent& debug_bounding_box);
		static void VisualizeDebugBoundingVolume(_Scene* scene, _EditorCamera& camera);

		static void SetSurfaceSize(int new_width, int new_height)
		{
			m_WindowWidth = new_width;
			m_WindowHeight = new_height;
		}

		[[nodiscard]] static RenderSystemResult SkyBoxRender(_Entity skybox, _EditorCamera& camera);

	

		//To do element instance draw
	
		//To do array instance draw
		
		static std::vector<Vec4f> GetFrustumCornersWorldSpace(const Mat4& projview);

		static std::vector<Vec4f> GetFrustumCornersWorldSpace(const Mat4& proj, const Mat4& view);

		static Mat4 GetLightSpaceMatrix(const _EditorCamera& camera, const Vec3f& lightDir, float nearPlane, float farPlane);

		static std::vector<Mat4> GetLightSpaceMatrices(const _EditorCamera& camera, const Vec3f& lightDir, const std::vector<float>& shadowCascadeLevels);

		static std::vector<Mat4> GetShadowTransformMatrices(const Mat4& shadowProj, const Vec3f& lightPos);

		static Vec2f GetSurfaceSize()
		{
			return { m_WindowWidth, m_WindowHeight };
		}


		static RenderStats_& GetRenderStats() { return m_RenderStats; }
		static void UpdateRenderSetting(const RenderSetting_& renderSetting);
		static void UpdateRenderSetting(const PointSetting_& pointSetting);
		static void UpdateRenderSetting(const LineSetting_& lineSetting);
		static void UpdateRenderSetting(const SurfaceSetting_& SurfaceSetting);
		[[nodiscard]] static RenderSystemResult CascadedShadowScenePass(_Scene* scene, _EditorCamera& camera, Shader* cascade_shader, const std::vector<float>& shadowCascadeLevels, const FinalFrameBuffer& fb);
		[[nodiscard]] static RenderSystemResult CascadedShadowPass(_Scene* scene, Shader* depth_shader, const CascadeShadowFrameBuffer& fb);
		[[nodiscard]] static RenderSystemResult PointShadowPass(_Scene* scene, Shader* depth_shader, const PointShadowFrameBuffer& fb, const Vec3f& lightPos, float near_plane, float far_plane);
		[[nodiscard]] static RenderSystemResult MousePickPass(_Scene* scene, const _EditorCamera& camera, Shader* mouse_pick_shader, const MousePickFrameBuffer& fb);
		[[nodiscard]] static RenderSystemResult MousePickPass(_Scene* scene, const _EditorCamera& camera, Shader* mouse_pick_shader, const MousePickFrameBuffer& fb, const Vec2f& min_bound, const Vec2f& max_bound);
		static void FinalPassBegin(_EditorCamera& camera, RenderTarget* target);
		template<typename uniformbuffer>
		static void SetupUBO(const uniformbuffer& ubo, const _EditorCamera& camera, const Vec3f& lightDir, const std::vector<float>& shadowCascadeLevels);

		[[nodiscard]] static RenderSystemResult KDTreeVisualize(_Scene* scene, _EditorCamera& camera, Shader* debug_shader, std::vector<Vec3f>& m_Points, const DebugKDTreeVisualizer& debug_kd_tree_visualizer);
		[[nodiscard]] static RenderSystemResult PointLightsVisualize(_Scene* scene, const _EditorCamera& camera, Shader* point_light_shader);


	private:
		inline static int m_WindowWidth{1280};
		inline static int m_WindowHeight{720};
		inline static RenderStats_ m_RenderStats{};
	};
	
}
