#pragma once
#include<vector>
#include"Scene/_Entity.h"
#include"RenderTarget.h"

namespace GEngine
{
	class _Scene;
	

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

		static void CascadedShadowPreRender(_Scene* scene);
		static void MousePickPreRender(_Scene* scene, const _EditorCamera& camera, Shader* mouse_pick_shader);
	
		static void CascadedShadowSceneRender(_Scene* scene, _EditorCamera& camera, const std::vector<float>& shadowCascadeLevels);
		static void SceneRender(_Scene* scene, _EditorCamera& camera);
		static void BeginRender(_EditorCamera& camera, RenderTarget* target);
		static void BeginRender(_EditorCamera& camera, const Vec4f& color = {0.1f, 0.1f, 0.1f, 1.f});
		static void Initialize(const Vec3f& clearColor = { 0.1f, 0.1f, 0.1f });
		static void Set(const RenderParam_& param);
		static void Clear(const Vec3f& clearColor = { 0.1f, 0.1f, 0.1f });
		static void OnMouseClicked(_Scene* scene, const MousePickFrameBuffer& fb);

		static void SetSurfaceSize(int new_width, int new_height)
		{
			m_WindowWidth = new_width;
			m_WindowHeight = new_height;
		}

		static void SkyBoxRender(_Entity skybox, _EditorCamera& camera);

		static void ArraysDraw(unsigned int mode, int count, int first = 0);
	
		static void ElementsDraw(unsigned int mode, int count, unsigned int type = 0x1405, const void* indice = 0);

		//To do element instance draw
		static void ElementsInstancedDraw(unsigned int mode, int count, int instancecount, unsigned int type = 0x1405,  const void* indices = nullptr);
	
		//To do array instance draw
		static void ArraysInstancedDraw(unsigned int mode, int count, int instancecount, int first = 0);
		
		static std::vector<Vec4f> GetFrustumCornersWorldSpace(const Mat4& projview);

		static std::vector<Vec4f> GetFrustumCornersWorldSpace(const Mat4& proj, const Mat4& view);

		static Mat4 GetLightSpaceMatrix(const _EditorCamera& camera, const Vec3f& lightDir, float nearPlane, float farPlane);

		static std::vector<Mat4> GetLightSpaceMatrices(const _EditorCamera& camera, const Vec3f& lightDir, const std::vector<float>& shadowCascadeLevels);

		static Vec2f GetSurfaceSize()
		{
			return { m_WindowWidth, m_WindowHeight };
		}


		static RenderStats_& GetRenderStats() { return m_RenderStats; }
		static void UpdateRenderSetting(const RenderSetting_& renderSetting);
		static void UpdateRenderSetting(const PointSetting_& pointSetting);
		static void UpdateRenderSetting(const LineSetting_& lineSetting);
		static void UpdateRenderSetting(const SurfaceSetting_& SurfaceSetting);
		static void CascadedShadowScenePass(_Scene* scene, _EditorCamera& camera, Shader* cascade_shader, const std::vector<float>& shadowCascadeLevels, const FinalFrameBuffer& fb);
		static void CascadedShadowPass(_Scene* scene, Shader* depth_shader, const CascadeShadowFrameBuffer& fb);
		static void MousePickPass(_Scene* scene, const _EditorCamera& camera, Shader* mouse_pick_shader, const MousePickFrameBuffer& fb);

		template<typename uniformbuffer>
		static void SetupUBO(const uniformbuffer& ubo, const _EditorCamera& camera, const Vec3f& lightDir, const std::vector<float>& shadowCascadeLevels);

	


	private:
		inline static int m_WindowWidth{1280};
		inline static int m_WindowHeight{720};
		inline static RenderStats_ m_RenderStats{};
	};
	
}
