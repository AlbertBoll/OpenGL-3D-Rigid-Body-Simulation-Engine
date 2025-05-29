#pragma once
#include"Component/Component.h"
#include <Core/Timestep.h>
#include"Managers/InputManager.h"
#include <string>
#include <vector>



namespace GEngine
{
	//using namespace Manager;
	//class GEngine::Manager::InputManager;

	using namespace Component;
	class CameraBase;

	class Actor
	{

	public:
		NONCOPYABLE(Actor);
		Actor() = default;
		Actor(const std::string& name) { m_Tag = name; }
		Actor(Actor&& other)noexcept;
		Actor& operator=(Actor&& other)noexcept;
		virtual ~Actor();
		


		void SetParent(Actor* parent) { m_Parent = parent; }

		Actor* GetParent()const { return m_Parent; }

		std::vector<Actor*>& GetChildrenRef() { return m_ChildrenRef; }

		auto& GetChildren() { return m_Children; }

		std::string GetTag()const { return m_Tag.Name; }
		std::string& GetTag(){ return m_Tag.Name; }
		void SetTag(const std::string& name) { m_Tag = name; }

		void PrintLocalTransform()const;
		Vec2f Get2DPosition()const { return Vec2f(m_LocalTransformComponent.Translation); }


		virtual void Update(Timestep ts) {}

		virtual void Render(CameraBase* camera){}

		//virtual void Render() {}


		//Add smarter child pointer
		template<typename child_type, typename = std::enable_if_t<std::is_base_of_v<ScopedPtr<Actor>, child_type>>>
		void Add(child_type&& child)
		{
			child->m_Parent = this;
			m_ChildrenRef.push_back(child.get());
			m_Children.push_back(std::move(child));
			
		}


		//Add smarter children pointers
		template<typename child_type, typename...child_types, typename = std::enable_if_t<std::is_base_of_v<ScopedPtr<Actor>, child_type>>>
		void Add(child_type&& child, child_types&&... children)
		{
			Add(std::move(child));
			Add(std::forward<child_types>(children)...);
		}


		//Add raw child pointer
		template<typename child_type, typename = std::enable_if_t<std::is_base_of_v<Actor, child_type>>>
		void Add(child_type* child)
		{
			child->m_Parent = this;
			ScopedPtr<Actor>temp(child);
			m_ChildrenRef.push_back(child);
			m_Children.push_back(std::move(temp));
		}

		//virtual void Add_(Actor* child) {}

		//Add raw children pointers
		template<typename child_type, typename...child_types, typename = std::enable_if_t<std::is_base_of_v<Actor, child_type>>>
		void Add(child_type* child, child_types&&... children)
		{
			Add(child);
			Add(std::forward<child_types>(children)...);
		}


		void Add(const std::initializer_list<Actor*>& children);


		void Remove(Actor* actor);

		template<typename child_type, typename...child_types, typename = std::enable_if_t<std::is_base_of_v<Actor, child_type>>>
		void Remove(child_type* child, child_types&&... children)
		{
			Remove(child);
			Remove(std::forward<child_types>(children)...);
		}


		void Remove(const std::initializer_list<Actor*>& children);
		Mat4 GetWorldTransform() const;
		Mat4 GetWorldTransform();
	

		Mat4& GetLocalTransform();
	
		Vec3f GetPosition()const { return m_LocalTransformComponent.Translation; }
		Vec3f& GetTranslation() { return m_LocalTransformComponent.Translation; }
		Vec3f& GetRotation() { return m_LocalTransformComponent.Rotation; }
		Vec3f& GetScale() { return m_LocalTransformComponent.Scale; }

	
		Mat4 GetLocalTransform() const;

		TransformComponent& GetLocalTransformation() { return m_LocalTransformComponent; }
		//Mat4& GetWorldTransform();

		void ApplyTransform(const Mat4& transform, bool isLocal = true);
		
		void Translate(float x, float y, float z, bool isLocal = true);

		void TranslateX(float x, bool isLocal = true);
		void TranslateY(float y, bool isLocal = true);
		void TranslateZ(float z, bool isLocal = true);

		void IncrementPosX(float deltaX);
		void IncrementPosY(float deltaY);
		void IncrementPosZ(float deltaZ);

		void RotateX(float angle, bool isLocal = true);

		void SetYaw(float yaw);

		void SetPitch(float pitch);

		float GetPitch()const { return m_LocalTransformComponent.Rotation.x; }
		float GetYaw()const { return m_LocalTransformComponent.Rotation.y; }
		float Yaw()const { return Math::ToDegrees(m_LocalTransformComponent.Rotation.y); }
		void SetRoll(float roll);
		

		void RotateY(float angle, bool isLocal = true);
		void SetRotation(int index, float angle);

		void SetRotation(const Vec3f& rot);

		void RotateZ(float angle, bool isLocal = true);

		void RotateFromArbitaryAxis(const Vec3f& axis, float angle, bool isLocal = true);
		
		void Scale(float scale, bool isLocal = true);
		void SetScale(const Vec3f& scale, bool isLocal = true);

		void SetScale(int index, float value);
		void SetInverseScale(int index, float value);
		void SetScaleX(float scale, bool isLocal = true);
		void SetScaleY(float scale, bool isLocal = true);
		void SetScaleZ(float scale, bool isLocal = true);

		Vec3f GetWorldPosition() const;
		//Vec3f& GetWorldPosition();

		void SetPosition(const Vec3f& pos);

		void SetPositionX(float x);
		void SetPositionY(float y);
		void SetPositionZ(float z);

		void SetPosition(int index, float value);
		

		void SetPosition(float x, float y, float z);

		void LookAt(const Vec3f& target_position);

		bool Empty()const
		{
			return m_ChildrenRef.empty();
		}

		//extract rotation part from 4x4 transform, taking both scaling and shearing into account
		Mat3 GetStrongRotationMatrix()const;

		//Assume no scaling and shearing part
		Mat3 GetWeakRotationMatrix()const;

		Vec3f GetForward()const;

		Vec3f GetRight()const;

		void SetForward(const Vec3f& direction);

		void SetVisibility(bool bVisibility) { m_Visible = bVisibility; }

		UUID GetID()const { return (UUID)m_ID; }

		operator uint64_t()const { return  (uintptr_t)this; }

		void UpdateLocalTransform();

		Quat GetOrientation() const;

		Vec3f GetForwardDirection() const;
		Vec3f GetUpDirection() const;
		Vec3f GetRightDirection() const;

		bool GetVisibility()const { return m_Visible; }


	protected:
		std::vector<ScopedPtr<Actor>> m_Children;

		//Collection of raw children pointer for using its functionality,
		//but not control its life time 
		std::vector<Actor*> m_ChildrenRef;
		Actor* m_Parent{};
		TransformComponent m_LocalTransformComponent;
		TagComponent m_Tag;
		IDComponent m_ID;
		bool m_Visible{ true };

	};


}
