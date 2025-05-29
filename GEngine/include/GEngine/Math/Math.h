#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include <glm/gtx/quaternion.hpp>
#include <vector>
#include <iostream>

namespace GEngine
{



	namespace Math
	{
		typedef float Vec1f;
		typedef glm::vec2  Vec2f;
		typedef glm::vec3  Vec3f;
		typedef glm::vec4  Vec4f;
		typedef int Vec1i;
		typedef glm::ivec2 Vec2i;
		typedef glm::ivec3 Vec3i;
		typedef glm::ivec4 Vec4i;
		typedef glm::mat4  Mat4;
		typedef glm::mat3  Mat3;
		typedef glm::mat2  Mat2;
		typedef glm::quat  Quat;




		constexpr float Pi = 3.1415926535f;
		constexpr float TwoPi = Pi * 2.0f;
		constexpr float PiOver2 = Pi / 2.0f;
		constexpr float Infinity = std::numeric_limits<float>::infinity();
		constexpr float NegInfinity = -std::numeric_limits<float>::infinity();

		inline constexpr float ToRadians(float degrees)
		{
			return degrees * Pi / 180.0f;
		}

		inline constexpr float ToDegrees(float radians)
		{
			return radians * 180.0f / Pi;
		}

		inline constexpr Vec3f ToDegrees(const Vec3f& radians)
		{
			return { ToDegrees(radians.x), ToDegrees(radians.y), ToDegrees(radians.z) };
		}

		inline bool NearZero(float val, float epsilon = 0.001f)
		{
			if (fabs(val) <= epsilon)
			{
				return true;
			}
			else
			{
				return false;
			}
		}

		inline Mat3 Minor(const Mat4& rows, const int i, const int j)
		{
			Mat3 minor;

			int yy = 0;
			for (int y = 0; y < 4; y++) {
				if (y == j) {
					continue;
				}

				int xx = 0;
				for (int x = 0; x < 4; x++) {
					if (x == i) {
						continue;
					}

					minor[yy][xx] = rows[y][x];
					xx++;
				}

				yy++;
			}
			return minor;
		}

		inline float Cofactor(const Mat4& rows, const int i, const int j) 
		{
			const Mat3 minor = Minor(rows, i, j);
			const float C = float(pow(-1, i + 1 + j + 1)) * glm::determinant(minor); //minor.Determinant();
			return C;
		}

		inline float BarryCentric(Vec3f p1, Vec3f p2, Vec3f p3, Vec2f pos)
		{
			float det = (p2.z - p3.z) * (p1.x - p3.x) + (p3.x - p2.x) * (p1.z - p3.z);
			float l1 = ((p2.z - p3.z) * (pos.x - p3.x) + (p3.x - p2.x) * (pos.y - p3.z)) / det;
			float l2 = ((p3.z - p1.z) * (pos.x - p3.x) + (p1.x - p3.x) * (pos.y - p3.z)) / det;
			float l3 = 1.0f - l1 - l2;
			return l1 * p1.y + l2 * p2.y + l3 * p3.y;
		}


		inline bool IsValid(const Vec3f& vec) 
		{
			if (vec.x * 0.0f != vec.x * 0.0f) 
			{
				return false;
			}

			if (vec.y * 0.0f != vec.y * 0.0f) 
			{
				return false;
			}

			if (vec.z * 0.0f != vec.z * 0.0f) 
			{
				return false;
			}

			return true;
		}

		inline void GetOrtho(const Vec3f& n, Vec3f& u, Vec3f& v) 
		{
			// Get the orthonormal basis for the normal vectorVec3f
		/*	Vec3f n_{};
			if (glm::length(n) >= 0.000001f)
			{
				n_ = glm::normalize(n);
			}
			else
			{
				n_ = n;
			}*/
			Vec3f n_ = glm::normalize(n);
		
			//const Vec3f w = (n_.z * n_.z > 0.9f * 0.9f) ? Vec3f(1, 0, 0) : Vec3f(0, 0, 1);
			const Vec3f w = (n_.y * n_.y > 0.9f * 0.9f) ? Vec3f(1, 0, 0) : Vec3f(0, 1, 0);
			u = glm::normalize(glm::cross(w, n_));

			v = glm::normalize(glm::cross(n_, u));

			u = glm::normalize(glm::cross(v, n_));
		
		}


		template <typename T>
		T Max(const T& a, const T& b)
		{
			return (a < b ? b : a);
		}

		template <typename T>
		T Min(const T& a, const T& b)
		{
			return (a < b ? a : b);
		}

		template <typename T>
		T Clamp(const T& value, const T& lower, const T& upper)
		{
			return Min(upper, Max(lower, value));
		}

		inline float Abs(float value)
		{
			return fabs(value);
		}

		inline float Cos(float angle)
		{
			return cosf(angle);
		}

		inline float Sin(float angle)
		{
			return sinf(angle);
		}

		inline float Tan(float angle)
		{
			return tanf(angle);
		}

		inline float Acos(float value)
		{
			return acosf(value);
		}

		inline float Atan2(float y, float x)
		{
			return atan2f(y, x);
		}

		inline float Cot(float angle)
		{
			return 1.0f / Tan(angle);
		}

		inline float Lerp(float a, float b, float f)
		{
			return a + f * (b - a);
		}

		inline float Sqrt(float value)
		{
			return sqrtf(value);
		}

		inline float Fmod(float numer, float denom)
		{
			return fmod(numer, denom);
		}
	

		// 2D Vector
		class Vector2
		{
		public:
			float x;
			float y;

			Vector2()
				:x(0.0f)
				, y(0.0f)
			{}

			explicit Vector2(float inX, float inY)
				:x(inX)
				, y(inY)
			{}

			// Set both components in one line
			void Set(float inX, float inY)
			{
				x = inX;
				y = inY;
			}

			// Vector addition (a + b)
			friend Vector2 operator+(const Vector2& a, const Vector2& b)
			{
				return Vector2(a.x + b.x, a.y + b.y);
			}

			// Vector subtraction (a - b)
			friend Vector2 operator-(const Vector2& a, const Vector2& b)
			{
				return Vector2(a.x - b.x, a.y - b.y);
			}

			// Component-wise multiplication
			// (a.x * b.x, ...)
			friend Vector2 operator*(const Vector2& a, const Vector2& b)
			{
				return Vector2(a.x * b.x, a.y * b.y);
			}

			// Scalar multiplication
			friend Vector2 operator*(const Vector2& vec, float scalar)
			{
				return Vector2(vec.x * scalar, vec.y * scalar);
			}

			// Scalar multiplication
			friend Vector2 operator*(float scalar, const Vector2& vec)
			{
				return Vector2(vec.x * scalar, vec.y * scalar);
			}

			// Scalar *=
			Vector2& operator*=(float scalar)
			{
				x *= scalar;
				y *= scalar;
				return *this;
			}

			// Vector +=
			Vector2& operator+=(const Vector2& right)
			{
				x += right.x;
				y += right.y;
				return *this;
			}

			// Vector -=
			Vector2& operator-=(const Vector2& right)
			{
				x -= right.x;
				y -= right.y;
				return *this;
			}

			// Length squared of vector
			float LengthSq() const
			{
				return (x * x + y * y);
			}

			// Length of vector
			float Length() const
			{
				return (Math::Sqrt(LengthSq()));
			}

			// Normalize this vector
			void Normalize()
			{
				float length = Length();
				x /= length;
				y /= length;
			}

			// Normalize the provided vector
			static Vector2 Normalize(const Vector2& vec)
			{
				Vector2 temp = vec;
				temp.Normalize();
				return temp;
			}

			// Dot product between two vectors (a dot b)
			static float Dot(const Vector2& a, const Vector2& b)
			{
				return (a.x * b.x + a.y * b.y);
			}

			// Lerp from A to B by f
			static Vector2 Lerp(const Vector2& a, const Vector2& b, float f)
			{
				return Vector2(a + f * (b - a));
			}

			// Reflect V about (normalized) N
			static Vector2 Reflect(const Vector2& v, const Vector2& n)
			{
				return v - 2.0f * Vector2::Dot(v, n) * n;
			}

			// Transform vector by matrix
			static Vector2 Transform(const Vector2& vec, const class Matrix3& mat, float w = 1.0f);

			//static const Vector2 Zero;
			static const Vector2 Zero;
			static const Vector2 UnitX;
			static const Vector2 UnitY;
			static const Vector2 NegUnitX;
			static const Vector2 NegUnitY;
		};

		// 3D Vector
		class Vector3
		{
		public:
			float x;
			float y;
			float z;

			Vector3()
				:x(0.0f)
				, y(0.0f)
				, z(0.0f)
			{}

			explicit Vector3(float inX, float inY, float inZ)
				:x(inX)
				, y(inY)
				, z(inZ)
			{}

			// Cast to a const float pointer
			[[nodiscard]] const float* GetAsFloatPtr() const
			{
				return reinterpret_cast<const float*>(&x);
			}

			// Set all three components in one line
			void Set(float inX, float inY, float inZ)
			{
				x = inX;
				y = inY;
				z = inZ;
			}

			// Vector addition (a + b)
			friend Vector3 operator+(const Vector3& a, const Vector3& b)
			{
				return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
			}

			// Vector subtraction (a - b)
			friend Vector3 operator-(const Vector3& a, const Vector3& b)
			{
				return Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
			}

			// Component-wise multiplication
			friend Vector3 operator*(const Vector3& left, const Vector3& right)
			{
				return Vector3(left.x * right.x, left.y * right.y, left.z * right.z);
			}

			// Scalar multiplication
			friend Vector3 operator*(const Vector3& vec, float scalar)
			{
				return Vector3(vec.x * scalar, vec.y * scalar, vec.z * scalar);
			}

			// Scalar multiplication
			friend Vector3 operator*(float scalar, const Vector3& vec)
			{
				return Vector3(vec.x * scalar, vec.y * scalar, vec.z * scalar);
			}

			// Scalar *=
			Vector3& operator*=(float scalar)
			{
				x *= scalar;
				y *= scalar;
				z *= scalar;
				return *this;
			}

			// Vector +=
			Vector3& operator+=(const Vector3& right)
			{
				x += right.x;
				y += right.y;
				z += right.z;
				return *this;
			}

			// Vector -=
			Vector3& operator-=(const Vector3& right)
			{
				x -= right.x;
				y -= right.y;
				z -= right.z;
				return *this;
			}

			// Length squared of vector
			float LengthSq() const
			{
				return (x * x + y * y + z * z);
			}

			// Length of vector
			float Length() const
			{
				return (Math::Sqrt(LengthSq()));
			}

			// Normalize this vector
			void Normalize()
			{
				float length = Length();
				x /= length;
				y /= length;
				z /= length;
			}

			// Normalize the provided vector
			static Vector3 Normalize(const Vector3& vec)
			{
				Vector3 temp = vec;
				temp.Normalize();
				return temp;
			}

			// Dot product between two vectors (a dot b)
			static float Dot(const Vector3& a, const Vector3& b)
			{
				return (a.x * b.x + a.y * b.y + a.z * b.z);
			}

			// Cross product between two vectors (a cross b)
			static Vector3 Cross(const Vector3& a, const Vector3& b)
			{
				Vector3 temp;
				temp.x = a.y * b.z - a.z * b.y;
				temp.y = a.z * b.x - a.x * b.z;
				temp.z = a.x * b.y - a.y * b.x;
				return temp;
			}

			// Lerp from A to B by f
			static Vector3 Lerp(const Vector3& a, const Vector3& b, float f)
			{
				return Vector3(a + f * (b - a));
			}

			// Reflect V about (normalized) N
			static Vector3 Reflect(const Vector3& v, const Vector3& n)
			{
				return v - 2.0f * Vector3::Dot(v, n) * n;
			}

			static Vector3 Transform(const Vector3& vec, const class Matrix4& mat, float w = 1.0f);
			// This will transform the vector and renormalize the w component
			static Vector3 TransformWithPerspDiv(const Vector3& vec, const class Matrix4& mat, float w = 1.0f);

			// Transform a Vector3 by a quaternion
			static Vector3 Transform(const Vector3& v, const class Quaternion& q);

		    static const Vector3 Zero;
		    static const Vector3 UnitX;
		    static const Vector3 UnitY;
		    static const Vector3 UnitZ;
		    static const Vector3 NegUnitX;
		    static const Vector3 NegUnitY;
		    static const Vector3 NegUnitZ;
		    static const Vector3 Infinity;
		    static const Vector3 NegInfinity;
		};

		// 3x3 Matrix
		class Matrix3
		{
		public:
			float mat[3][3];

			Matrix3()
			{
				*this = Matrix3::Identity;
			}

			explicit Matrix3(float inMat[3][3])
			{
				memcpy(mat, inMat, 9 * sizeof(float));
			}

			// Cast to a const float pointer
			const float* GetAsFloatPtr() const
			{
				return reinterpret_cast<const float*>(&mat[0][0]);
			}

			// Matrix multiplication
			friend Matrix3 operator*(const Matrix3& left, const Matrix3& right)
			{
				Matrix3 retVal;
				// row 0
				retVal.mat[0][0] =
					left.mat[0][0] * right.mat[0][0] +
					left.mat[0][1] * right.mat[1][0] +
					left.mat[0][2] * right.mat[2][0];

				retVal.mat[0][1] =
					left.mat[0][0] * right.mat[0][1] +
					left.mat[0][1] * right.mat[1][1] +
					left.mat[0][2] * right.mat[2][1];

				retVal.mat[0][2] =
					left.mat[0][0] * right.mat[0][2] +
					left.mat[0][1] * right.mat[1][2] +
					left.mat[0][2] * right.mat[2][2];

				// row 1
				retVal.mat[1][0] =
					left.mat[1][0] * right.mat[0][0] +
					left.mat[1][1] * right.mat[1][0] +
					left.mat[1][2] * right.mat[2][0];

				retVal.mat[1][1] =
					left.mat[1][0] * right.mat[0][1] +
					left.mat[1][1] * right.mat[1][1] +
					left.mat[1][2] * right.mat[2][1];

				retVal.mat[1][2] =
					left.mat[1][0] * right.mat[0][2] +
					left.mat[1][1] * right.mat[1][2] +
					left.mat[1][2] * right.mat[2][2];

				// row 2
				retVal.mat[2][0] =
					left.mat[2][0] * right.mat[0][0] +
					left.mat[2][1] * right.mat[1][0] +
					left.mat[2][2] * right.mat[2][0];

				retVal.mat[2][1] =
					left.mat[2][0] * right.mat[0][1] +
					left.mat[2][1] * right.mat[1][1] +
					left.mat[2][2] * right.mat[2][1];

				retVal.mat[2][2] =
					left.mat[2][0] * right.mat[0][2] +
					left.mat[2][1] * right.mat[1][2] +
					left.mat[2][2] * right.mat[2][2];

				return retVal;
			}

			Matrix3& operator*=(const Matrix3& right)
			{
				*this = *this * right;
				return *this;
			}

			// Create a scale matrix with x and y scales
			static Matrix3 CreateScale(float xScale, float yScale)
			{
				float temp[3][3] =
				{
					{ xScale, 0.0f, 0.0f },
					{ 0.0f, yScale, 0.0f },
					{ 0.0f, 0.0f, 1.0f },
				};
				return Matrix3(temp);
			}

			static Matrix3 CreateScale(const Vector2& scaleVector)
			{
				return CreateScale(scaleVector.x, scaleVector.y);
			}

			// Create a scale matrix with a uniform factor
			static Matrix3 CreateScale(float scale)
			{
				return CreateScale(scale, scale);
			}

			// Create a rotation matrix about the Z axis
			// theta is in radians
			static Matrix3 CreateRotation(float theta)
			{
				float temp[3][3] =
				{
					{ Math::Cos(theta), Math::Sin(theta), 0.0f },
					{ -Math::Sin(theta), Math::Cos(theta), 0.0f },
					{ 0.0f, 0.0f, 1.0f },
				};
				return Matrix3(temp);
			}

			// Create a translation matrix (on the xy-plane)
			static Matrix3 CreateTranslation(const Vector2& trans)
			{
				float temp[3][3] =
				{
					{ 1.0f, 0.0f, 0.0f },
					{ 0.0f, 1.0f, 0.0f },
					{ trans.x, trans.y, 1.0f },
				};
				return Matrix3(temp);
			}

			static const Matrix3 Identity;
		};

		// 4x4 Matrix
		class Matrix4
		{
		public:
			float mat[4][4];

			Matrix4()
			{
				*this = Matrix4::Identity;
			}

			explicit Matrix4(float inMat[4][4])
			{
				memcpy(mat, inMat, 16 * sizeof(float));
			}

			// Cast to a const float pointer
			const float* GetAsFloatPtr() const
			{
				return reinterpret_cast<const float*>(&mat[0][0]);
			}

			// Matrix multiplication (a * b)
			friend Matrix4 operator*(const Matrix4& a, const Matrix4& b)
			{
				Matrix4 retVal;
				// row 0
				retVal.mat[0][0] =
					a.mat[0][0] * b.mat[0][0] +
					a.mat[0][1] * b.mat[1][0] +
					a.mat[0][2] * b.mat[2][0] +
					a.mat[0][3] * b.mat[3][0];

				retVal.mat[0][1] =
					a.mat[0][0] * b.mat[0][1] +
					a.mat[0][1] * b.mat[1][1] +
					a.mat[0][2] * b.mat[2][1] +
					a.mat[0][3] * b.mat[3][1];

				retVal.mat[0][2] =
					a.mat[0][0] * b.mat[0][2] +
					a.mat[0][1] * b.mat[1][2] +
					a.mat[0][2] * b.mat[2][2] +
					a.mat[0][3] * b.mat[3][2];

				retVal.mat[0][3] =
					a.mat[0][0] * b.mat[0][3] +
					a.mat[0][1] * b.mat[1][3] +
					a.mat[0][2] * b.mat[2][3] +
					a.mat[0][3] * b.mat[3][3];

				// row 1
				retVal.mat[1][0] =
					a.mat[1][0] * b.mat[0][0] +
					a.mat[1][1] * b.mat[1][0] +
					a.mat[1][2] * b.mat[2][0] +
					a.mat[1][3] * b.mat[3][0];

				retVal.mat[1][1] =
					a.mat[1][0] * b.mat[0][1] +
					a.mat[1][1] * b.mat[1][1] +
					a.mat[1][2] * b.mat[2][1] +
					a.mat[1][3] * b.mat[3][1];

				retVal.mat[1][2] =
					a.mat[1][0] * b.mat[0][2] +
					a.mat[1][1] * b.mat[1][2] +
					a.mat[1][2] * b.mat[2][2] +
					a.mat[1][3] * b.mat[3][2];

				retVal.mat[1][3] =
					a.mat[1][0] * b.mat[0][3] +
					a.mat[1][1] * b.mat[1][3] +
					a.mat[1][2] * b.mat[2][3] +
					a.mat[1][3] * b.mat[3][3];

				// row 2
				retVal.mat[2][0] =
					a.mat[2][0] * b.mat[0][0] +
					a.mat[2][1] * b.mat[1][0] +
					a.mat[2][2] * b.mat[2][0] +
					a.mat[2][3] * b.mat[3][0];

				retVal.mat[2][1] =
					a.mat[2][0] * b.mat[0][1] +
					a.mat[2][1] * b.mat[1][1] +
					a.mat[2][2] * b.mat[2][1] +
					a.mat[2][3] * b.mat[3][1];

				retVal.mat[2][2] =
					a.mat[2][0] * b.mat[0][2] +
					a.mat[2][1] * b.mat[1][2] +
					a.mat[2][2] * b.mat[2][2] +
					a.mat[2][3] * b.mat[3][2];

				retVal.mat[2][3] =
					a.mat[2][0] * b.mat[0][3] +
					a.mat[2][1] * b.mat[1][3] +
					a.mat[2][2] * b.mat[2][3] +
					a.mat[2][3] * b.mat[3][3];

				// row 3
				retVal.mat[3][0] =
					a.mat[3][0] * b.mat[0][0] +
					a.mat[3][1] * b.mat[1][0] +
					a.mat[3][2] * b.mat[2][0] +
					a.mat[3][3] * b.mat[3][0];

				retVal.mat[3][1] =
					a.mat[3][0] * b.mat[0][1] +
					a.mat[3][1] * b.mat[1][1] +
					a.mat[3][2] * b.mat[2][1] +
					a.mat[3][3] * b.mat[3][1];

				retVal.mat[3][2] =
					a.mat[3][0] * b.mat[0][2] +
					a.mat[3][1] * b.mat[1][2] +
					a.mat[3][2] * b.mat[2][2] +
					a.mat[3][3] * b.mat[3][2];

				retVal.mat[3][3] =
					a.mat[3][0] * b.mat[0][3] +
					a.mat[3][1] * b.mat[1][3] +
					a.mat[3][2] * b.mat[2][3] +
					a.mat[3][3] * b.mat[3][3];

				return retVal;
			}

			Matrix4& operator*=(const Matrix4& right)
			{
				*this = *this * right;
				return *this;
			}

			// Invert the matrix - super slow
			void Invert();

			// Get the translation component of the matrix
			Vector3 GetTranslation() const
			{
				return Vector3(mat[3][0], mat[3][1], mat[3][2]);
			}

			// Get the X axis of the matrix (forward)
			Vector3 GetXAxis() const
			{
				return Vector3::Normalize(Vector3(mat[0][0], mat[0][1], mat[0][2]));
			}

			// Get the Y axis of the matrix (left)
			Vector3 GetYAxis() const
			{
				return Vector3::Normalize(Vector3(mat[1][0], mat[1][1], mat[1][2]));
			}

			// Get the Z axis of the matrix (up)
			Vector3 GetZAxis() const
			{
				return Vector3::Normalize(Vector3(mat[2][0], mat[2][1], mat[2][2]));
			}

			// Extract the scale component from the matrix
			Vector3 GetScale() const
			{
				Vector3 retVal;
				retVal.x = Vector3(mat[0][0], mat[0][1], mat[0][2]).Length();
				retVal.y = Vector3(mat[1][0], mat[1][1], mat[1][2]).Length();
				retVal.z = Vector3(mat[2][0], mat[2][1], mat[2][2]).Length();
				return retVal;
			}

			// Create a scale matrix with x, y, and z scales
			static Matrix4 CreateScale(float xScale, float yScale, float zScale)
			{
				float temp[4][4] =
				{
					{ xScale, 0.0f, 0.0f, 0.0f },
					{ 0.0f, yScale, 0.0f, 0.0f },
					{ 0.0f, 0.0f, zScale, 0.0f },
					{ 0.0f, 0.0f, 0.0f, 1.0f }
				};
				return Matrix4(temp);
			}

			static Matrix4 CreateScale(const Vector3& scaleVector)
			{
				return CreateScale(scaleVector.x, scaleVector.y, scaleVector.z);
			}

			// Create a scale matrix with a uniform factor
			static Matrix4 CreateScale(float scale)
			{
				return CreateScale(scale, scale, scale);
			}

			// Rotation about x-axis
			static Matrix4 CreateRotationX(float theta)
			{
				float temp[4][4] =
				{
					{ 1.0f, 0.0f, 0.0f , 0.0f },
					{ 0.0f, Math::Cos(theta), Math::Sin(theta), 0.0f },
					{ 0.0f, -Math::Sin(theta), Math::Cos(theta), 0.0f },
					{ 0.0f, 0.0f, 0.0f, 1.0f },
				};
				return Matrix4(temp);
			}

			// Rotation about y-axis
			static Matrix4 CreateRotationY(float theta)
			{
				float temp[4][4] =
				{
					{ Math::Cos(theta), 0.0f, -Math::Sin(theta), 0.0f },
					{ 0.0f, 1.0f, 0.0f, 0.0f },
					{ Math::Sin(theta), 0.0f, Math::Cos(theta), 0.0f },
					{ 0.0f, 0.0f, 0.0f, 1.0f },
				};
				return Matrix4(temp);
			}

			// Rotation about z-axis
			static Matrix4 CreateRotationZ(float theta)
			{
				float temp[4][4] =
				{
					{ Math::Cos(theta), Math::Sin(theta), 0.0f, 0.0f },
					{ -Math::Sin(theta), Math::Cos(theta), 0.0f, 0.0f },
					{ 0.0f, 0.0f, 1.0f, 0.0f },
					{ 0.0f, 0.0f, 0.0f, 1.0f },
				};
				return Matrix4(temp);
			}

		
				


			// Create a rotation matrix from a quaternion
			static Matrix4 CreateFromQuaternion(const class Quaternion& q);

			static Matrix4 CreateTranslation(const Vector3& trans)
			{
				float temp[4][4] =
				{
					{ 1.0f, 0.0f, 0.0f, 0.0f },
					{ 0.0f, 1.0f, 0.0f, 0.0f },
					{ 0.0f, 0.0f, 1.0f, 0.0f },
					{ trans.x, trans.y, trans.z, 1.0f }
				};
				return Matrix4(temp);
			}

			static Matrix4 CreateLookAt(const Vector3& eye, const Vector3& target, const Vector3& up)
			{
				Vector3 zaxis = Vector3::Normalize(target - eye);
				Vector3 xaxis = Vector3::Normalize(Vector3::Cross(up, zaxis));
				Vector3 yaxis = Vector3::Normalize(Vector3::Cross(zaxis, xaxis));
				Vector3 trans;
				trans.x = -Vector3::Dot(xaxis, eye);
				trans.y = -Vector3::Dot(yaxis, eye);
				trans.z = -Vector3::Dot(zaxis, eye);

				float temp[4][4] =
				{
					{ xaxis.x, yaxis.x, zaxis.x, 0.0f },
					{ xaxis.y, yaxis.y, zaxis.y, 0.0f },
					{ xaxis.z, yaxis.z, zaxis.z, 0.0f },
					{ trans.x, trans.y, trans.z, 1.0f }
				};
				return Matrix4(temp);
			}

			static Matrix4 CreateOrtho(float width, float height, float near_, float far_)
			{
				float temp[4][4] =
				{
					{ 2.0f / width, 0.0f, 0.0f, 0.0f },
					{ 0.0f, 2.0f / height, 0.0f, 0.0f },
					{ 0.0f, 0.0f, 1.0f / (far_ - near_), 0.0f },
					{ 0.0f, 0.0f, near_ / (near_ - far_), 1.0f }
				};
				return Matrix4(temp);
			}

			static Matrix4 CreatePerspectiveFOV(float fovY, float width, float height, float near_, float far_)
			{
				float yScale = Math::Cot(fovY / 2.0f);
				float xScale = yScale * height / width;
				float temp[4][4] =
				{
					{ xScale, 0.0f, 0.0f, 0.0f },
					{ 0.0f, yScale, 0.0f, 0.0f },
					{ 0.0f, 0.0f, far_ / (far_ - near_), 1.0f },
					{ 0.0f, 0.0f, -near_ * far_ / (far_ - near_), 0.0f }
				};
				return Matrix4(temp);
			}

			// Create "Simple" View-Projection Matrix from Chapter 6
			static Matrix4 CreateSimpleViewProj(float width, float height)
			{
				float temp[4][4] =
				{
					{ 2.0f / width, 0.0f, 0.0f, 0.0f },
					{ 0.0f, 2.0f / height, 0.0f, 0.0f },
					{ 0.0f, 0.0f, 1.0f, 0.0f },
					{ 0.0f, 0.0f, 1.0f, 1.0f }
				};
				return Matrix4(temp);
			}

			static const Matrix4 Identity;
		};

		// (Unit) Quaternion
		class Quaternion
		{
		public:
			float x;
			float y;
			float z;
			float w;

			Quaternion()
			{
				*this = Quaternion::Identity;
			}

			// This directly sets the quaternion components --
			// don't use for axis/angle
			explicit Quaternion(float inX, float inY, float inZ, float inW)
			{
				Set(inX, inY, inZ, inW);
			}

			// Construct the quaternion from an axis and angle
			// It is assumed that axis is already normalized,
			// and the angle is in radians
			explicit Quaternion(const Vector3& axis, float angle)
			{
				float scalar = Math::Sin(angle / 2.0f);
				x = axis.x * scalar;
				y = axis.y * scalar;
				z = axis.z * scalar;
				w = Math::Cos(angle / 2.0f);
			}

			// Directly set the internal components
			void Set(float inX, float inY, float inZ, float inW)
			{
				x = inX;
				y = inY;
				z = inZ;
				w = inW;
			}

			void Conjugate()
			{
				x *= -1.0f;
				y *= -1.0f;
				z *= -1.0f;
			}

			float LengthSq() const
			{
				return (x * x + y * y + z * z + w * w);
			}

			float Length() const
			{
				return Math::Sqrt(LengthSq());
			}

			void Normalize()
			{
				float length = Length();
				x /= length;
				y /= length;
				z /= length;
				w /= length;
			}

			// Normalize the provided quaternion
			static Quaternion Normalize(const Quaternion& q)
			{
				Quaternion retVal = q;
				retVal.Normalize();
				return retVal;
			}

			// Linear interpolation
			static Quaternion Lerp(const Quaternion& a, const Quaternion& b, float f)
			{
				Quaternion retVal;
				retVal.x = Math::Lerp(a.x, b.x, f);
				retVal.y = Math::Lerp(a.y, b.y, f);
				retVal.z = Math::Lerp(a.z, b.z, f);
				retVal.w = Math::Lerp(a.w, b.w, f);
				retVal.Normalize();
				return retVal;
			}

			static float Dot(const Quaternion& a, const Quaternion& b)
			{
				return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			}

			// Spherical Linear Interpolation
			static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float f)
			{
				float rawCosm = Quaternion::Dot(a, b);

				float cosom = -rawCosm;
				if (rawCosm >= 0.0f)
				{
					cosom = rawCosm;
				}

				float scale0, scale1;

				if (cosom < 0.9999f)
				{
					const float omega = Math::Acos(cosom);
					const float invSin = 1.f / Math::Sin(omega);
					scale0 = Math::Sin((1.f - f) * omega) * invSin;
					scale1 = Math::Sin(f * omega) * invSin;
				}
				else
				{
					// Use linear interpolation if the quaternions
					// are collinear
					scale0 = 1.0f - f;
					scale1 = f;
				}

				if (rawCosm < 0.0f)
				{
					scale1 = -scale1;
				}

				Quaternion retVal;
				retVal.x = scale0 * a.x + scale1 * b.x;
				retVal.y = scale0 * a.y + scale1 * b.y;
				retVal.z = scale0 * a.z + scale1 * b.z;
				retVal.w = scale0 * a.w + scale1 * b.w;
				retVal.Normalize();
				return retVal;
			}

			// Concatenate
			// Rotate by q FOLLOWED BY p
			static Quaternion Concatenate(const Quaternion& q, const Quaternion& p)
			{
				Quaternion retVal;

				// Vector component is:
				// ps * qv + qs * pv + pv x qv
				Vector3 qv(q.x, q.y, q.z);
				Vector3 pv(p.x, p.y, p.z);
				Vector3 newVec = p.w * qv + q.w * pv + Vector3::Cross(pv, qv);
				retVal.x = newVec.x;
				retVal.y = newVec.y;
				retVal.z = newVec.z;

				// Scalar component is:
				// ps * qs - pv . qv
				retVal.w = p.w * q.w - Vector3::Dot(pv, qv);

				return retVal;
			}

			static const Quaternion Identity;
		};


		template<size_t N>
		class Vec
		{
		public:
			Vec(): m_N(N), m_Data(N) {}	// default constructor
			Vec(const Vec& rhs);
			Vec& operator = (const Vec& rhs);
			Vec& operator = (const std::vector<float>& rhs);
			Vec(const std::vector<float>& rhs) : m_N(N), m_Data(rhs) {}	// constructor from vector


			float  operator[] (const int idx) const { return m_Data[idx]; }
			float& operator[] (const int idx) { return m_Data[idx]; }
			const Vec& operator *= (float rhs);
			Vec			operator * (float rhs) const;
			Vec			operator + (const Vec& rhs) const;
			Vec			operator - (const Vec& rhs) const;
			const Vec& operator += (const Vec& rhs);
			const Vec& operator -= (const Vec& rhs);

			float Dot(const Vec& rhs) const;
			void Zero();

		public:
			size_t m_N{N};
			std::vector<float> m_Data;
		};


		template<size_t N>
		inline Vec<N>::Vec(const Vec& rhs)
		{
			m_N = rhs.m_N;
			m_Data = rhs.m_Data;
		}

		template<size_t N>
		inline Vec<N>& Vec<N>::operator=(const Vec& rhs)
		{
			m_N = rhs.m_N;
			m_Data = rhs.m_Data;
			return *this;
		}

		template<size_t N>
		inline Vec<N>& Vec<N>::operator=(const std::vector<float>& rhs)
		{
			m_N = rhs.size();
			m_Data = rhs;
			return *this;
		}

		template<size_t N>
		inline const Vec<N>& Vec<N>::operator*=(float rhs)
		{
			for (size_t i = 0; i < m_N; ++i)
			{
				m_Data[i] *= rhs;
			}
			return *this;
		}

		template<size_t N>
		inline Vec<N> Vec<N>::operator*(float rhs) const
		{
			Vec<N> retVal = *this;
			retVal *= rhs;
			return retVal;
		}

		template<size_t N>
		inline Vec<N> Vec<N>::operator+(const Vec& rhs) const
		{
			Vec<N> retVal = *this;
			retVal += rhs;
			return retVal;
		}

		template<size_t N>
		inline Vec<N> Vec<N>::operator-(const Vec& rhs) const
		{
			Vec<N> retVal = *this;
			retVal -= rhs;
			return retVal;
		}

		template<size_t N>
		inline const Vec<N>& Vec<N>::operator+=(const Vec& rhs)
		{
			for (int i = 0; i < m_N; i++) {
				m_Data[i] += rhs.m_Data[i];
			}
			return *this;
		}

		template<size_t N>
		inline const Vec<N>& Vec<N>::operator-=(const Vec& rhs)
		{
			for (int i = 0; i < m_N; i++) {
				m_Data[i] -= rhs.m_Data[i];
			}
			return *this;
		}

		template<size_t N>
		inline float Vec<N>::Dot(const Vec& rhs) const
		{
			float sum = 0;
			for (int i = 0; i < N; i++) {
				sum += m_Data[i] * rhs.m_Data[i];
			}
			return sum;
		}

		template<size_t N>
		inline void Vec<N>::Zero()
		{
			std::fill(m_Data.begin(), m_Data.end(), 0.f);
		}

		template<size_t M, size_t N>
		class Mat
		{
		public:
			Mat() : m_NumOfRow(M), m_NumOfColumn(N), m_Data(M) {}	// default constructor

			Mat(const Mat& rhs) {
				*this = rhs;
			}

			Vec<N> operator[] (const int idx) const { return m_Data[idx]; }
			Vec<N>& operator[] (const int idx) { return m_Data[idx]; }

			const Mat& operator = (const Mat& rhs);
			const Mat& operator *= (float rhs);

			Vec<M> operator * (const Vec<N>& rhs) const;

			template<size_t RC>
			Mat<M, RC> operator * (const Mat<N, RC>& rhs) const;
			Mat operator * (float rhs) const;

			void Zero();
			Mat<N, M> Transpose() const;

		public:
			size_t m_NumOfRow{M};	// M rows
			size_t m_NumOfColumn{N};	// N columns
			std::vector<Vec<N>> m_Data{M};
		};

		/*template<size_t N>
		class Mat 
		{
		public:
			Mat() = default;
			Mat(const Mat& rhs) {
				*this = rhs;
			}
			~Mat() {}

			const Mat& operator = (const Mat& rhs);
			
			void Identity();
			void Zero();
			void Transpose();

			void operator *= (float rhs);
			Vec<N> operator * (const Vec<N>& rhs);
			Mat operator * (const Mat& rhs);

		public:
			size_t m_Dimension{N};
			std::vector<Vec<N>> m_Rows{N};
		};*/


		template<size_t M, size_t N>
		inline const Mat<M, N>& Mat<M, N>::operator=(const Mat& rhs)
		{
			// TODO: insert return statement here
			m_NumOfRow = rhs.m_NumOfRow;
			m_NumOfColumn = rhs.m_NumOfColumn;
			m_Data = rhs.m_Data;
			return *this;
		}

		template<size_t M, size_t N>
		inline const Mat<M, N>& Mat<M, N>::operator*=(float rhs)
		{
			for (int m = 0; m < M; m++) {
				m_Data[m] *= rhs;
			}
			return *this;
		}

		template<size_t M, size_t N>
		inline Vec<M> Mat<M, N>::operator*(const Vec<N>& rhs) const
		{
			Vec<M> retVal{};
			for (int m = 0; m < M; m++) {
				retVal[m] = m_Data[m].Dot(rhs);
			}
			return retVal;
		}

		template<size_t M, size_t N>
		inline Mat<M, N> Mat<M, N>::operator*(float rhs) const
		{
			Mat<M, N> retVal = *this;
			retVal *= rhs;
			return retVal;
		}

		template<size_t M, size_t N>
		inline void Mat<M, N>::Zero()
		{
			for (int i = 0; i < M; i++)
			{
				m_Data[i].Zero();
			}
		}

		template<size_t M, size_t N>
		inline Mat<N, M> Mat<M, N>::Transpose() const
		{
			Mat<N, M> retVal;
			for (int i = 0; i < N; i++)
			{
				for (int j = 0; j < M; j++)
				{
					retVal.m_Data[i][j] = m_Data[j][i];
				}
			}

			return retVal;
		}

	

		template<size_t M, size_t N>
		template<size_t RC>
		inline Mat<M, RC> Mat<M, N>::operator*(const Mat<N, RC>& rhs) const
		{
			Mat<M, RC> retVal;
			Mat<RC, N> tranposedRHS = rhs.Transpose();
			for (int m = 0; m < M; m++)
			{
				for (int j = 0; j < RC; j++)
				{
					retVal[m][j] = m_Data[m].Dot(tranposedRHS.m_Data[j]);
				}
			}
			return retVal;
		}

		template<size_t N>
		using MatN = Mat<N, N>;

		template<size_t dim, size_t size>
		Vec<size> LCP_GaussSeidel(const Mat<dim, dim>& A, const Vec<size>& b) {
			//const int N = b.m_N;
			Vec<size> x{};
			x.Zero();

			for (int iter = 0; iter < size; iter++) {
				for (int i = 0; i < size; i++) {
					float dx = (b[i] - A[i].Dot(x)) / A[i][i];
					if (dx * 0.0f == dx * 0.0f) {
						x[i] = x[i] + dx;
					}
				}
			}
			return x;
		}

		template<size_t dim, size_t size>
		Vec<size> LCP_GaussSeidelVerbose(const Mat<dim, dim>& A, const Vec<size>& b) {
			//const int N = b.m_N;
			Vec<size> x{};
			x.Zero();

			for (int iter = 0; iter < 5; iter++) {
				printf("Iteration %d: ", iter);
				for (int i = 0; i < size; i++) {
					float dx = (b[i] - A[i].Dot(x)) / A[i][i];
					if (dx * 0.0f == dx * 0.0f) {
						x[i] = x[i] + dx;
					}
					printf("%.2f ", x[i]);
				}
				printf("\n");
			}
			return x;
		}

		inline bool are_same_point(const Vec3f& a, const Vec3f& b) {
			double epsilon = 1e-6;
			return std::fabs(a.x - b.x) < epsilon && std::fabs(a.y - b.y) < epsilon && std::fabs(a.z - b.z);
		}

		inline std::vector<Vec3f> GenerateDiamond()
		{
			std::vector<Vec3f> Diamond(56, { 0, 0, 0 });
			Vec3f pts[4 + 4];
			/*pts[0] = Vec3f(0.1f, 0, -1);
			pts[1] = Vec3f(1, 0, 0);
			pts[2] = Vec3f(1, 0, 0.1f);
			pts[3] = Vec3f(0.4f, 0, 0.4f);*/
			pts[0] = Vec3f(0.1f, -1.f, 0);
			pts[1] = Vec3f(1, 0, 0);
			pts[2] = Vec3f(1, 0.1f, 0.f);
			pts[3] = Vec3f(0.4f, 0.4f, 0.f);

			const float pi = acosf(-1.0f);
			const Quat quatHalf = glm::angleAxis(2.0f * pi * 0.125f * 0.5f, Vec3f{ 0, 1, 0 });
			pts[4] = Vec3f(0.8f, 0.3f, 0.f);
			/*pts[4] = glm::transpose(glm::toMat3(quatHalf)) * pts[4];
			pts[5] = glm::transpose(glm::toMat3(quatHalf)) * pts[1];
			pts[6] = glm::transpose(glm::toMat3(quatHalf)) * pts[2];*/
			pts[4] = glm::toMat3(quatHalf) * pts[4];
			pts[5] = glm::toMat3(quatHalf) * pts[1];
			pts[6] = glm::toMat3(quatHalf) * pts[2];

			const Quat quat = glm::angleAxis(2.0f * pi * 0.125f, Vec3f{ 0, 1, 0 });
			int idx = 0;
			for (int i = 0; i < 7; i++) {
				Diamond[idx] = pts[i];
				idx++;
			}

			Quat quatAccumulator{ 1, 0, 0, 0 };
			for (int i = 1; i < 8; i++)
			{
				quatAccumulator = quatAccumulator * quat;
				for (int pt = 0; pt < 7; pt++) {
					//Diamond[idx] = glm::transpose(glm::toMat3(quatAccumulator)) * pts[pt];
					Diamond[idx] = glm::toMat3(quatAccumulator) * pts[pt];
					idx++;
				}
			}

			return Diamond;
		}

		inline unsigned char FloatToByte_n11(const float f) {
			int i = (int)(f * 127 + 128);
			return (unsigned char)i;
		}

}		
		


	namespace Color
	{
		static const Math::Vector3 Black(0.0f, 0.0f, 0.0f);
		static const Math::Vector3 White(1.0f, 1.0f, 1.0f);
		static const Math::Vector3 Red(1.0f, 0.0f, 0.0f);
		static const Math::Vector3 Green(0.0f, 1.0f, 0.0f);
		static const Math::Vector3 Blue(0.0f, 0.0f, 1.0f);
		static const Math::Vector3 Yellow(1.0f, 1.0f, 0.0f);
		static const Math::Vector3 LightYellow(1.0f, 1.0f, 0.88f);
		static const Math::Vector3 LightBlue(0.68f, 0.85f, 0.9f);
		static const Math::Vector3 LightPink(1.0f, 0.71f, 0.76f);
		static const Math::Vector3 LightGreen(0.56f, 0.93f, 0.56f);
	}



}
