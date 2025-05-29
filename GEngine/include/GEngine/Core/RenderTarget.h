#pragma once
#include <Assets/Textures/Texture.h>
#include <Math/Math.h>




namespace GEngine
{
	//using namespace GEngine::Asset;
	//using namespace GEngine::Math;

	enum class RenderTargetTextureFormat
	{
		None = 0,
		//Color
		RGBA8,
		RED_INTEGER,

		DEPTH24STENCIL8,

		//Default
		Depth = DEPTH24STENCIL8
	};

	struct RenderTargetTextureSpecification
	{
		RenderTargetTextureSpecification() = default;
		RenderTargetTextureSpecification(RenderTargetTextureFormat format): TextureFormat(format){}

		RenderTargetTextureFormat TextureFormat = RenderTargetTextureFormat::None;

	};

	struct RenderTargetAttachmentSpecification
	{
		RenderTargetAttachmentSpecification() = default;
		RenderTargetAttachmentSpecification(const std::initializer_list<RenderTargetTextureSpecification>& attachments): Attachments(attachments){}

		std::vector<RenderTargetTextureSpecification> Attachments;
	};

	struct RenderTargetSpecification
	{
		uint32_t Width = 0, Height = 0;
		uint32_t Samples = 4;
		RenderTargetAttachmentSpecification Attachments;

		bool SwapChainTarget = false;
	};

	class FinalFrameBuffer
	{
	public:
		FinalFrameBuffer(unsigned int resolution_x, unsigned int resolution_y);
		unsigned int GetFBO() const { return m_FBO; }
		unsigned int GetColorMap() const { return m_ColorMap; }
		unsigned int GetMousePickMap() const { return m_MousePickMap; }
		void OnResize(unsigned int width, unsigned int height);
		Math::Vec2f GetResolution()const;
		int ReadPixel(int x, int y)const;
		void Bind() const;
		void UnBind() const;
		void BindReadFrameBuffer()const;
		void BindDefaultDrawFrameBuffer() const;
		void ClearMousePickAttachment(int value)const;
		void BlitFrameBuffer()const;

	private:
		void Invalidate();
	private:
		unsigned int m_FBO{};
		unsigned int m_ColorMap{};
		unsigned int m_MousePickMap{};
		unsigned int m_DepthMap{};
		unsigned int m_Width{};
		unsigned int m_Height{};
	};

	class MousePickFrameBuffer
	{
	public:
		MousePickFrameBuffer(unsigned int resolution_x, unsigned int resolution_y);

		unsigned int GetLightFBO() const { return m_MousePickFBO; }
		unsigned int GetMousePickMap() const { return m_MousePickColorMap; }
		void OnResize(unsigned int width, unsigned int height);
		Math::Vec2f GetResolution()const;
		int ReadPixel(int x, int y)const;
		void Bind() const;
		void UnBind() const;
		void ClearAttachment(uint32_t attachmentindex, int value)const;
	
	private:
		void Invalidate();
	private:
		unsigned int m_MousePickFBO{};
		unsigned int m_MousePickColorMap{};
		unsigned int m_MousePickDepthMap{};
		unsigned int m_Width{};
		unsigned int m_Height{};
	};
	

	class CascadeShadowFrameBuffer
	{
	public:
		CascadeShadowFrameBuffer(unsigned int resolution_x, unsigned int resolution_y, unsigned int depth);
		
		unsigned int GetLightFBO() const { return m_LightFBO; }
		unsigned int GetLightDepthMaps() const { return m_LightDepthMaps; }
		//unsigned int GetMousePickMap() const { return m_MousePickMap; }
		void OnResize(unsigned int width, unsigned int height);
		Math::Vec2f GetResolution()const;
		int ReadPixel(int x, int y);
		void Bind() const;
		void UnBind() const;
		
	
	private:
		void Invalidate(unsigned int depth);
	private:
		unsigned int m_LightFBO{};
		unsigned int m_LightDepthMaps{};
		//unsigned int m_MousePickMap{};
		unsigned int m_Width{};
		unsigned int m_Height{};
	};

	enum class UniformType
	{
		VEC2F,
		VEC3F,
		VEC4F,
		MATRIX_2_2,
		MATRIX_3_3,
		MATRIX_4_4
	};

	template<UniformType Type>
	class UniformBufferObject
	{
	public:
		UniformBufferObject(unsigned int max_size, unsigned int bind_point = 0);
		unsigned int GetUBO() const { return m_UBO; }
		unsigned int GetUniformTypeSize() const { return m_UniformTypeSize; }
		
	private:
		unsigned int m_UBO{};
		unsigned int m_MaxSize{};
		unsigned int m_BindingPoint{};
		unsigned int m_UniformTypeSize{};
	};

	class RenderTarget
	{
		
	public:
		RenderTarget(const RenderTargetSpecification& spec);

		RenderTarget(const Math::Vec2f& resolution = { 512.f, 512.f });
		//RenderTarget(const Math::Vec2f& resolution = { 512.f, 512.f }, Asset::Texture* tex = nullptr);
		RenderTarget(int x_res, int y_res);

		~RenderTarget();
		

		[[nodiscard]] int GetWidth() const { return m_Width; }
		[[nodiscard]] int GetHeight() const { return m_Height; }
		[[nodiscard]] int GetFrameBufferID() const { return m_FrameBufferID; }
		[[nodiscard]] int GetScreenFrameBufferID() const { return m_ScreenFrameBufferID; }
		[[nodiscard]] int GetRenderBufferID() const { return m_RenderBufferID; }
		//[[nodiscard]] uint64_t GetTargetTextureID() const { return m_Texture->GetTextureID(); }
		[[nodiscard]] uint64_t GetColorAttachmentID() const { return m_ColorAttachmentID; }
		//[[nodiscard]] Asset::Texture* GetTexture() const { return m_Texture; }

		[[nodiscard]] uint64_t GetScreenAttachmentID() const { return m_ScreenColorAttachmentID; }

		void Invalidate();

		void _Invalidate();

		void InvalidatePostProcessing();

		void Bind(unsigned int ID) const;

		void BindFrameBuffer(unsigned int FrameBufferID, unsigned int target)const;

		void BindRenderBuffer()const;

		void BindAndBlitToScreen();

		int ReadPixel(uint32_t attachmentIndex, int x, int y);

		

		void UnBind() const;

		void RenderSize(const Math::Vec2f& resolution = { 512, 512 });

		unsigned int& GetSamples() { return m_Samples; }
		void SetSamples(int new_Sample) 
		{
			if (new_Sample != m_Samples)
			{
				m_Samples = new_Sample;
				Invalidate();
			}
				
		}

		void OnResize(uint32_t width, uint32_t height);

		bool constexpr IsMultiSampled()const { return m_Samples > 1; }

	private:
	
		//Asset::Texture* m_Texture{};
		int m_Width{};
		int m_Height{};

		unsigned int m_Samples = 8;
		unsigned int m_FrameBufferID{};
		unsigned int m_RenderBufferID{};
		unsigned int m_ColorAttachmentID{};
		unsigned int m_ScreenFrameBufferID{};
		unsigned int m_ScreenColorAttachmentID{};

		std::vector<RenderTargetTextureSpecification> m_ColorAttachmentSpecifications;
		RenderTargetSpecification m_Specification;
		std::vector<uint32_t> m_ColorAttachments;
		//uint32_t m_DepthAttachment = 0;
	
	};


}

