#include "gepch.h"
#include "Core/Actor.h"
#include "Math/Matrix.h"
#include "Core/Log.h"
#include <glm/gtc/type_ptr.hpp>

namespace GEngine
{


	Actor::Actor(Actor&& other) noexcept
	{
	

		m_Children = std::move(other.m_Children);
		m_ChildrenRef = std::move(other.m_ChildrenRef);
		m_Parent = other.m_Parent;
		other.m_Parent = nullptr;
		m_Visible = other.m_Visible;
		other.m_Visible = false;
		m_LocalTransformComponent = std::move(other.m_LocalTransformComponent);
		//m_GlobalTransformComponent = std::move(other.m_GlobalTransformComponent);
		m_Tag = std::move(other.m_Tag);
	}

	Actor& Actor::operator=(Actor&& other) noexcept
	{
		if (this != &other)
		{
			m_Children = std::move(other.m_Children);
			m_ChildrenRef = std::move(other.m_ChildrenRef);
			delete m_Parent;
			m_Parent = other.m_Parent;
			other.m_Parent = nullptr;
			m_Visible = other.m_Visible;
			other.m_Visible = false;
			m_LocalTransformComponent = std::move(other.m_LocalTransformComponent);
			//m_GlobalTransformComponent = std::move(other.m_GlobalTransformComponent);
			m_Tag = std::move(other.m_Tag);
		}

		return *this;
	}

	Actor::~Actor()
	{
		std::cout << m_Tag.Name << " Destructor was called!" << std::endl;
	}

	//void Actor::PrintGlobalTransform()
	//{
	//	std::cout << (Mat4)m_GlobalTransformComponent << std::endl;
	//}

	void Actor::PrintLocalTransform()const
	{
		std::cout << (Mat4)m_LocalTransformComponent << std::endl;
	}


	void Actor::Add(const std::initializer_list<Actor*>& children)
	{
		for (auto& p : children)
		{
			Add(p);
		}
	}

	void Actor::Remove(Actor* actor) 
	{
		for (size_t i = 0; i < m_ChildrenRef.size(); ++i)
		{
			if (m_ChildrenRef[i] == actor)
			{
				m_ChildrenRef.erase(m_ChildrenRef.begin() + i);
				m_Children.erase(m_Children.begin() + i);
				break;
			}
		}
	}

	void Actor::Remove(const std::initializer_list<Actor*>& children)
	{
		for (auto& p : children) Remove(p);
	}



	Mat4 Actor::GetWorldTransform() const
	{

		auto condition = bool_variant(!m_Parent);

		/*if (!m_Parent)
		{
			return m_TransformComponent;
		}

		else 
		{
			return m_Parent->GetWorldTransform() * (Mat4)m_TransformComponent;
		}*/

		return std::visit([&](auto condition)
				{
				if constexpr (condition)
				{
					return (Mat4)m_LocalTransformComponent;
				}

				else if constexpr (!condition)
				{
					return m_Parent->GetWorldTransform() * m_LocalTransformComponent.Transform;
				}
 
			}, condition);
	}

	Mat4 Actor::GetWorldTransform()
	{
		auto condition = bool_variant(!m_Parent);

		auto transform = std::visit([&](auto condition)
			{
				if constexpr (condition)
				{
					return m_LocalTransformComponent.Transform;
				}

				else if constexpr (!condition)
				{
					return m_Parent->GetWorldTransform() * m_LocalTransformComponent.Transform;
				}

			}, condition);


		return transform;
	}



	Mat4& Actor::GetLocalTransform()
	{

		return (Mat4&)m_LocalTransformComponent;
	
	}



	Mat4 Actor::GetLocalTransform() const
	{
		return m_LocalTransformComponent.Transform;
	}

	void Actor::ApplyTransform(const Mat4& transform, bool isLocal)
	{
		auto is_local = bool_variant(isLocal);


		std::visit([&](auto is_local)
			{
				if constexpr (is_local)
					m_LocalTransformComponent.Transform = m_LocalTransformComponent.Transform * transform;

				else if constexpr (!is_local)
					m_LocalTransformComponent.Transform = transform * m_LocalTransformComponent.Transform;
			}, is_local);

	
	}


	void Actor::Translate(float x, float y, float z, bool isLocal)
	{
		


		//m_LocalTransformComponent.Transform[3][0] = m_LocalTransformComponent.Translation.x;
		//m_LocalTransformComponent.Transform[3][1] = m_LocalTransformComponent.Translation.y;
		//m_LocalTransformComponent.Transform[3][2] = m_LocalTransformComponent.Translation.z;
		ApplyTransform(Matrix::MakeTranslation(x, y, z), isLocal);

		m_LocalTransformComponent.Translation = { m_LocalTransformComponent.Transform[3][0],
		m_LocalTransformComponent.Transform[3][1], m_LocalTransformComponent.Transform[3][2] };
		//m_LocalTransformComponent.Translation.x += x;
		//m_LocalTransformComponent.Translation.y += y;
		//m_LocalTransformComponent.Translation.z += z;
	}

	void Actor::TranslateX(float x, bool isLocal)
	{
		//m_LocalTransformComponent.Translation.x += x;
		ApplyTransform(Matrix::MakeTranslation(x, 0, 0), isLocal);
		m_LocalTransformComponent.Translation = { m_LocalTransformComponent.Transform[3][0],
		m_LocalTransformComponent.Transform[3][1], m_LocalTransformComponent.Transform[3][2] };

	}

	void Actor::TranslateY(float y, bool isLocal)
	{
		//m_LocalTransformComponent.Translation.y += y;
		ApplyTransform(Matrix::MakeTranslation(0, y, 0), isLocal);
		m_LocalTransformComponent.Translation = { m_LocalTransformComponent.Transform[3][0],
		m_LocalTransformComponent.Transform[3][1], m_LocalTransformComponent.Transform[3][2] };
	}

	void Actor::TranslateZ(float z, bool isLocal)
	{
		//m_LocalTransformComponent.Translation.z += z;
		ApplyTransform(Matrix::MakeTranslation(0, 0, z), isLocal);
		m_LocalTransformComponent.Translation = { m_LocalTransformComponent.Transform[3][0],
		m_LocalTransformComponent.Transform[3][1], m_LocalTransformComponent.Transform[3][2] };
	}

	void Actor::IncrementPosX(float deltaX)
	{
		m_LocalTransformComponent.Translation.x += deltaX;
		m_LocalTransformComponent.Transform[3][0] = m_LocalTransformComponent.Translation.x;
	}

	void Actor::IncrementPosY(float deltaY)
	{
		m_LocalTransformComponent.Translation.y += deltaY;
		m_LocalTransformComponent.Transform[3][1] = m_LocalTransformComponent.Translation.y;
	}

	void Actor::IncrementPosZ(float deltaZ)
	{
		m_LocalTransformComponent.Translation.z += deltaZ;
		m_LocalTransformComponent.Transform[3][2] = m_LocalTransformComponent.Translation.z;
	}

	void Actor::RotateX(float angle, bool isLocal)
	{
		m_LocalTransformComponent.Rotation.x += angle;
		//m_LocalTransformComponent.ReCalculateTransform();
		ApplyTransform(Matrix::MakeRotationX(angle), isLocal);
	}

	void Actor::SetYaw(float yaw)
	{
		//GetTranslation().y = yaw;
		m_LocalTransformComponent = (Mat4)m_LocalTransformComponent * Matrix::MakeRotationY(yaw);
	}

	void Actor::SetPitch(float pitch)
	{
		//GetTranslation().x = pitch;
		m_LocalTransformComponent = (Mat4)m_LocalTransformComponent * Matrix::MakeRotationX(pitch);
	}

	void Actor::SetRoll(float roll)
	{
		//GetTranslation().z = roll;
		m_LocalTransformComponent = (Mat4)m_LocalTransformComponent * Matrix::MakeRotationZ(roll);
	}

	void Actor::RotateY(float angle, bool isLocal)
	{
		m_LocalTransformComponent.Rotation.y += angle;
		//m_LocalTransformComponent.ReCalculateTransform();
		ApplyTransform(Matrix::MakeRotationY(angle), isLocal);
	}

	void Actor::SetRotation(int index, float angle)
	{
		if (index == 1) RotateX(angle);
		else if (index == 2) RotateY(angle);
		else RotateZ(angle);
	}

	void Actor::SetRotation(const Vec3f& rot)
	{

	}

	void Actor::RotateZ(float angle, bool isLocal)
	{
		m_LocalTransformComponent.Rotation.z += angle;
		//m_LocalTransformComponent.ReCalculateTransform();
		ApplyTransform(Matrix::MakeRotationZ(angle), isLocal);
	}

	void Actor::RotateFromArbitaryAxis(const Vec3f& axis, float angle, bool isLocal)
	{
		ApplyTransform(Matrix::MakeRotationFromArbitaryAxis(axis, angle), isLocal);
	}

	void Actor::Scale(float scale, bool isLocal)
	{
		m_LocalTransformComponent.Scale = { scale, scale, scale };
		ApplyTransform(Matrix::MakeScale(scale), isLocal);
	}

	void Actor::SetScale(const Vec3f& scale, bool isLocal)
	{
		m_LocalTransformComponent.Scale = scale;
		ApplyTransform(Matrix::MakeScale(scale), isLocal);
	}

	void Actor::SetScale(int index, float value)
	{
		if (index == 1) SetScaleX(value);
		else if (index == 2) SetScaleY(value);
		else SetScaleZ(value);
	}

	void Actor::SetInverseScale(int index, float value)
	{
		if (index == 1) SetScaleX(1.f/value);
		else if (index == 2) SetScaleY(1.f/value);
		else SetScaleZ(1.f/value);
	}

	void Actor::SetScaleX(float scale, bool isLocal)
	{
		//m_LocalTransformComponent.Scale.x = scale;
		//if(scale!=0.0f)
		
		ApplyTransform(Matrix::MakeScaleX(scale), isLocal);
	}

	void Actor::SetScaleY(float scale, bool isLocal)
	{
		//m_LocalTransformComponent.Scale.y = scale;
		ApplyTransform(Matrix::MakeScaleY(scale), isLocal);
	}

	void Actor::SetScaleZ(float scale, bool isLocal)
	{
		//m_LocalTransformComponent.Scale.z = scale;
		ApplyTransform(Matrix::MakeScaleZ(scale), isLocal);
	}

	Vec3f Actor::GetWorldPosition() const
	{
		auto world_transform = GetWorldTransform();
	
		return { world_transform[3][0], world_transform[3][1] , world_transform[3][2] };
	}


	void Actor::SetPosition(const Vec3f& pos)
	{
		m_LocalTransformComponent.Translation = pos;
		m_LocalTransformComponent.Transform[3][0] = pos.x;
		m_LocalTransformComponent.Transform[3][1] = pos.y;
		m_LocalTransformComponent.Transform[3][2] = pos.z;
	}

	void Actor::SetPositionX(float x)
	{
		m_LocalTransformComponent.Translation.x = x;
		m_LocalTransformComponent.Transform[3][0] = x;
	}

	void Actor::SetPositionY(float y)
	{
		m_LocalTransformComponent.Translation.y = y;
		m_LocalTransformComponent.Transform[3][1] = y;
	}

	void Actor::SetPositionZ(float z)
	{
		m_LocalTransformComponent.Translation.z = z;
		m_LocalTransformComponent.Transform[3][2] = z;
	}

	void Actor::SetPosition(int index, float value)
	{
		if (index == 1) SetPositionX(value);
		else if (index == 2) SetPositionY(value);
		else SetPositionZ(value);

	}


	void Actor::SetPosition(float x, float y, float z)
	{
		m_LocalTransformComponent.Translation = {x, y, z};
		m_LocalTransformComponent.Transform[3][0] = x;
		m_LocalTransformComponent.Transform[3][1] = y;
		m_LocalTransformComponent.Transform[3][2] = z;
	}


	void Actor::LookAt(const Vec3f& target_position)
	{
		m_LocalTransformComponent = glm::inverse(Matrix::MakeLookAt(GetWorldPosition(), target_position));
		Vec3f translation, rotation, scale;
		if (Matrix::DecomposeTransform(m_LocalTransformComponent, translation, rotation, scale))
		{
			m_LocalTransformComponent.Translation = translation;
			m_LocalTransformComponent.Rotation = { Math::ToDegrees(rotation.x), Math::ToDegrees(rotation.y), Math::ToDegrees(rotation.z) };
		}
		
	}

	Mat3 Actor::GetStrongRotationMatrix() const
	{
		Vec3f t, r, s;
		auto world_transform = GetWorldTransform();
		Matrix::DecomposeTransform(world_transform, t, r, s);
		return glm::toMat4(Quat(r));

	}

	Mat3 Actor::GetWeakRotationMatrix() const
	{
		return Mat3(GetWorldTransform());
	}


	Vec3f Actor::GetForward() const
	{
		return GetWeakRotationMatrix() * Vec3f(0.f, 0.f, -1.f);
	}


	Vec3f Actor::GetRight() const
	{
		return GetWeakRotationMatrix() * Vec3f(1.f, 0.f, 0.f);
	}


	void Actor::SetForward(const Vec3f& direction)
	{
		const auto self_pos = GetWorldPosition();
		const auto target_pos = self_pos + direction;
		LookAt(target_pos);
	}


	void Actor::UpdateLocalTransform()
	{
		m_LocalTransformComponent.Transform =  Matrix::CalTransformation(GetTranslation(), GetRotation(), GetScale());
	}


	Quat Actor::GetOrientation() const
	{
		float yaw = GetYaw();
		float pitch = GetPitch();
		return Quat(Vec3f(-pitch, -yaw, 0.f));
	}


	Vec3f Actor::GetForwardDirection() const
	{
		return glm::rotate(GetOrientation(), Vec3f(0.0f, 0.0f, -1.0f));
	}


	Vec3f Actor::GetUpDirection() const
	{
		return glm::rotate(GetOrientation(), Vec3f(0.0f, 1.0f, 0.0f));
	}


	Vec3f Actor::GetRightDirection() const
	{
		return glm::rotate(GetOrientation(), Vec3f(1.0f, 0.0f, 0.0f));
	}


}