#include "SceneHierarchyPanel.h"
#include <GEngine/Core/Scene.h>
#include <imgui/imgui.h>
#include<glm/gtc/type_ptr.hpp>
#include<GEngine/Camera/Camera.h>
#include<GEngine/Camera/PerspectiveCamera.h>
#include <GEngine/Core/BaseApp.h>
#include <GEngine/Extras/CameraRig.h>
#include <GEngine/Core/Log.h>
#include <GEngine/Scene/BoxEntity.h>
#include <external/imgui/imgui_internal.h>
#include<GEngine/Math/Matrix.h>
#include <GEngine/Core/Group.h>

namespace GEngine
{



	SceneHierarchyPanel::SceneHierarchyPanel(BaseApp* app, Scene* context) : m_App{app}, m_Context(context)
	{

	}

	void SceneHierarchyPanel::SetContext(Scene* context)
	{
		m_Context = context;
	}

	void SceneHierarchyPanel::OnImGuiRender()
	{
		ImGui::Begin("Scene Hierarchy");

		for (auto p : m_Context->GetChildrenRef())
		{
			/*auto& tag = p->GetTag();
			ImGui::Separator();
			ImGui::Text("%s", tag.c_str());
			ImGui::Separator();*/
		
			if (auto it = dynamic_cast<Group<Entity>*>(p); it != nullptr)
			{
				auto& collection = it->GetCollections();
				for (auto entity : collection)
				{
					DrawEntityNode(entity);
				}
			}

			else
			{
				DrawEntityNode(p);
			}
			
		}

		if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(0))
		{
			m_SelectedContext = nullptr;
		}

		ImGui::End();

		ImGui::Begin("Properties");
		if (m_SelectedContext)
		{
			DrawComponents(m_SelectedContext);
		}
		ImGui::End();
		//ImGui::ShowDemoWindow();
	}

	template<typename CallBack>
	static void DragVec3Control(const std::string& label, Vec3f& value, const CallBack& ValueChangefunc, float resetValue = 0.f, float min = 0.f, float max = 0.f, float vel = 0.03f, float columnWidth = 100.f)
	{

		auto& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];
		//Vec3f degree = { ToDegrees(value.x), ToDegrees(value.y), ToDegrees(value.z) };
		ImGui::PushID(label.c_str());
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columnWidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();
		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
		float lightHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;

		ImVec2 buttonSize = { lightHeight + 3.0f, lightHeight };

		ImGui::PushStyleColor(ImGuiCol_Button, { 0.8f, 0.1f, 0.15f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.9f, 0.2f, 0.2f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.8f, 0.1f, 0.15f, 1.0f });

		ImGui::PushFont(boldFont);
		if (ImGui::Button("X", buttonSize)) { value.x = resetValue; ValueChangefunc();  }
		ImGui::PopFont();
		ImGui::PopStyleColor(3);
		ImGui::SameLine();
		if (ImGui::DragFloat("##X", &value.x, vel, min, max, "%.2f"))
		{
			ValueChangefunc();
		}

		ImGui::PopItemWidth();
		ImGui::SameLine();


		ImGui::PushStyleColor(ImGuiCol_Button, { 0.1f, 0.25f, 0.8f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.2f, 0.35f, 0.9f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.1f, 0.25f, 0.8f, 1.0f });


		ImGui::PushFont(boldFont);
		if (ImGui::Button("Y", buttonSize)) { value.y = resetValue; ValueChangefunc();  }
		ImGui::PopFont();
		ImGui::PopStyleColor(3);
		ImGui::SameLine();


		if (ImGui::DragFloat("##Y", &value.y, vel, min, max, "%.2f"))
		{
			ValueChangefunc();
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();


		ImGui::PushStyleColor(ImGuiCol_Button, { 0.2f, 0.7f, 0.3f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.3f, 0.8f, 0.3f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.2f, 0.7f, 0.2f, 1.0f });

		ImGui::PushFont(boldFont);
		if (ImGui::Button("Z", buttonSize)) { value.z = resetValue; ValueChangefunc();}
		ImGui::PopFont();
		ImGui::PopStyleColor(3);
		ImGui::SameLine();
		if (ImGui::DragFloat("##Z", &value.z, vel, min, max, "%.2f"))
		{
			ValueChangefunc();
		}

		//value = { ToRadians(degree.x), ToRadians(degree.y), ToRadians(degree.z) };
		ImGui::PopItemWidth();
		//ImGui::SameLine();
		ImGui::PopStyleVar();
		ImGui::Columns(1);

		ImGui::PopID();
	}

	template<typename T, typename UIFunction>
	static void DrawComponent(const std::string& name, Actor* actor, UIFunction func)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiButtonFlags_AllowItemOverlap;
		if (auto it = dynamic_cast<T*>(actor))
		{
			bool opened = ImGui::TreeNodeEx((void*)(uint64_t)actor, flags, name);
		}

	}

	void SceneHierarchyPanel::DrawComponents(Actor* actor)
	{
		auto& tag = actor->GetTag();
		char buffer[256];

		memset(buffer, 0, sizeof(buffer));
		strcpy_s(buffer, tag.c_str());

		if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
		{
			tag = std::string(buffer);

		}

		
		//auto& Localtransform = actor-> GetLocalTransform();
		//auto& Globaltransform = actor->GetGlobalTransform();

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
		ImGui::Separator();
		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)actor->GetID(), flags, "Transform");

		if (opened)
		{
			auto& update_tranlation = actor->GetTranslation();
			auto translationFunc = [&]() {actor->SetPosition(actor->GetTranslation()); };
			DragVec3Control("Translation", update_tranlation, translationFunc);

			
			auto& update_rotation = actor->GetRotation();
			auto rotationFunc = [&]()
			{
				using namespace Math;

				actor->GetLocalTransform() = Matrix::CalTransformation(actor->GetTranslation(), actor->GetRotation(), actor->GetScale());
				
			};

			DragVec3Control("Rotation", update_rotation, rotationFunc, 0.f, 0.f, 0.f, 0.1f);

		
			auto& update_scale = actor->GetScale();
			auto scaleFunc = [&]() 
			{
				using namespace Math;

				actor->GetLocalTransform() = Matrix::CalTransformation(actor->GetTranslation(), actor->GetRotation(), actor->GetScale());

			};

			DragVec3Control("Scale", update_scale, scaleFunc, 1.0f, 0.f, 0.f, 0.1f);


			ImGui::TreePop();
		}

		using namespace SceneObjects;
		if (auto it = dynamic_cast<BoxEntity*>(actor))
		{
			ImGui::Separator();
			if (ImGui::TreeNodeEx((void*)((uint64_t)actor->GetID() + typeid(BoxEntity).hash_code()), flags, "Sprite Renderer"))
			{
				if (ImGui::ColorEdit4("Color", glm::value_ptr(it->GetColor())))
				{

				}

				ImGui::TreePop();
			}
		}

		if (auto it = dynamic_cast<CameraRig*>(actor))
		{
			ImGui::Separator();
			if (ImGui::TreeNodeEx((void*)((uint64_t)actor->GetID() + typeid(CameraRig).hash_code()), flags, "CameraRig"))
			{
				auto& RigParam = it->GetRigParam();

				auto yaw = Math::ToDegrees(RigParam.m_CurrentYaw);
				auto pitch = Math::ToDegrees(RigParam.m_CurrentPitch);

				if (ImGui::DragFloat("translation rate", &RigParam.m_UnitsPerSecond, 0.5f))
				{
					
				}

				if (ImGui::DragFloat("rotation rate", &RigParam.m_DegreesPerSecond, 0.5f))
				{
				
				}

				if (ImGui::DragFloat("yaw", &yaw, 0.5f))
				{
					RigParam.m_CurrentYaw = Math::ToRadians(yaw);
					it->SetYaw(RigParam.m_CurrentYaw);
				}

				if (ImGui::DragFloat("pitch", &pitch, 0.5f))
				{
					RigParam.m_CurrentPitch = Math::ToRadians(pitch);
					it->SetPitch(RigParam.m_CurrentPitch);
				}

				ImGui::TreePop();

			}
		}

		if (auto it = dynamic_cast<CameraBase*>(actor))
		{
			ImGui::Separator();
			if (ImGui::TreeNodeEx((void*)((uint64_t)actor->GetID() + typeid(CameraBase).hash_code()), flags, "Camera"))
			{
				
			
				const char* projectionType[] = { "Orthographic", "Perspective"};

				bool currentProjectionType = m_App->m_ProjectionType[1];//dynamic_cast<Camera::PerspectiveCamera*>(actor);

				const char* currentProjectionTypeString = projectionType[currentProjectionType];
				
				if (ImGui::BeginCombo("projection", currentProjectionTypeString))
				{
					for (auto i = 0; i < 2; i++)
					{
						bool isSelected = currentProjectionTypeString == projectionType[i];

						if (ImGui::Selectable(projectionType[i], isSelected))
						{
							currentProjectionTypeString = projectionType[i];
							m_App->m_ProjectionType[i] = true;

							m_App->m_ProjectionType[1-i] = false;
						}

						if (isSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndCombo();
				}



				if (m_App->m_ProjectionType[0])
				{
					
					ImGui::DragFloat("left", &it->GetCameraSetting().m_OrthographicSetting.m_Left);
					ImGui::DragFloat("right", &it->GetCameraSetting().m_OrthographicSetting.m_Right);
					ImGui::DragFloat("top",   &it->GetCameraSetting().m_OrthographicSetting.m_Top);
					ImGui::DragFloat("bottom",&it->GetCameraSetting().m_OrthographicSetting.m_Bottom);
					ImGui::DragFloat("near",  &it->GetCameraSetting().m_OrthographicSetting.m_Near);
					ImGui::DragFloat("far",   &it->GetCameraSetting().m_OrthographicSetting.m_Far);
				}

				else
				{

					auto perspectiveCamera = static_cast<Camera::PerspectiveCamera*>(it);

					if (ImGui::Checkbox("Fixed Aspect Ratio", &it->GetIsFixAspectRatio()))
					{
						GENGINE_CORE_INFO("{}", perspectiveCamera->GetCameraSetting().m_PerspectiveSetting.m_AspectRatio);
						perspectiveCamera->OnResize(perspectiveCamera->GetCameraSetting().m_PerspectiveSetting.m_AspectRatio);
					}

					/*if (ImGui::DragFloat("zoom", &it->GetCameraSetting().m_PerspectiveSetting.m_ZoomLevel, 0.1f, 0.2f, 1.5f))
					{

						perspectiveCamera->SetZoom(it->GetCameraSetting().m_PerspectiveSetting.m_ZoomLevel);
					}*/

					auto& _it = it->GetCameraSetting();
					if (ImGui::DragFloat("fov", &_it.m_PerspectiveSetting.m_FieldOfView, 1.f, 10.f, 90.f))
					{
						perspectiveCamera->SetFieldOfView(it->GetCameraSetting().m_PerspectiveSetting.m_FieldOfView);
					}

				/*	if (ImGui::DragFloat("aspect ratio", &it->GetCameraSetting().m_PerspectiveSetting.m_AspectRatio))
					{
						perspectiveCamera->SetAspectRatio(it->GetCameraSetting().m_PerspectiveSetting.m_AspectRatio);
					}*/

					if (ImGui::DragFloat("near", &it->GetCameraSetting().m_PerspectiveSetting.m_Near))
					{
						perspectiveCamera->SetNear(it->GetCameraSetting().m_PerspectiveSetting.m_Near);
					}

					if (ImGui::DragFloat("far", &it->GetCameraSetting().m_PerspectiveSetting.m_Far))
					{
						perspectiveCamera->SetFar(it->GetCameraSetting().m_PerspectiveSetting.m_Far);
					}
				}

				ImGui::TreePop();
			}

		}

	}

	void SceneHierarchyPanel::DrawEntityNode(Actor* actor)
	{
		//static int offset = 0;
		//std::string temp(5, ' ');
		//std::string temp(space * 3, ' ');
		auto& tag = actor->GetTag();

		ImGuiTreeNodeFlags flags = ((m_SelectedContext == actor)? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)actor, flags, tag.c_str());

		if (ImGui::IsItemClicked())
		{
			m_SelectedContext = actor;
		}

		if (opened)
		{
			for (auto p : actor->GetChildrenRef())
			{
				DrawEntityNode(p);
			}

			ImGui::TreePop();
		}
		
		////tag.append(temp);
		//tag.insert(0, temp);
		//ImGui::Separator();
		//ImGui::Text("%s", tag.c_str());
		////ImGui::Text("%s", tag.c_str());
		//ImGui::Separator();

		//if (!actor->Empty())
		//{
		//	for (auto p : actor->GetChildrenRef())
		//	{
		//		
		//		DrawEntityNode(p, ++space);
		//	}
		//}

		//--space;
		
	}

}