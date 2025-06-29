#include "gepch.h"
#include "Mesh/Attribute.h"

namespace GEngine::Buffer
{
	template<typename T>
	Attribute<T>::Attribute()
	{
		//glGenVertexArrays(1, &m_VAO);
		glGenBuffers(1, &m_BufferRef);

		//glBindVertexArray(m_VAO);
		glBindBuffer(GL_ARRAY_BUFFER, m_BufferRef);

		glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(Vec3f), nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, b_Normalized, 0, (void*)0);
		glBindVertexArray(0);
	}

	template<typename T>
	Attribute<T>::Attribute(std::vector<T> data): m_Data(std::move(data))
	{
		glGenBuffers(1, &m_BufferRef);
		
	}

	template<typename T>
	void Attribute<T>::LoadData() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_BufferRef);
		glBufferData(GL_ARRAY_BUFFER, m_Data.size() * sizeof(T), m_Data.data(), GL_STATIC_DRAW);
	}

	template<typename T>
	void Attribute<T>::LoadAABBNullData() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_BufferRef);
		glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(Vec3f), nullptr, GL_DYNAMIC_DRAW);
	}

	template<typename T>
	void Attribute<T>::LoadKDTreeNullData() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_BufferRef);
		glBufferData(GL_ARRAY_BUFFER, 25000, nullptr, GL_DYNAMIC_DRAW); // No data for KDTree visualization
		//glBindBuffer(GL_ARRAY_BUFFER, 0);
	}


	template<typename T>
	void Attribute<T>::AssociateSlot(unsigned int location)
	{
		if (!m_Data.empty())
			LoadData();
		else
			LoadKDTreeNullData();
			//LoadAABBNullData();

		glEnableVertexAttribArray(location);


		if constexpr (std::is_same_v<T, Vec1f>)
		{
			glVertexAttribPointer(location, 1, GL_FLOAT, b_Normalized, 0, nullptr);
		}

		else if constexpr (std::is_same_v<T, Vec2f>)
		{
			glVertexAttribPointer(location, 2, GL_FLOAT, b_Normalized, 0, nullptr);
		}

		else if constexpr (std::is_same_v<T, Vec3f>)
		{
			glVertexAttribPointer(location, 3, GL_FLOAT, b_Normalized, 0, nullptr);
		}

		else if constexpr (std::is_same_v<T, Vec4f>)
		{
			glVertexAttribPointer(location, 4, GL_FLOAT, b_Normalized, 0, nullptr);
		}

		else if constexpr (std::is_same_v<T, Vec1i>)
		{
			glVertexAttribIPointer(location, 1, GL_INT, 0, nullptr);
		}

		else if constexpr (std::is_same_v<T, Vec2i>)
		{
			glVertexAttribIPointer(location, 2, GL_INT, 0, nullptr);
		}

		else if constexpr (std::is_same_v<T, Vec3i>)
		{
			glVertexAttribIPointer(location, 3, GL_INT, 0, nullptr);
		}

		else if constexpr (std::is_same_v<T, Vec4i>)
		{
			glVertexAttribIPointer(location, 4, GL_INT, 0, nullptr);
		}

	/*	else if constexpr (std::is_same_v<T, bool>)
		{
			glVertexAttribIPointer(location, 1, GL_INT, 0, nullptr);
		}*/

		else if constexpr (std::is_same_v<T, Mat2>)
		{
			for (uint8_t i = 0; i < 2; i++)
			{
				glVertexAttribPointer(location, 2, GL_FLOAT, b_Normalized, sizeof(Mat2), (void*)(i * sizeof(Vec2f) + 0));
				glVertexAttribDivisor(location, 1);
				glEnableVertexAttribArray(++location);
			}

		}

		else if constexpr (std::is_same_v<T, Mat3>)
		{
			for (uint8_t i = 0; i < 3; i++)
			{
				glVertexAttribPointer(location, 3, GL_FLOAT, b_Normalized, sizeof(Mat3), (void*)(i * sizeof(Vec3f) + 0));
				glVertexAttribDivisor(location, 1);
				glEnableVertexAttribArray(++location);
			}

		}

		else if constexpr (std::is_same_v<T, Mat4>)
		{
			for (uint8_t i = 0; i < 4; i++)
			{
				glVertexAttribPointer(location, 4, GL_FLOAT, b_Normalized, sizeof(Mat4), (void*)(i * sizeof(Vec4f) + 0));
				glVertexAttribDivisor(location, 1);
				glEnableVertexAttribArray(++location);
			}

		}

		else
		{
			GENGINE_CORE_ERROR("Unknown data type: {}", typeid(T).name());
		}


		glBindBuffer(GL_ARRAY_BUFFER, 0);

	}

	//template<typename T>
	//void Attribute<T>::AssociateAttributeName(unsigned int programRef, const std::string& variableName)
	//{
	//	LoadData();

	//	GLint variableRef = glGetAttribLocation(programRef, variableName.c_str());

	//	if (variableRef == -1) return;

	//	glEnableVertexAttribArray(variableRef);

	//	if constexpr (std::is_same_v<T, Vec1f>)
	//	{
	//		glVertexAttribPointer(variableRef, 1, GL_FLOAT, b_Normalized, 0, nullptr);
	//	}

	//	else if constexpr (std::is_same_v<T, Vec2f>)
	//	{
	//		glVertexAttribPointer(variableRef, 2, GL_FLOAT, b_Normalized, 0, nullptr);
	//	}

	//	else if constexpr (std::is_same_v<T, Vec3f>)
	//	{
	//		glVertexAttribPointer(variableRef, 3, GL_FLOAT, b_Normalized, 0, nullptr);
	//	}

	//	else if constexpr (std::is_same_v<T, Vec4f>)
	//	{
	//		glVertexAttribPointer(variableRef, 4, GL_FLOAT, b_Normalized, 0, nullptr);
	//	}

	//	else if constexpr (std::is_same_v<T, Vec1i>)
	//	{
	//		glVertexAttribIPointer(variableRef, 1, GL_INT, 0, nullptr);
	//	}

	//	else if constexpr (std::is_same_v<T, Vec2i>)
	//	{
	//		glVertexAttribIPointer(variableRef, 2, GL_INT, 0, nullptr);
	//	}

	//	else if constexpr (std::is_same_v<T, Vec3i>)
	//	{
	//		glVertexAttribIPointer(variableRef, 3, GL_INT, 0, nullptr);
	//	}

	//	else if constexpr (std::is_same_v<T, Vec4i>)
	//	{
	//		glVertexAttribIPointer(variableRef, 4, GL_INT, 0, nullptr);
	//	}

	//	/*else if constexpr (std::is_same_v<T, bool>)
	//	{
	//		glVertexAttribIPointer(variableRef, 1, GL_INT, 0, nullptr);
	//	}*/

	//	else if constexpr (std::is_same_v<T, Mat2>)
	//	{
	//		for (uint8_t i = 0; i < 2; i++)
	//		{
	//			glVertexAttribPointer(variableRef, 2, GL_FLOAT, b_Normalized, sizeof(Mat2), (void*)(i * sizeof(Vec2f) + 0));
	//			glVertexAttribDivisor(variableRef, 1);
	//			glEnableVertexAttribArray(++variableRef);
	//		}

	//	}

	//	else if constexpr (std::is_same_v<T, Mat3>)
	//	{
	//		for (uint8_t i = 0; i < 3; i++)
	//		{
	//			glVertexAttribPointer(variableRef, 3, GL_FLOAT, b_Normalized, sizeof(Mat3), (void*)(i * sizeof(Vec3f) + 0));
	//			glVertexAttribDivisor(variableRef, 1);
	//			glEnableVertexAttribArray(++variableRef);
	//		}

	//	}

	//	else if constexpr (std::is_same_v<T, Mat4>)
	//	{
	//		for (uint8_t i = 0; i < 4; i++)
	//		{
	//			glVertexAttribPointer(variableRef, 4, GL_FLOAT, b_Normalized, sizeof(Mat4), (void*)(i * sizeof(Vec4f) + 0));
	//			glVertexAttribDivisor(variableRef, 1);
	//			glEnableVertexAttribArray(++variableRef);
	//		}

	//	}


	//	else
	//	{
	//		GENGINE_CORE_ERROR("Unknown data type: {}", typeid(T).name());
	//	}

	//	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//}


	template class Attribute<Vec1f>;
	template class Attribute<Vec2f>;
	template class Attribute<Vec3f>;
	template class Attribute<Vec4f>;
	template class Attribute<Vec1i>;
	template class Attribute<Vec2i>;
	template class Attribute<Vec3i>;
	template class Attribute<Vec4i>;
	//template class Attribute<bool>;
	template class Attribute<Mat2>;
	template class Attribute<Mat3>;
	template class Attribute<Mat4>;


}