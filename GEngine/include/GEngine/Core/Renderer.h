#pragma once
#include"Math/Math.h"

namespace GEngine
{
	using namespace Math;

	class Window;
	class CameraBase;
	class Actor;
	class RenderTarget;

	struct RenderStats
	{
		int m_ArrayDrawCall{};
		int m_ElementsDrawCall{};
		int m_VerticeCount{};
		int m_IndicesCount{};
	};

	struct RenderParam
	{
		Vec4f ClearColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		bool bClearColorBit{true};
		bool bClearDepthBit{true};
		bool bClearStencilBit{ false };
		bool bEnableDepthTest{ true };


	};

	class Renderer
	{
		
	public:

		static void ImGuiRender();
		static void Initialize(const Vec3f& clearColor = { 0.5f, 0.5f, 0.5f });

		static void Set(const RenderParam& param);
		static void Clear();

		//static void RenderBegin(CameraBase* camera, Window* window, bool bClearColor = true, bool bClearDepth = true, RenderTarget* target = nullptr);

		static void RenderBegin(CameraBase* camera, RenderTarget* target);

		static void RenderBegin(CameraBase* camera, bool bClearColor = true, bool bClearDepth = true, RenderTarget* target = nullptr);
		static void RenderScene(Actor* scene, CameraBase* camera);

		static void RenderBeginHUD(CameraBase* camera);

		static void Render(Actor* scene, CameraBase* camera);

		static RenderStats& GetRenderStats() { return m_RenderStats; }

		static void SetSurfaceSize(int new_width, int new_height)
		{
			m_WindowWidth = new_width;
			m_WindowHeight = new_height;
		}

		static Vec2f GetSurfaceSize()
		{
			return { m_WindowWidth, m_WindowHeight };
		}

		static void RenderHelper(Actor* entity, CameraBase* camera);

	private:
		inline static int m_WindowWidth{};
		inline static int m_WindowHeight{};
		inline static RenderStats m_RenderStats{};

	};
}
