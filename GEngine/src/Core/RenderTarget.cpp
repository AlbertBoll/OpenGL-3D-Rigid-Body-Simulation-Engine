#include "gepch.h"
#include "Core/RenderTarget.h"

namespace GEngine
{
	static constexpr uint32_t s_MaxFramebufferSize = 8192;

	namespace Utils {

		static constexpr GLenum TextureTarget(bool multisampled)
		{
			return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
		}

		static void CreateTextures(bool multisampled, uint32_t* outID, uint32_t count)
		{
			glCreateTextures(TextureTarget(multisampled), count, outID);
		}

		static void BindTexture(bool multisampled, uint32_t id)
		{
			glBindTexture(TextureTarget(multisampled), id);
		}

		static void AttachColorTexture(uint32_t id, int samples, GLenum internalFormat, GLenum format, uint32_t width, uint32_t height, int index)
		{
			

			bool multisampled = samples > 1;

			auto textureTarget = TextureTarget(multisampled);

			if (multisampled)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, internalFormat, width, height, GL_TRUE);
			}
			else
			{
				glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, nullptr);
			}

			//check if hardware support anisotropic filtering
			if (GLAD_GL_EXT_texture_filter_anisotropic)
			{
				GLfloat largest;
				glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest);

				//activate anisotropic filtering
				glTexParameterf(textureTarget, GL_TEXTURE_MAX_ANISOTROPY_EXT, largest);
			}

			glTexParameteri(textureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(textureTarget, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
			glTexParameteri(textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, textureTarget, id, 0);
		}

		static void AttachDepthTexture(uint32_t id, int samples, GLenum format, GLenum attachmentType, uint32_t width, uint32_t height)
		{
			bool multisampled = samples > 1;

			auto textureTarget = TextureTarget(multisampled);

			if (multisampled)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, width, height, GL_TRUE);
			}
			else
			{
				glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);

				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}

			glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentType, TextureTarget(multisampled), id, 0);
		}

		static constexpr bool IsDepthFormat(RenderTargetTextureFormat format)
		{
			switch (format)
			{
			case RenderTargetTextureFormat::DEPTH24STENCIL8:  return true;
			}

			return false;
		}

		static constexpr GLenum FBTextureFormatToGL(RenderTargetTextureFormat format)
		{
			switch (format)
			{
			case RenderTargetTextureFormat::RGBA8:       return GL_RGBA8;
			case RenderTargetTextureFormat::RED_INTEGER: return GL_RED_INTEGER;
			}

			ASSERT(false);
			return 0;
		}

	}


	RenderTarget::~RenderTarget()
	{
		//if (m_Texture) delete m_Texture;
		
		glDeleteFramebuffers(1, &m_FrameBufferID);
		glDeleteRenderbuffers(1, &m_RenderBufferID);
		glDeleteTextures(1, &m_ColorAttachmentID);

		//GENGINE_CORE_INFO("Render Targer destructor was called.");

	}




	RenderTarget::RenderTarget(const RenderTargetSpecification& spec): m_Specification(spec)
	{





	}



	RenderTarget::RenderTarget(const Math::Vec2f& resolution): m_Width{(int)resolution.x}, m_Height{(int)resolution.y}
	{
		Invalidate();
	}



	RenderTarget::RenderTarget(int x_res, int y_res):m_Width(x_res), m_Height(y_res)
	{
		Invalidate();
	}

	void RenderTarget::Invalidate()
	{
		if (m_FrameBufferID)
		{
			//delete m_Texture;
			glDeleteFramebuffers(1, &m_FrameBufferID);
			glDeleteRenderbuffers(1, &m_RenderBufferID);
			glDeleteTextures(1, &m_ColorAttachmentID);
		}

	    //m_Texture = new Asset::Texture(info);
		
		bool multisampled = m_Samples > 1;
		int textureMode = multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

		// Create a frame buffer
		glGenFramebuffers(1, &m_FrameBufferID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBufferID);
		glGenTextures(1, &m_ColorAttachmentID);
		glBindTexture(textureMode, m_ColorAttachmentID);


		if (multisampled)
		{
			glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_Samples, GL_RGBA8, m_Width, m_Height, GL_TRUE);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, m_ColorAttachmentID, 0);
		}

		else
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_ColorAttachmentID, 0);
		}

		//check if hardware support anisotropic filtering
		if (GLAD_GL_EXT_texture_filter_anisotropic)
		{
			GLfloat largest;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest);

			//activate anisotropic filtering
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, largest);
		}

		glTexParameteri(textureMode, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(textureMode, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(textureMode, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(textureMode, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenRenderbuffers(1, &m_RenderBufferID);
		glBindRenderbuffer(GL_RENDERBUFFER, m_RenderBufferID);

		if(multisampled) glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_Samples, GL_DEPTH24_STENCIL8, m_Width, m_Height);
		else glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_Width, m_Height);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_RenderBufferID);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		//Assert the frame buffer created successfully
		ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Framebuffer status error");
		//UnBind();

		if (multisampled) InvalidatePostProcessing();

	}


	void RenderTarget::_Invalidate()
	{

		if (m_FrameBufferID)
		{
			glDeleteFramebuffers(1, &m_FrameBufferID);

			glDeleteTextures((int)m_ColorAttachments.size(), m_ColorAttachments.data());

			m_ColorAttachments.clear();

			glDeleteRenderbuffers(1, &m_RenderBufferID);
		}

		// Create a frame buffer
		glGenFramebuffers(1, &m_FrameBufferID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBufferID);

		bool multisampled = m_Samples > 1;

		// Attachments
		if (m_ColorAttachmentSpecifications.size())
		{
			m_ColorAttachments.resize(m_ColorAttachmentSpecifications.size());
			Utils::CreateTextures(multisampled, m_ColorAttachments.data(), (uint32_t)m_ColorAttachments.size());

			for (int i = 0; i < m_ColorAttachments.size(); i++)
			{
				Utils::BindTexture(multisampled, m_ColorAttachments[i]);
				switch (m_ColorAttachmentSpecifications[i].TextureFormat)
				{
				case RenderTargetTextureFormat::RGBA8:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples, GL_RGBA8, GL_RGBA, m_Specification.Width, m_Specification.Height, i);
					break;
				case RenderTargetTextureFormat::RED_INTEGER:
					Utils::AttachColorTexture(m_ColorAttachments[i], m_Specification.Samples, GL_R32I, GL_RED_INTEGER, m_Specification.Width, m_Specification.Height, i);
					break;
				}
			}
		}

		glGenRenderbuffers(1, &m_RenderBufferID);
		glBindRenderbuffer(GL_RENDERBUFFER, m_RenderBufferID);

		if (multisampled) glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_Specification.Samples, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height);
		else glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_RenderBufferID);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		//Assert the frame buffer created successfully
		ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Framebuffer status error");
		//UnBind();

		if (multisampled) InvalidatePostProcessing();


	}


	void RenderTarget::InvalidatePostProcessing()
	{
		if (m_ScreenFrameBufferID)
		{
			glDeleteFramebuffers(1, &m_ScreenFrameBufferID);
			glDeleteTextures(1, &m_ScreenColorAttachmentID);
		}

		glGenFramebuffers(1, &m_ScreenFrameBufferID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_ScreenFrameBufferID);
	
		glGenTextures(1, &m_ScreenColorAttachmentID);
		glBindTexture(GL_TEXTURE_2D, m_ScreenColorAttachmentID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_Width, m_Height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ScreenColorAttachmentID, 0);	// we only need a color buffer

		ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Framebuffer status error");
		//UnBind();

	}

	void RenderTarget::Bind(unsigned int ID) const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, ID);
		//glBindRenderbuffer(GL_RENDERBUFFER, m_RenderBufferID);
	}

	//void RenderTarget::BindFrameBuffer() const
	//{
	//	glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBufferID);
	//}

	void RenderTarget::BindFrameBuffer(unsigned int FrameBufferID, unsigned int target) const
	{
		glBindFramebuffer(target, FrameBufferID);
	}

	void RenderTarget::BindRenderBuffer() const
	{
		glBindRenderbuffer(GL_RENDERBUFFER, m_RenderBufferID);
	}

	void RenderTarget::BindAndBlitToScreen()
	{
		//BindFrameBuffer(m_FrameBufferID, GL_READ_FRAMEBUFFER);
		//BindFrameBuffer(m_ScreenFrameBufferID, GL_DRAW_FRAMEBUFFER);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FrameBufferID);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ScreenFrameBufferID);
		glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	int RenderTarget::ReadPixel(uint32_t attachmentIndex, int x, int y)
	{
		ASSERT(attachmentIndex < m_ColorAttachments.size());
		glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
		int pixelData;
		glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
		return pixelData;

	}

	void RenderTarget::UnBind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		//glBindRenderbuffer(GL_RENDERBUFFER, 0);
	}
	
	
	void RenderTarget::RenderSize(const Math::Vec2f& resolution)
	{
		
		m_Width = static_cast<int>(resolution.x);
		m_Height = static_cast<int>(resolution.y);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_Width, m_Height);
		
		//glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_Width, m_Height);
	}

	void RenderTarget::OnResize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0 || width > s_MaxFramebufferSize || height > s_MaxFramebufferSize)
		{
			GENGINE_CORE_WARN("Attempted to resize framebuffer to {0}, {1}", width, height);
			return;
		}

		m_Width = width;
		m_Height = height;
		Invalidate();
	}

	CascadeShadowFrameBuffer::CascadeShadowFrameBuffer(unsigned int resolution_x, unsigned int resolution_y, unsigned int depth) : m_Width(resolution_x), m_Height(resolution_y)
	{
		Invalidate(depth);
	}

	void CascadeShadowFrameBuffer::Invalidate(unsigned int depth)
	{
		if (m_LightFBO)
		{
			//delete m_Texture;
			glDeleteFramebuffers(1, &m_LightFBO);
			glDeleteTextures(1, &m_LightDepthMaps);
			//glDeleteTextures(1, &m_MousePickMap);
		}

		// Create a frame buffer
		glGenFramebuffers(1, &m_LightFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_LightFBO);
		

		glGenTextures(1, &m_LightDepthMaps);
		glBindTexture(GL_TEXTURE_2D_ARRAY, m_LightDepthMaps);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, m_Width, m_Height, depth + 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

		constexpr float bordercolor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, bordercolor);

		glBindFramebuffer(GL_FRAMEBUFFER, m_LightFBO);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_LightDepthMaps, 0);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);

		//glGenTextures(1, &m_MousePickMap);
		//glBindTexture(GL_TEXTURE_2D, m_MousePickMap);

		//glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, 1280, 720, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		//glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_MousePickMap, 0);	// we only need a color buffer
		//GLenum buffers[1] = { GL_COLOR_ATTACHMENT0};
		//glDrawBuffers(1, buffers);

		int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);


	}

	void CascadeShadowFrameBuffer::OnResize(unsigned int width, unsigned int height)
	{
		m_Width = width;
		m_Height = height;
		Invalidate(5);
	}

	Math::Vec2f CascadeShadowFrameBuffer::GetResolution() const
	{
		return Math::Vec2f{ m_Width, m_Height };
	}

	int CascadeShadowFrameBuffer::ReadPixel(int x, int y)
	{
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		int pixelData;
		glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
		return pixelData;	
	}

	void CascadeShadowFrameBuffer::Bind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_LightFBO); 
	}

	void CascadeShadowFrameBuffer::UnBind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0); 
	}
	


	template<UniformType Type>
	UniformBufferObject<Type>::UniformBufferObject(unsigned int max_size, unsigned int bind_point)
		: m_MaxSize(max_size), m_BindingPoint(bind_point)
	{
		using namespace Math;
		if constexpr (Type == UniformType::VEC2F)
		{
			m_UniformTypeSize = sizeof(Vec2f);
		}
		else if constexpr (Type == UniformType::VEC3F)
		{
			m_UniformTypeSize = sizeof(Vec3f);
		}
		else if constexpr (Type == UniformType::VEC4F)
		{
			m_UniformTypeSize = sizeof(Vec4f);
		}
		else if constexpr (Type == UniformType::MATRIX_2_2)
		{
			m_UniformTypeSize = sizeof(Mat2);
		}
		else if constexpr (Type == UniformType::MATRIX_3_3)
		{
			m_UniformTypeSize = sizeof(Mat3);
		}
		else if constexpr (Type == UniformType::MATRIX_4_4)
		{
			m_UniformTypeSize = sizeof(Mat4);
		}
		glGenBuffers(1, &m_UBO);
		glBindBuffer(GL_UNIFORM_BUFFER, m_UBO);
		glBufferData(GL_UNIFORM_BUFFER, m_UniformTypeSize * m_MaxSize, nullptr, GL_STATIC_DRAW);
		glBindBufferBase(GL_UNIFORM_BUFFER, m_BindingPoint, m_UBO);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}

	template class UniformBufferObject<UniformType::VEC2F>;
	template class UniformBufferObject<UniformType::VEC3F>;
	template class UniformBufferObject<UniformType::VEC4F>;
	template class UniformBufferObject<UniformType::MATRIX_2_2>;
	template class UniformBufferObject<UniformType::MATRIX_3_3>;
	template class UniformBufferObject<UniformType::MATRIX_4_4>;


	MousePickFrameBuffer::MousePickFrameBuffer(unsigned int resolution_x, unsigned int resolution_y)
	{
		m_Width = resolution_x;
		m_Height = resolution_y;
		Invalidate();
	}

	void MousePickFrameBuffer::OnResize(unsigned int width, unsigned int height)
	{
		m_Width = width;
		m_Height = height;
		Invalidate();
	}

	Math::Vec2f MousePickFrameBuffer::GetResolution() const
	{
		return { static_cast<float>(m_Width), static_cast<float>(m_Height) };
	}

	int MousePickFrameBuffer::ReadPixel(int x, int y)const
	{
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		int pixelData;
		glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
		return pixelData;
	}

	

	void MousePickFrameBuffer::Bind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_MousePickFBO);
		//glViewport(0, 0, m_Width, m_Height);
	}

	void MousePickFrameBuffer::UnBind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void MousePickFrameBuffer::ClearAttachment(uint32_t attachmentindex, int value) const
	{
		glClearTexImage(m_MousePickColorMap, 0, GL_RED_INTEGER, GL_INT, &value);
	}

	void MousePickFrameBuffer::Invalidate()
	{
		if (m_MousePickFBO)
		{
			//delete m_Texture;
			glDeleteFramebuffers(1, &m_MousePickFBO);
			glDeleteTextures(1, &m_MousePickColorMap);
			glDeleteTextures(1, &m_MousePickDepthMap);
		}

		// Create a frame buffer
		glGenFramebuffers(1, &m_MousePickFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_MousePickFBO);

		glGenTextures(1, &m_MousePickColorMap);
		glBindTexture(GL_TEXTURE_2D, m_MousePickColorMap);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, m_Width, m_Width, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_MousePickColorMap, 0);	// we only need a color buffer
		GLenum buffers[1] = { GL_COLOR_ATTACHMENT0};
		glDrawBuffers(1, buffers);

		glGenTextures(1, &m_MousePickDepthMap);
		glBindTexture(GL_TEXTURE_2D, m_MousePickDepthMap);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_Width, m_Height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_MousePickDepthMap, 0);	// we only need a depth buffer

		int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

	}

	FinalFrameBuffer::FinalFrameBuffer(unsigned int resolution_x, unsigned int resolution_y)
	{
		m_Width = resolution_x;
		m_Height = resolution_y;
		Invalidate();

	}

	void FinalFrameBuffer::OnResize(unsigned int width, unsigned int height)
	{
		m_Width = width;
		m_Height = height;
		Invalidate();
	}

	Math::Vec2f FinalFrameBuffer::GetResolution() const
	{
		return { m_Width, m_Height };
	}

	int FinalFrameBuffer::ReadPixel(int x, int y) const
	{
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		int pixelData;
		glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
		return pixelData;
	}

	void FinalFrameBuffer::Bind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glViewport(0, 0, m_Width, m_Height);
	}

	void FinalFrameBuffer::UnBind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void FinalFrameBuffer::BindReadFrameBuffer() const
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FBO);
	}

	void FinalFrameBuffer::BindDefaultDrawFrameBuffer() const
	{
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	}

	void FinalFrameBuffer::ClearMousePickAttachment(int value) const
	{
		glClearTexImage(m_MousePickMap, 0, GL_RED_INTEGER, GL_INT, &value);
	}

	void FinalFrameBuffer::BlitFrameBuffer() const
	{
		
		glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void FinalFrameBuffer::Invalidate()
	{
		if (m_FBO)
		{
			//delete m_Texture;
			glDeleteFramebuffers(1, &m_FBO);
			glDeleteTextures(1, &m_MousePickMap);
			glDeleteTextures(1, &m_DepthMap);
			glDeleteTextures(1, &m_ColorMap);
		}

		// Create a frame buffer
		glGenFramebuffers(1, &m_FBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

		glGenTextures(1, &m_ColorMap);
		glBindTexture(GL_TEXTURE_2D, m_ColorMap);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Width, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorMap, 0);	// we need a color buffer

		glGenTextures(1, &m_MousePickMap);
		glBindTexture(GL_TEXTURE_2D, m_MousePickMap);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, m_Width, m_Width, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_MousePickMap, 0);	// we need a mouse pick buffer

		GLenum buffers[2] = { GL_COLOR_ATTACHMENT0,  GL_COLOR_ATTACHMENT1};
		glDrawBuffers(2, buffers);

		glGenTextures(1, &m_DepthMap);
		glBindTexture(GL_TEXTURE_2D, m_DepthMap);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_Width, m_Height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthMap, 0);	// we only need a depth buffer

		int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

}
