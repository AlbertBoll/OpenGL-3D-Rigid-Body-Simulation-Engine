#include "gepch.h"
#include "RenderSystem.h"
#include"Scene/_SCene.h"
#include "Core/RenderTarget.h"
#include "Camera/EditorCamera.h"
#include "Assets/Shaders/Shader.h"
#include "Geometry/Geometry.h"
#include "BaseApp.h"

namespace GEngine
{
	//class _Entity;
	//using namespace Camera;

	void RenderSystem::MousePickPreRender(_Scene* scene, const _EditorCamera& camera, Shader* mouse_pick_shader)
	{

		auto& render_groups = scene->GetGroupEntities();

		for (auto& group : render_groups)
		{
			if (!group.empty())
			{
				auto& renderComp = group[0].GetComponent<RenderComponent>();
				mouse_pick_shader->SetUniform("u_view", camera.GetViewMatrix());
				mouse_pick_shader->SetUniform("u_projection", camera.GetProjection());

				for (auto& entity : group)
				{
					//auto view = scene->View<Transform3DComponent, MeshComponent>();
					mouse_pick_shader->SetUniform("u_EntityID", int((entt::entity)entity));
					auto geoComp = entity.GetComponent<MeshComponent>();
					geoComp.m_Geometry->BindVAO();

					if (entity.HasAllComponents<Transform3DComponent>())
					{
						auto& comp = entity.GetComponent<Transform3DComponent>();

						mouse_pick_shader->SetUniform("u_model", entity.GetComponent<Transform3DComponent>().GetTransform());
					}

					if (geoComp.m_Geometry->IsUsingIndexBuffer())
					{
						ElementsDraw((unsigned int)renderComp.RenderSettings.DrawStyle, geoComp.m_Geometry->GetIndicesCount());
					}
					else
					{
						ArraysDraw((unsigned int)renderComp.RenderSettings.DrawStyle, geoComp.m_Geometry->GetVerticesCount());
					}
				}
			}
		}
	}


	void RenderSystem::CascadedShadowPreRender(_Scene* scene)
	{
		auto& render_groups = scene->GetGroupEntities();

		for (auto& group : render_groups)
		{
			if (!group.empty())
			{
				if (group[0].HasAllComponents<PreRenderPassComponent>())
				{
					auto& renderComp = group[0].GetComponent<PreRenderPassComponent>();
					auto shader = renderComp.Shader;
					auto id = renderComp.Shader->GetHandle();
					shader->Bind();

					for (auto& entity : group)
					{
						//auto view = scene->View<Transform3DComponent, MeshComponent>();
						auto geoComp = entity.GetComponent<MeshComponent>();
						geoComp.m_Geometry->BindVAO();

						if (entity.HasAllComponents<Transform3DComponent>())
						{
							auto& comp = entity.GetComponent<Transform3DComponent>();

							shader->SetUniform("u_model", entity.GetComponent<Transform3DComponent>().GetTransform());
						}

						if (geoComp.m_Geometry->IsUsingIndexBuffer())
						{
							ElementsDraw((unsigned int)renderComp.RenderSettings.DrawStyle, geoComp.m_Geometry->GetIndicesCount());
						}
						else
						{
							ArraysDraw((unsigned int)renderComp.RenderSettings.DrawStyle, geoComp.m_Geometry->GetVerticesCount());
						}
					}
				}
			}
		}
	}

	
	void RenderSystem::CascadedShadowSceneRender(_Scene* scene, _EditorCamera& camera, const std::vector<float>& shadowCascadeLevels)
	{

		auto& render_groups = scene->GetGroupEntities();

		for (auto& group : render_groups)
		{
			if (!group.empty())
			{
				//auto geoComp = group[0].GetComponent<MeshComponent>();
				//auto& texturesComp = group[0].GetComponent<TexturesComponent>();
				auto& renderComp = group[0].GetComponent<RenderComponent>();
				auto shader = renderComp.Shader;
				auto id = renderComp.Shader->GetHandle();
				shader->Bind();
				shader->SetUniform("u_view", camera.GetViewMatrix());
				shader->SetUniform("u_projection", camera.GetProjection());
				shader->SetUniform("viewPos", camera.GetPosition());
				shader->SetUniform("farPlane", camera.GetFarClip());
				shader->SetUniform("cascadeCount", (int)shadowCascadeLevels.size());
				
				for (size_t i = 0; i < shadowCascadeLevels.size(); ++i)
				{
					std::string cascadePlaneDistances = "cascadePlaneDistances[" + std::to_string(i) + "]";
					shader->SetUniform(cascadePlaneDistances.c_str(), (float)shadowCascadeLevels[i]);
				}

				auto& lightEntities = scene->GetLightEntitiesWithRenderID(id);

				for (auto& light_entity : lightEntities)
				{
					if (light_entity.HasAllComponents<DirectionalLightComponent>())
					{
						auto it = light_entity.GetComponent<DirectionalLightComponent>();
						it.LoadUniforms(shader);
					}
					else if (light_entity.HasAllComponents<PointLightComponent>())
					{
						auto it = light_entity.GetComponent<PointLightComponent>();
						it.LoadUniforms(shader);
					}
					else if (light_entity.HasAllComponents<SpotLightComponent>())
					{
						auto it = light_entity.GetComponent<PointLightComponent>();
						it.LoadUniforms(shader);
					}
				}


				/*if (group[0].HasAllComponents<DirectionalLightComponent>())
				{
					auto& dirLightComp = group[0].GetComponent<DirectionalLightComponent>();
					dirLightComp.LoadUniforms(shader);
				}*/

				//auto geo = geoComp.m_Geometry;

				//shader->SetUniform(dirLightComp.ambient.Name.c_str(), camera.GetProjection());
				//texturesComp.BindTextures(shader);
				//geoComp.m_Geometry->BindVAO();

				for (auto& entity : group)
				{
					//auto view = scene->View<Transform3DComponent, MeshComponent>();
					auto geoComp = entity.GetComponent<MeshComponent>();
					geoComp.m_Geometry->BindVAO();
					shader->SetUniform("u_entityID", int((entt::entity)entity));
					if (entity.HasAllComponents<Transform3DComponent>())
					{
						auto& comp = entity.GetComponent<Transform3DComponent>();

						shader->SetUniform("u_model", entity.GetComponent<Transform3DComponent>().GetTransform());
					}

					if (entity.HasAllComponents<TexturesComponent>())
					{
						auto& it = entity.GetComponent<TexturesComponent>();
						it.BindTextures(shader);
						it.LoadUniforms(shader);
						UpdateRenderSetting(renderComp.RenderSettings.m_PrimitivesSetting.surfaceSetting);
					}

					/*if (entity.HasAllComponents<MaterialComponent>())
					{
						auto& it = entity.GetComponent<MaterialComponent>();
						shader->SetUniform(it.Reflectivity.Name.c_str(), it.Reflectivity.Data);

					}*/

					if (entity.HasAllComponents<HelperMaterialComponent>())
					{
						auto it = entity.GetComponent<HelperMaterialComponent>();

						shader->SetUniform(it.BaseColor.Name.c_str(), it.BaseColor.Data);
						shader->SetUniform(it.UsingVertexColor.Name.c_str(), it.UsingVertexColor.Data);
						UpdateRenderSetting(renderComp.RenderSettings.m_PrimitivesSetting.lineSetting);
					}
					//if (entity.HasAllComponents<MeshComponent>())
					//{
						//auto geo = entity.GetComponent<MeshComponent>().m_Geometry;
						//if (geo)
						//{
							//geo->BindVAO();

					if (geoComp.m_Geometry->IsUsingIndexBuffer())
					{
						ElementsDraw((unsigned int)renderComp.RenderSettings.DrawStyle, geoComp.m_Geometry->GetIndicesCount());
					}
					else
					{
						ArraysDraw((unsigned int)renderComp.RenderSettings.DrawStyle, geoComp.m_Geometry->GetVerticesCount());
					}
					//}



				}

			}

		}

	}



	void RenderSystem::SceneRender(_Scene* scene, _EditorCamera& camera)
	{
		
		auto& render_groups = scene->GetGroupEntities();

		for (auto& group : render_groups)
		{
			if (!group.empty())
			{
				//auto geoComp = group[0].GetComponent<MeshComponent>();
				//auto& texturesComp = group[0].GetComponent<TexturesComponent>();
				auto& renderComp = group[0].GetComponent<RenderComponent>();
				auto shader = renderComp.Shader;
				auto id = renderComp.Shader->GetHandle();
				shader->Bind();
				shader->SetUniform("u_view", camera.GetViewMatrix());
				shader->SetUniform("u_projection", camera.GetProjection());

				auto& lightEntities = scene->GetLightEntitiesWithRenderID(id);			
			
				for (auto& light_entity : lightEntities)
				{
					if (light_entity.HasAllComponents<DirectionalLightComponent>())
					{
						auto it = light_entity.GetComponent<DirectionalLightComponent>();
						it.LoadUniforms(shader);
					}
					else if (light_entity.HasAllComponents<PointLightComponent>())
					{
						auto it = light_entity.GetComponent<PointLightComponent>();
						it.LoadUniforms(shader);
					}
					else if (light_entity.HasAllComponents<SpotLightComponent>())
					{
						auto it = light_entity.GetComponent<PointLightComponent>();
						it.LoadUniforms(shader);
					}
				}
				

				/*if (group[0].HasAllComponents<DirectionalLightComponent>())
				{
					auto& dirLightComp = group[0].GetComponent<DirectionalLightComponent>();
					dirLightComp.LoadUniforms(shader);
				}*/
			
				//auto geo = geoComp.m_Geometry;
			
				//shader->SetUniform(dirLightComp.ambient.Name.c_str(), camera.GetProjection());
				//texturesComp.BindTextures(shader);
				//geoComp.m_Geometry->BindVAO();
			
				for (auto& entity : group)
				{
					//auto view = scene->View<Transform3DComponent, MeshComponent>();
					auto geoComp = entity.GetComponent<MeshComponent>();
					geoComp.m_Geometry->BindVAO();

					if (entity.HasAllComponents<Transform3DComponent>())
					{
						auto& comp = entity.GetComponent<Transform3DComponent>();
					
						shader->SetUniform("u_model", entity.GetComponent<Transform3DComponent>().GetTransform());
					}

					if (entity.HasAllComponents<TexturesComponent>())
					{
						auto& it = entity.GetComponent<TexturesComponent>();
						it.BindTextures(shader);
						it.LoadUniforms(shader);
						UpdateRenderSetting(renderComp.RenderSettings.m_PrimitivesSetting.surfaceSetting);
					}

					if (entity.HasAllComponents<MaterialComponent>())
					{
						auto& it = entity.GetComponent<MaterialComponent>();
						shader->SetUniform(it.Reflectivity.Name.c_str(), it.Reflectivity.Data);
						
					}

					if (entity.HasAllComponents<HelperMaterialComponent>())
					{
						auto it = entity.GetComponent<HelperMaterialComponent>();

						shader->SetUniform(it.BaseColor.Name.c_str(), it.BaseColor.Data);
						shader->SetUniform(it.UsingVertexColor.Name.c_str(), it.UsingVertexColor.Data);
						UpdateRenderSetting(renderComp.RenderSettings.m_PrimitivesSetting.lineSetting);
					}
					//if (entity.HasAllComponents<MeshComponent>())
					//{
						//auto geo = entity.GetComponent<MeshComponent>().m_Geometry;
						//if (geo)
						//{
							//geo->BindVAO();
					
					if (geoComp.m_Geometry->IsUsingIndexBuffer())
					{
						ElementsDraw((unsigned int)renderComp.RenderSettings.DrawStyle, geoComp.m_Geometry->GetIndicesCount());
					}
					else
					{
						ArraysDraw((unsigned int)renderComp.RenderSettings.DrawStyle, geoComp.m_Geometry->GetVerticesCount());
					}
						//}
						
						
				
				}

			}
			
		}
	}

	void RenderSystem::BeginRender(_EditorCamera& camera, RenderTarget* target)
	{
		if (!target)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, m_WindowWidth, m_WindowHeight);
		}

		else
		{
			glBindFramebuffer(GL_FRAMEBUFFER, target->GetFrameBufferID());
			glViewport(0, 0, target->GetWidth(), target->GetHeight());
		}


		camera.UpdateView();
	}

	void RenderSystem::BeginRender(_EditorCamera& camera, const Vec4f& color)
	{
		glClearColor(color.r, color.g, color.b, color.a);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glViewport(0, 0, camera.m_ViewportWidth, camera.m_ViewportHeight);
		camera.UpdateView();
	}

	void RenderSystem::Initialize(const Vec3f& clearColor)
	{
		GENGINE_CORE_INFO("OpenGL Info:\n  \t\t\t\t  Vendor: {}\n  \t\t\t        Renderer: {}\n  \t\t\t         Version: {}", (const char*)glGetString(GL_VENDOR),
			(const char*)glGetString(GL_RENDERER),
			(const char*)glGetString(GL_VERSION));
		//enable depth test
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);

		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
		//enable blending

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		//Enable multi-sampling
		glEnable(GL_MULTISAMPLE);


		glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
	}

	void RenderSystem::Set(const RenderParam_& param)
	{
		auto color = param.ClearColor;
		glClearColor(color.r, color.g, color.b, color.a);
		//glEnable(GL_DEPTH_TEST);
		if (param.bClearColorBit)   glClear(GL_COLOR_BUFFER_BIT);
		if (param.bClearDepthBit)   glClear(GL_DEPTH_BUFFER_BIT);
		//if (param.bClearStencilBit) glClear(GL_STENCIL_BUFFER_BIT);
		if (param.bEnableDepthTest)
			glEnable(GL_DEPTH_TEST);
		else 
			glDisable(GL_DEPTH_TEST);
		//glDepthFunc(GL_ALWAYS);
	}

	void RenderSystem::Clear(const Vec3f& clearColor)
	{
		glClearColor(clearColor.x, clearColor.y, clearColor.z, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	void RenderSystem::OnMouseClicked(_Scene* scene, const MousePickFrameBuffer& fb)
	{
		if (BaseApp::GetInputManager()->GetMouseState().isButtonPressed(GEngineMouseCode::GENGINE_BUTTON_LEFT))
		{
			auto mousePos = BaseApp::GetInputManager()->GetMouseState().m_MousePos;
			int x = mousePos.x;
			int y = m_WindowHeight - mousePos.y;
			//m_MousePickFrameBuffer->Bind();
			
			int pixel_data = fb.ReadPixel(x, y);
			std::string str = "None";
			auto entity = pixel_data == -1 ? _Entity() : _Entity((entt::entity)pixel_data, scene);
			if (entity)
			{
				str = entity.GetComponent<TagComponent>().Name;
			}
			else
			{
				str = "None";
			}
			std::cout << "Mouse Clicked at: " << x << ", " << y << std::endl;
			std::cout << "Entity " << str << " has been clicked" << std::endl;
			//m_MousePickFrameBuffer->UnBind();
		}
	}

	void RenderSystem::SkyBoxRender(_Entity skybox, _EditorCamera& camera)
	{
		glDepthFunc(GL_LEQUAL);
		auto& renderComp = skybox.GetComponent<RenderComponent>();
		auto shader = renderComp.Shader;
		shader->Bind();
		shader->SetUniform("u_projection", camera.GetProjection());
		shader->SetUniform("u_view", Mat4(Mat3(camera.GetViewMatrix())));
		shader->SetUniform("u_model", skybox.GetComponent<Transform3DComponent>().GetTransform());
		

		auto& skybox_geo = skybox.GetComponent<MeshComponent>().m_Geometry;
		skybox_geo->BindVAO();

		skybox.GetComponent<TexturesComponent>().BindTextures(shader);

		UpdateRenderSetting(renderComp.RenderSettings.m_PrimitivesSetting.surfaceSetting);

		if (skybox_geo->IsUsingIndexBuffer())
		{
			ElementsDraw((unsigned int)renderComp.RenderSettings.DrawStyle, skybox_geo->GetIndicesCount());
		}
		else
		{
			ArraysDraw((unsigned int)renderComp.RenderSettings.DrawStyle, skybox_geo->GetVerticesCount());
		}

		glDepthFunc(GL_LESS);

	}


	void RenderSystem::ArraysDraw(unsigned int mode, int count, int first)
	{
		glDrawArrays(mode, first, count);
		m_RenderStats.m_ArrayDrawCall++;
	}

	void RenderSystem::ElementsDraw(unsigned int mode, int count, unsigned int type, const void* indices)
	{
		glDrawElements(mode, count, type, indices);
		m_RenderStats.m_ElementsDrawCall++;
	}

	void RenderSystem::ElementsInstancedDraw(unsigned int mode, int count, int instancecount, unsigned int type, const void* indices)
	{
		glDrawElementsInstanced(mode, count, type, indices, instancecount);
		m_RenderStats.m_ElementsInstancedDrawCall++;
	}

	void RenderSystem::ArraysInstancedDraw(unsigned int mode, int count, int instancecount, int first)
	{
		glDrawArraysInstanced(mode, first, count, instancecount);
		m_RenderStats.m_ArrayInstancedDrawCall++;
	
	}

	std::vector<Vec4f> RenderSystem::GetFrustumCornersWorldSpace(const Mat4& projview)
	{
		const auto inv = glm::inverse(projview);

		std::vector<Vec4f> frustumCorners;
		for (unsigned int x = 0; x < 2; ++x)
		{
			for (unsigned int y = 0; y < 2; ++y)
			{
				for (unsigned int z = 0; z < 2; ++z)
				{
					const Vec4f pt = inv * Vec4f(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
					frustumCorners.push_back(pt / pt.w);
				}
			}
		}

		return frustumCorners;
	}

	std::vector<Vec4f> RenderSystem::GetFrustumCornersWorldSpace(const Mat4& proj, const Mat4& view)
	{
		return GetFrustumCornersWorldSpace(proj * view);
	}

	Mat4 RenderSystem::GetLightSpaceMatrix(const _EditorCamera& camera, const Vec3f& lightDir, float nearPlane, float farPlane)
	{
		Mat4 proj = glm::perspective(camera.GetFOV(), camera.GetAspectRatio(), nearPlane, farPlane);
		const auto corners = GetFrustumCornersWorldSpace(proj, camera.GetViewMatrix());

		Vec3f center = Vec3f(0, 0, 0);
		for (const auto& v : corners)
		{
			center += Vec3f(v);
		}
		center /= corners.size();

		const auto lightView = glm::lookAt(center + lightDir, center, Vec3f(0.0f, 1.0f, 0.0f));

		float minX = std::numeric_limits<float>::max();
		float maxX = std::numeric_limits<float>::lowest();
		float minY = std::numeric_limits<float>::max();
		float maxY = std::numeric_limits<float>::lowest();
		float minZ = std::numeric_limits<float>::max();
		float maxZ = std::numeric_limits<float>::lowest();
		for (const auto& v : corners)
		{
			const auto trf = lightView * v;
			minX = std::min(minX, trf.x);
			maxX = std::max(maxX, trf.x);
			minY = std::min(minY, trf.y);
			maxY = std::max(maxY, trf.y);
			minZ = std::min(minZ, trf.z);
			maxZ = std::max(maxZ, trf.z);
		}

		// Tune this parameter according to the scene
		constexpr float zMult = 10.0f;
		if (minZ < 0)
		{
			minZ *= zMult;
		}
		else
		{
			minZ /= zMult;
		}
		if (maxZ < 0)
		{
			maxZ /= zMult;
		}
		else
		{
			maxZ *= zMult;
		}

		Mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
		return lightProjection * lightView;
	}

	std::vector<Mat4> RenderSystem::GetLightSpaceMatrices(const _EditorCamera& camera, const Vec3f& lightDir, const std::vector<float>& shadowCascadeLevels)
	{
		float cameraNearPlane = camera.GetNearClip();
		float cameraFarPlane = camera.GetFarClip();
		std::vector<Mat4> ret;
		for (size_t i = 0; i < shadowCascadeLevels.size() + 1; ++i)
		{
			if (i == 0)
			{
				ret.push_back(GetLightSpaceMatrix(camera, lightDir, cameraNearPlane, shadowCascadeLevels[i]));
			}
			else if (i < shadowCascadeLevels.size())
			{
				ret.push_back(GetLightSpaceMatrix(camera, lightDir, shadowCascadeLevels[i - 1], shadowCascadeLevels[i]));
			}
			else
			{
				ret.push_back(GetLightSpaceMatrix(camera, lightDir, shadowCascadeLevels[i - 1], cameraFarPlane));
			}
		}
		return ret;
	}


	void RenderSystem::UpdateRenderSetting(const RenderSetting_& renderSetting)
	{
		if (renderSetting.m_PrimitivesSetting.surfaceSetting.bDoubleSide)
		{
			glDisable(GL_CULL_FACE);
		}

		else
		{
			glEnable(GL_CULL_FACE);
		}

		if (renderSetting.m_PrimitivesSetting.surfaceSetting.bWireFrame)
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}

		else
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}

		glLineWidth(renderSetting.m_PrimitivesSetting.surfaceSetting.lineWidth);
	
	}

	void RenderSystem::UpdateRenderSetting(const PointSetting_& pointSetting)
	{

	}

	void RenderSystem::UpdateRenderSetting(const LineSetting_& lineSetting)
	{
		glEnable(GL_LINE_SMOOTH);
		glLineWidth(lineSetting.lineWidth);
	}

	//template<UniformType Type>
	template<typename uniformbuffer>
	void RenderSystem::SetupUBO(const uniformbuffer& ubo, const _EditorCamera& camera, const Vec3f& lightDir, const std::vector<float>& shadowCascadeLevels)
	{
		const auto lightMatrices = GetLightSpaceMatrices(camera, lightDir, shadowCascadeLevels);
		glBindBuffer(GL_UNIFORM_BUFFER, ubo.GetUBO());
		for (size_t i = 0; i < lightMatrices.size(); ++i)
		{
			glBufferSubData(GL_UNIFORM_BUFFER, i * ubo.GetUniformTypeSize(), ubo.GetUniformTypeSize(), &lightMatrices[i]);
		}
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

	}

	template void RenderSystem::SetupUBO(const UniformBufferObject<UniformType::MATRIX_4_4>&, const _EditorCamera&, const Vec3f&, const std::vector<float>&);
	template void RenderSystem::SetupUBO(const UniformBufferObject<UniformType::MATRIX_3_3>&, const _EditorCamera&, const Vec3f&, const std::vector<float>&);
	template void RenderSystem::SetupUBO(const UniformBufferObject<UniformType::MATRIX_2_2>&, const _EditorCamera&, const Vec3f&, const std::vector<float>&);
	template void RenderSystem::SetupUBO(const UniformBufferObject<UniformType::VEC4F>&, const _EditorCamera&, const Vec3f&, const std::vector<float>&);
	template void RenderSystem::SetupUBO(const UniformBufferObject<UniformType::VEC3F>&, const _EditorCamera&, const Vec3f&, const std::vector<float>&);
	template void RenderSystem::SetupUBO(const UniformBufferObject<UniformType::VEC2F>&, const _EditorCamera&, const Vec3f&, const std::vector<float>&);


	void RenderSystem::UpdateRenderSetting(const SurfaceSetting_& surfaceSetting)
	{
		if (surfaceSetting.bDoubleSide)
		{
			glDisable(GL_CULL_FACE);
		}

		else
		{
			glEnable(GL_CULL_FACE);
		}

		if (surfaceSetting.bWireFrame)
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}

		else
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}

		glLineWidth(surfaceSetting.lineWidth);
	}

	void RenderSystem::CascadedShadowPass(_Scene* scene, Shader* depth_shader, const CascadeShadowFrameBuffer& fb)
	{
		depth_shader->Bind();
		fb.Bind();
		//glBindFramebuffer(GL_FRAMEBUFFER, fb.GetLightFBO());
		glViewport(0, 0, fb.GetResolution().x, fb.GetResolution().y);
		glClear(GL_DEPTH_BUFFER_BIT);
		glCullFace(GL_FRONT);  // peter panning
		CascadedShadowPreRender(scene);
		glCullFace(GL_BACK);
		fb.UnBind();
		//glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void RenderSystem::CascadedShadowScenePass(_Scene* scene, _EditorCamera& camera, Shader* cascade_shader, const std::vector<float>& shadowCascadeLevels, const FinalFrameBuffer& fb)
	{
		cascade_shader->Bind();
		fb.Bind();
		glViewport(0, 0, fb.GetResolution().x, fb.GetResolution().y);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		fb.ClearMousePickAttachment(-1); // Clear the color attachment to -1
		CascadedShadowSceneRender(scene, camera, shadowCascadeLevels);
		fb.UnBind();

	}

	void RenderSystem::MousePickPass(_Scene* scene, const _EditorCamera& camera, Shader* mouse_pick_shader, const MousePickFrameBuffer& fb)
	{
		mouse_pick_shader->Bind();
		fb.Bind();
		glViewport(0, 0, fb.GetResolution().x, fb.GetResolution().y);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		fb.ClearAttachment(0, -1); // Clear the color attachment to -1
		//glDisable(GL_DEPTH_TEST);
		//glEnable(GL_DEPTH_TEST);
		MousePickPreRender(scene, camera, mouse_pick_shader);
		//;
		
		OnMouseClicked(scene, fb);
		
		fb.UnBind();
		
	}

}