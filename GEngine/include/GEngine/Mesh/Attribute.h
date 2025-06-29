#pragma once
#include "Math/Math.h"

namespace GEngine::Buffer
{



	using namespace Math;
	template<typename T>
	class Attribute
	{

	public:
		std::vector<T> m_Data;
		unsigned int m_BufferRef{};
		bool b_Normalized = false;
	

	public:

		//Attribute() = default;
		Attribute();
		Attribute(std::vector<T> data);
	

		[[nodiscard]] unsigned int GetBufferRef()const { return m_BufferRef; }

		void LoadData() const;
		void LoadAABBNullData() const;
		void LoadKDTreeNullData() const;


		void AddData(const std::vector<T>& data)
		{
			m_Data.insert(m_Data.end(), data.begin(), data.end());
		}

		void AssociateSlot(unsigned int location);

		//void AssociateAttributeName(unsigned int programRef, const std::string& variableName);


	};


};