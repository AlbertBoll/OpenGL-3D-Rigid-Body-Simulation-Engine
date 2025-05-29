#pragma once
#include"Math/Math.h"
#include <string>
#include <Core/UUID.h>
#include <Camera/SceneCamera.h>
#include"Core/Utility.h"
#include <Core/Log.h>
#include <Events/Signal.h>
//#include "Events/Property.h"
#include "Physics/Bounds.h"

//#include <Assets/Textures/Texture.h>
//#include <Assets/Shaders/Shader.h>


//using namespace GEngine;
//using namespace GEngine::Math;
//using namespace GEngine::Asset;

namespace GEngine
{
	class RigidBody3D;
	class Geometry;

	namespace Asset
	{
		class Texture;
		class Shader;
	}
	
	//using namespace Asset;

	class Asset::Texture;
	class Asset::Shader;

	using namespace Math;

	namespace Component
	{
		template<typename T>
		struct Uniform
		{
			std::string Name;
			T Data;
		};

		template<typename T>
		struct Uniforms
		{
			std::string Name;
			std::vector<T> Data;
		};

		struct IDComponent
		{
			UUID ID;
			IDComponent() = default;
			IDComponent(UUID id): ID(id){}
			IDComponent(const IDComponent&) = default;
			operator UUID() const { return ID; }
		};

		struct TransformComponent
		{
			Vec3f Translation{ 0.0f, 0.0f, 0.0f };
			Vec3f Rotation{ 0.0f, 0.0f, 0.0f };
			Vec3f Scale{ 1.0f, 1.0f, 1.0f };

			Mat4 Transform = Mat4(1.0f);
			//Mat4 Transform{1.0f};


			TransformComponent() = default;
			TransformComponent(const TransformComponent& transform) = default;
			TransformComponent(const Vec3f& translation) : Translation(translation) {}
			TransformComponent(const Mat4& transform) : Transform(transform) {}

			operator Mat4() const { return Transform; }
			operator Mat4& () { return Transform; }

			Mat4 GetTransform()
			{
				Mat4 rotation = glm::toMat4(glm::quat(Rotation));
				Transform = glm::translate(Mat4(1.0f), Translation) * rotation * glm::scale(Mat4(1.0f), Scale);
				return Transform;
			}

			Mat4 GetTransform()const
			{
				Mat4 rotation = glm::toMat4(glm::quat(Rotation));
				//Transform = 
				return glm::translate(Mat4(1.0f), Translation) * rotation * glm::scale(Mat4(1.0f), Scale);
			}



		};

		struct MeshComponent
		{
			Geometry* m_Geometry{};
			MeshComponent(Geometry* geo);
			MeshComponent() = default;

		};

		struct Transform3DComponent
		{
			Vec3f Translation{ 0.0f, 0.0f, 0.0f };
			Vec3f EulerRotation{ 0.0f, 0.0f, 0.0f };
			Vec3f Scale{ 1.0f, 1.0f, 1.0f };
			Quat QuatRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			Signal<void(const Vec3f&)> OnScaleChanged;
			
			/*Property<Vec3f> Translation_{ {0.0f, 0.0f, 0.0f } };
			Property<Vec3f> EulerRotation_{ {0.0f, 0.0f, 0.0f } };
			Property<Vec3f> Scale_{ {1.0f, 1.0f, 1.0f } };
			Property<Quat> QuatRotation_{ {1.0f, 0.0f, 0.0f, 0.0f } };*/
			
			Transform3DComponent() = default;
			Transform3DComponent(const Transform3DComponent& transform) = default;
		
			/*Transform3DComponent(const Vec3f& translation) : Translation(translation)
			{
			}*/
			
			Transform3DComponent(const Vec3f& translation, const Vec3f& euler_rotation = { 0.0f, 0.0f, 0.0f }, const Vec3f& scale = { 1.0f, 1.0f, 1.0f }) : Translation(translation), Scale(scale), EulerRotation(euler_rotation)
			{
				QuatRotation = Quat(EulerRotation);
			}
			

			Mat4 GetTransform()const
			{
				Mat4 rotation = glm::toMat4(QuatRotation);
				return glm::translate(Mat4(1.0f), Translation) * rotation * glm::scale(Mat4(1.0f), Scale);
			}

			void SetTranslation(const Vec3f& new_translation)
			{
				Translation = new_translation;
			}

			/*void SetRotation(const Quat& quat)
			{
				QuatRotation = quat;
				EulerRotation = glm::eulerAngles(quat);
			}*/

			void SetRotation(const Vec3f& rotation)
			{
				QuatRotation = Quat(rotation);
				EulerRotation = rotation;
			}

			void SetRotation(const Quat& quat)
			{
				QuatRotation = quat;
				EulerRotation = glm::eulerAngles(quat);
			}

			/*void SetRotation(const Property<Vec3f>& rotation)
			{
				QuatRotation = Quat(rotation);
				EulerRotation = rotation;
			}*/


			void SetScale(const Vec3f& scale)
			{
				if (Scale != scale)
				{
					OnScaleChanged(scale);
					Scale = scale;
				}
			}

			void SetScale(float scale)
			{
				if (Scale != Vec3f{ scale })
				{
					OnScaleChanged({ scale, scale, scale });
					Scale = Vec3f{ scale };
				}
			}

		};

		struct Transform2DComponent
		{
			Vec2f Translation{ 0.0f, 0.0f };
			Vec2f Scale{ 1.0f, 1.0f };
			float Rotation{ 0 };
		};


		struct TagComponent
		{
			std::string Name;
			TagComponent() = default;
			TagComponent(const std::string& name) : Name(name) {}
			operator std::string() { return Name; }
			operator std::string() const { return Name; }
		};

		struct BasicMaterialComponent
		{


			//Shader* m_Shader{};
			Vec4f Color{ 1.0f };
			BasicMaterialComponent() = default;
			BasicMaterialComponent(const Vec4f& color) : Color(color) {}

		};



		struct CameraComponent
		{
			SceneCamera Camera;
			bool Primary = true; // TODO: think about moving to Scene
			bool FixedAspectRatio = false;

			CameraComponent() = default;
			CameraComponent(const CameraComponent&) = default;
		};


		struct DirectionalLightComponent
		{
			Uniform<Vec3f> direction;
			Uniform<Vec3f> ambient;
			Uniform<Vec3f> diffuse;
			Uniform<Vec3f> specular;

			void LoadUniforms(Asset::Shader* shader)const;
		};

		struct PointLightComponent
		{
			Uniform<float> constant;
			Uniform<float> linear;
			Uniform<float> quadratic;

			Uniform<Vec3f> ambient;
			Uniform<Vec3f> diffuse;
			Uniform<Vec3f> specular;
			void LoadUniforms(Asset::Shader* shader)const;

		};

		struct SpotLightComponent
		{
						
			Uniform<float> cutOff;
			Uniform<float> outerCutOff;
			Uniform<float> constant;
			Uniform<float> linear;
			Uniform<float> quadratic;

			Uniform<Vec3f> direction;
			Uniform<Vec3f> ambient;
			Uniform<Vec3f> diffuse;
			Uniform<Vec3f> specular;
			void LoadUniforms(Asset::Shader* shader)const;

		};


		struct LightComponent
		{

			//Uniform<Vec3f> Position;

			Uniform<Vec3f> Color;
			Uniform<Vec3f> Attenuation;
			LightComponent() = default;
			LightComponent(const LightComponent&) = default;
			std::string UniformName;

		};

		struct LightComponents
		{
			Uniforms<Vec3f> Positions;
			Uniforms<Vec3f> Colors;
			Uniforms<Vec3f> Attenuations;
		};

		struct FogComponent
		{
			Uniform<float> Density;
			Uniform<float> Gradient;
		};

		struct MaterialComponent
		{
			Uniform<float> Reflectivity;
			Uniform<float> ShineDamper;
		};

		struct TextureComponent
		{
			Uniform<int> NumOfRows;

		};

		struct SkyBoxComponent
		{
			Uniform<Vec3f> FogAmbientColor;
			Uniform<float> LowerLimit;
			Uniform<float> UpperLimit;
			Uniform<float> BlendFactor;
		};

		struct SpriteComponent
		{
			Uniform<Vec3f> SpriteColor;
			Vec2f Velocity;
			Vec2f Size;
		};

		struct ParticleComponent
		{
			Vec3f Position;
			Vec3f Velocity;
			float GravityEffect;
			float LifeLength;
			float Rotation;
			float Scale;
			float ElapsedTime = 0;
		};


		struct RelationshipComponent
		{
			UUID ParentHandle = 0;
			std::vector<UUID> Children;

			RelationshipComponent() = default;
			RelationshipComponent(const RelationshipComponent& other) = default;
			RelationshipComponent(UUID parent)
				: ParentHandle(parent) {}

		};

		


		enum class DrawStyle_
		{
			Points,
			LINES,
			LINE_LOOP,
			LINE_STRIP,
			TRIANGLES,
			TRIANGLE_STRIP,
			TRIANGLE_FAN
		}; 

		enum class DrawMode_
		{
			Arrays,
			Elements,
			ArraysInstanced,
			ElementsInstanced
		};

		enum class LineType_
		{
			Connected,
			Loop,
			Segments
		};

		enum class DrawType_
		{
			Point,
			Line,
			Surface
		};


		struct PointSetting_
		{
			float pointSize{ 8.0f };
			bool bRoundedPoints = true;
		};

		struct LineSetting_
		{
			float lineWidth = 1.0f;
			LineType_ lineType;
		};

		struct SurfaceSetting_
		{
			float lineWidth = 1.0f;
			bool bDoubleSide = true;
			bool bWireFrame = false;
		};

		union PrimitivesSettings_
		{
			PointSetting_ pointSetting;
			LineSetting_ lineSetting;
			SurfaceSetting_ surfaceSetting;


			PrimitivesSettings_(): surfaceSetting(){}
		};

		struct RenderSetting_
		{
			PrimitivesSettings_ m_PrimitivesSetting{};
			DrawMode_ DrawMode = DrawMode_::Arrays;
			DrawStyle_ DrawStyle = DrawStyle_::TRIANGLES;
		};

		struct RenderComponent
		{
			Asset::Shader* Shader{};
			RenderSetting_ RenderSettings{};

		};

		struct PreRenderPassComponent
		{
			Asset::Shader* Shader{};
			RenderSetting_ RenderSettings{};
		};


		//class Asset::Texture;
		struct TexturesComponent
		{
			TexturesComponent() = default;
			
			TexturesComponent(const std::initializer_list<Asset::Texture*>& texturesList):Textures{texturesList}
			{
				GENGINE_CORE_INFO("initializer");
			}

			TexturesComponent(std::initializer_list<Asset::Texture*>&& texturesList) :Textures{ std::move(texturesList) }
			{
				GENGINE_CORE_INFO("move");
			}

			TexturesComponent(const std::vector<Asset::Texture*>& texturesList) :Textures{ texturesList }
			{
				GENGINE_CORE_INFO("vector");
			}
			
			void BindTextures(Asset::Shader* shader);

			void PreBindTextures(Asset::Shader* shader);
			
			void LoadUniforms(Asset::Shader* shader)const;

			std::unordered_map<unsigned int, std::pair<unsigned int, unsigned int>> TextureList;
			std::vector<Asset::Texture*> Textures;
			Uniform<Vec2f> Tiling{ "u_tiling",  {1.f, 1.f} };
		};


		enum class BodyType { Static = 0, Dynamic, Kinematic };

		struct Fixture3DProperty
		{
			
			//BodyType Type = BodyType::Static;
			Vec3f m_Position{ 0.f };
			Quat m_Orientation{ 1.0f, 0.f, 0.f, 0.f };

			Vec3f m_LinearVelocity{ 0.f };
			Vec3f m_AngularVelocity{ 0.f };

			float		m_InvMass = 1.f;
			float		m_Elasticity = 0.5f;
			float		m_Friction = 0.5f;
		};

		class SphereShape;
		
		struct SphereFixture3DComponent
		{
			//SphereShape* m_Shape{};
			float Radius = 1.0f;
			Fixture3DProperty Property{};
		};

		struct BoxFixture3DComponent
		{
			Fixture3DProperty Property{};
			//std::vector<Vec3f> Points;
			//Bounds Builds;
			//void BuildPoints();

		};

		struct ConvexFixture3DComponent
		{
			Fixture3DProperty Property{};
			//std::vector<Vec3f> Points;
			//Bounds Builds;
			//void BuildPoints();

		};

		struct RigidBody3DComponent
		{

			RigidBody3DComponent() = default;
			RigidBody3DComponent(const RigidBody3DComponent&) = default;

			BodyType Type = BodyType::Static;
			RigidBody3D* RuntimeBody{};
			//RigidBodyBody3DProperty Property{};

		};

		
		template<typename... Component>
		struct ComponentGroup
		{

		};

		

		struct HelperMaterialComponent
		{
			Uniform<bool> UsingVertexColor = { "u_useVertexColor", true };
			Uniform<Vec4f> BaseColor = { "u_baseColor", {1.0f, 1.0f, 1.0f, 1.0f} };

		};

		using AllComponents = ComponentGroup<HelperMaterialComponent, DirectionalLightComponent, PreRenderPassComponent, Transform3DComponent, CameraComponent, TexturesComponent, RigidBody3DComponent, RenderComponent, RelationshipComponent,
			SphereFixture3DComponent, MeshComponent, BoxFixture3DComponent, ConvexFixture3DComponent, MaterialComponent>;


	}
	

}