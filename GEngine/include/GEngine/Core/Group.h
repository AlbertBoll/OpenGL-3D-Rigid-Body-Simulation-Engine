#pragma once

#include"Actor.h"
#include <vector>
#include <string>
#include <Geometry/Geometry.h>
#include"Camera/Camera.h"
#include "Renderer.h"
//#include "Assets/Shaders/Shader.h"
//#include "Managers/ShaderManager.h"

namespace GEngine
{

	template<typename T, typename = std::enable_if_t<std::is_base_of_v<Actor, T>>>
	class Group: public Actor
	{
		
		typedef typename std::vector<T*> Collections;
		typedef typename Collections::iterator iterator;
		typedef typename Collections::const_iterator const_iterator;

	public:
		Group() = default;
		~Group() override
		{
			for (auto ptr : m_Group)
			{
				delete ptr;
			}
		}

		Group(Geometry* geo, Material* mat): m_Geometry(geo), m_Material(mat){}

		Group(std::vector<T*> objects) : Actor(), m_Group(std::move(objects))
		{

		}

		void Push(const T& object) { m_Group.emplace_back(&object); }
		void Push(T* object) { m_Group.push_back(object); }
		//void Push(T&& object) { m_Group.emplace_back(std::move(object)); }

		void Push(const std::vector<T*>& objects) { m_Group.insert(m_Group.end(), objects.begin(), objects.end()); }

		void Remove(T* object)
		{
			m_Group.erase(std::remove(m_Group.begin(), m_Group.end(), object), m_Group.end());
		}

		void Render(CameraBase* camera) override
		{
		
			
			auto is_ArraysDraw = bool_variant(m_Material->GetRenderSetting().m_RenderMode == RenderMode::Arrays);
			auto is_ElementDraw = bool_variant(m_Material->GetRenderSetting().m_RenderMode == RenderMode::Elements);

			//m_Material->UseProgram();
			m_Geometry->BindVAO();
			//m_Material->SetUniforms<Mat4>({ {"u_view", camera->GetView()},
			//									   {"u_projection", camera->GetProjection()}});

			//m_Material->SetUniforms<Vec3f>({ {"u_cameraPosition", camera->GetWorldPosition()} });
			m_Material->BindTextureUniforms();

			m_Material->UpdateRenderSettings();
			m_Material->UploadUniforms();

			for (auto obj : m_Group)
			{
				auto is_visible = bool_variant(obj->GetVisibility());
				//obj->Render(camera);
				std::visit([&](auto b_visible, auto b_ArrayDraw, auto b_ElementDraw)
					{
						if constexpr (b_visible)
						{
						
							obj->SetUniforms<Mat4>({ {"u_model", obj->GetWorldTransform()} });
							obj->SetUniforms<Vec2f>({ {"u_offset", obj->GetTextureOffset()} });

							if constexpr (b_ArrayDraw) obj->ArraysDraw();
							else if constexpr (b_ElementDraw) obj->ElementsDraw();
						}

				    }, is_visible, is_ArraysDraw, is_ElementDraw);

				Renderer::RenderHelper(obj, camera);

			}


		}

		T* operator[](int i) { return m_Group[i]; }
		inline iterator begin() noexcept { return  m_Group.begin(); }
		inline const_iterator cbegin() const noexcept { return  m_Group.cbegin(); }
		inline iterator end() noexcept { return  m_Group.end(); }
		inline const_iterator cend() const noexcept { return  m_Group.cend(); }

		void SetMaterial(Material* mat) { m_Material = mat; }
		void SetGeometry(Geometry* geo) { m_Geometry = geo; }

		unsigned int GetProgramID()const
		{
			return m_Material->GetShaderID();
		}

		Collections& GetCollections() { return m_Group; }

		Material* GetMaterial()const { return m_Material; }
		/*void SetShader(const std::initializer_list<std::string>& files)
		{
			using namespace GEngine::Manager;
			const ShaderManager::Files _files{ files };
			m_Shader = ShaderManager::GetShaderProgram(_files);
		}*/

	private:
		Collections m_Group;
		Material* m_Material{};
		Geometry* m_Geometry{};
	};
}
