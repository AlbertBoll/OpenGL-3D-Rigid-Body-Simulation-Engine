#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "glm/glm.hpp"
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace GEngine::Math
{
	class Matrix
	{
	public:
		static glm::mat4 MakeIdentity()
		{
			return glm::mat4(1.0f);
		}

		static glm::mat4 MakeTranslation(float x, float y, float z)
		{
			return glm::translate(glm::mat4(1.0f), { x, y, z });
		}

		static glm::mat4 MakeRotationX(float angle)
		{
			return glm::rotate(glm::mat4(1.0f), angle, { 1, 0, 0 });
		}

		static glm::mat4 MakeRotationY(float angle)
		{
			return glm::rotate(glm::mat4(1.0f), angle, { 0, 1, 0 });
		}

		static glm::mat4 MakeRotationZ(float angle)
		{
			return glm::rotate(glm::mat4(1.0f), angle, { 0, 0, 1 });
		}

		static glm::mat4 MakeRotationFromArbitaryAxis(const Vec3f& axis, float angle)
		{
			auto quat = glm::angleAxis(glm::radians(angle), axis);
			return glm::toMat4(quat);
		}


		static glm::vec3 Transform(const glm::vec3& v, const glm::quat& q)
		{
			return glm::toMat4(q) * glm::vec4(v, 1.0f);
		}

		static glm::mat4 MakeScale(float s)
		{
			return glm::scale(glm::mat4(1.0f), { s, s, s });
		}

		static Vec3f GetTranslation(const Mat4& invView)
		{
			return { invView[3][0], invView[3][1], invView[3][2] };
		}


		static Vec3f GetZAxis(const Mat4& invView)
		{
			return glm::normalize(Vec3f{ invView[2][0], invView[2][1], invView[2][2] });
		
		}


		static Vec3f GetXAxis(const Mat4& invView)
		{
			return glm::normalize(Vec3f{ invView[0][0], invView[0][1], invView[0][2] });

		}

		static Vec3f GetYAxis(const Mat4& invView)
		{
			return glm::normalize(Vec3f{ invView[1][0], invView[1][1], invView[1][2] });

		}

		static glm::mat4 MakeScale(const Vec3f& scale)
		{
			return glm::scale(glm::mat4(1.0f), scale);
		}

		static glm::mat4 MakeScaleX(float s)
		{
			return glm::scale(glm::mat4(1.0f), { s, 1, 1 });
		}

		static glm::mat4 MakeScaleY(float s)
		{
			return glm::scale(glm::mat4(1.0f), { 1, s, 1 });
		}

		static glm::mat4 MakeScaleZ(float s)
		{
			return glm::scale(glm::mat4(1.0f), { 1, 1, s });
		}

		static glm::mat4 MakePerspective(float field_of_view, float aspect_ratio, float near_field, float far_field)
		{
			return glm::perspective(glm::radians(field_of_view), aspect_ratio, near_field, far_field);
		}

		static glm::mat4 MakeOrthographic(float left = -1.f, float right = 1.f, float bottom = -1.f,
			float top = 1.f, float near_ = -1.f, float far_ = 1.f)
		{
			return glm::ortho(left, right, bottom, top, near_, far_);
		}


		static bool DecomposeTransform(const Mat4& transform, Vec3f& translation, Vec3f& rotation, Vec3f& scale)
		{
			// From glm::decompose in matrix_decompose.inl

			using namespace glm;
			using T = float;

			mat4 LocalMatrix(transform);

			// Normalize the matrix.
			if (epsilonEqual(LocalMatrix[3][3], static_cast<float>(0), epsilon<T>()))
				return false;

			// First, isolate perspective.  This is the messiest.
			if (
				epsilonNotEqual(LocalMatrix[0][3], static_cast<T>(0), epsilon<T>()) ||
				epsilonNotEqual(LocalMatrix[1][3], static_cast<T>(0), epsilon<T>()) ||
				epsilonNotEqual(LocalMatrix[2][3], static_cast<T>(0), epsilon<T>()))
			{
				// Clear the perspective partition
				LocalMatrix[0][3] = LocalMatrix[1][3] = LocalMatrix[2][3] = static_cast<T>(0);
				LocalMatrix[3][3] = static_cast<T>(1);
			}

			// Next take care of translation (easy).
			translation = vec3(LocalMatrix[3]);
			LocalMatrix[3] = vec4(0, 0, 0, LocalMatrix[3].w);

			vec3 Row[3], Pdum3{};

			// Now get scale and shear.
			for (length_t i = 0; i < 3; ++i)
				for (length_t j = 0; j < 3; ++j)
					Row[i][j] = LocalMatrix[i][j];

			// Compute X scale factor and normalize first row.
			scale.x = length(Row[0]);
			Row[0] = detail::scale(Row[0], static_cast<T>(1));
			scale.y = length(Row[1]);
			Row[1] = detail::scale(Row[1], static_cast<T>(1));
			scale.z = length(Row[2]);
			Row[2] = detail::scale(Row[2], static_cast<T>(1));

			// At this point, the matrix (in rows[]) is orthonormal.
			// Check for a coordinate system flip.  If the determinant
			// is -1, then negate the matrix and the scaling factors.
			#if 0
			Pdum3 = cross(Row[1], Row[2]); // v3Cross(row[1], row[2], Pdum3);
			if (dot(Row[0], Pdum3) < 0)
			{
				for (length_t i = 0; i < 3; i++)
				{
					scale[i] *= static_cast<T>(-1);
					Row[i] *= static_cast<T>(-1);
				}
			}
			#endif

			rotation.y = asin(-Row[0][2]);
			if (cos(rotation.y) != 0) {
				rotation.x = atan2(Row[1][2], Row[2][2]);
				rotation.z = atan2(Row[0][1], Row[0][0]);
			}
			else {
				rotation.x = atan2(-Row[2][0], Row[1][1]);
				rotation.z = 0;
			}


			return true;

		}


		static Mat4 CalTransformation(const Vec3f& translation, const Vec3f& rotation, const Vec3f& scale)
		{
			return glm::translate(Mat4(1.0f), translation) * RotationMatrixFromEulerAngle(rotation) * glm::scale(Mat4(1.f), scale);
		}


		static Mat4 RotationMatrixFromEulerAngle(const Vec3f& euler)
		{
			return glm::eulerAngleXYZ(ToRadians(euler.x), ToRadians(euler.y), ToRadians(euler.z));
		}

		static bool ReCalculateNewTransform(const Mat4& oldTransform, Mat4& newTransform, Vec3f& translation, Vec3f& rotation, Vec3f& scale)
		{
			if (DecomposeTransform(oldTransform, translation, rotation, scale))
			{
				Mat4 transformX = glm::eulerAngleX(ToRadians(rotation.x));
				Mat4 transformY = glm::eulerAngleY(ToRadians(rotation.y));
				Mat4 transformZ = glm::eulerAngleZ(ToRadians(rotation.z));

				Mat4 new_rotation = transformX * transformY * transformZ;

				newTransform = glm::translate(Mat4(1.0f), translation) * new_rotation * glm::scale(Mat4(1.f), scale);
				return true;
			}

			return false;
		}


		static glm::mat4 MakeLookAt(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up = { 0, 1, 0 })
		{


			glm::vec3 const f(normalize(center - eye));
			glm::vec3 const s(normalize(cross(f, up)));
			glm::vec3 const u(cross(s, f));

			glm::mat4 Result(1.0f);

			//Result[0][0] = s.x;
			//Result[0][1] = s.y;
			//Result[0][2] = s.z;
			//Result[1][0] = u.x;
			//Result[1][1] = u.y;
			//Result[1][2] = u.z;
			//Result[2][0] = -f.x;
			//Result[2][1] = -f.y;
			//Result[2][2] = -f.z;
			//
			////Result[3][0] = eye.x;
			////Result[3][1] = eye.y;
			////Result[3][2] = eye.z;

			//Result[3][0] = -dot(s, eye);
			//Result[3][1] = -dot(u, eye);
			//Result[3][2] = dot(f, eye);

			Result[0][0] = s.x;
			Result[1][0] = s.y;
			Result[2][0] = s.z;
			Result[0][1] = u.x;
			Result[1][1] = u.y;
			Result[2][1] = u.z;
			Result[0][2] = -f.x;
			Result[1][2] = -f.y;
			Result[2][2] = -f.z;
			//Result[3][0] = eye.x;
			//Result[3][1] = eye.y;
			//Result[3][2] = eye.z;
			Result[3][0] = -dot(s, eye);
			Result[3][1] = -dot(u, eye);
			Result[3][2] = dot(f, eye);
			
			return Result;

		}
	};
	
}
