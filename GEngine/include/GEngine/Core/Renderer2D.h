#pragma once
#include <Math/Math.h>



namespace GEngine
{
	class SpriteEntity;
	class CameraBase;
	class RenderTarget;

	using namespace Math;

	struct Render2DParam
	{
		Vec4f ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		bool bClearColorBit{ true };
		bool bClearDepthBit{ false };
		bool bClearStencilBit{ false };
		bool bEnableDepthTest{ false };


	};
	
	class Renderer2D
	{

	public:
		static void Render(SpriteEntity* entity, CameraBase* camera);
		static void Render(const std::vector<SpriteEntity*>& entities, CameraBase* camera);
		static void Render(const std::unordered_map<unsigned int, std::vector<std::vector<SpriteEntity*>>>& groupsLookUp, CameraBase* camera);
		static void RenderBegin(CameraBase* camera, RenderTarget* target = nullptr);
		static void RenderSetup(const Render2DParam& param = Render2DParam{});
		static void Initialize(const Vec2f& windowSize);



		static void SetSurfaceSize(int new_width, int new_height)
		{
			m_WindowWidth = new_width;
			m_WindowHeight = new_height;
		}

		

	private:
		inline static int m_WindowWidth{};
		inline static int m_WindowHeight{};
	};

}