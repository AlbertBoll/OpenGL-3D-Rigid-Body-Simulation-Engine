#pragma once
#include "Events/Signal.h"

#define Setter(member) void SetProperty(const decltype(member)& p){\
std::cout << "Property changes to "<< p.m_Property<<std::endl;\
									 member = p;\
}\

#define Getter(member) decltype(member)::PropertyType Get_##member()const{\
							return member;\
}\
                    
#define GetterAndSetter(member) Getter(member);\
                                Setter(member);\
							


namespace GEngine
{
	template<typename T>
	class Property
	{
		
	public:
	
		using PropertyType = T;

		T m_Property{};


		Signal<void(const Property&)> OnPropertyChangeSignal;

		Property(const T& property) : m_Property{ property } {}

		Property() = default;

		Property(const Property& other)
		{
			m_Property = other.m_Property;
		}

		Property& operator = (const Property& other)
		{
			if (this != &other)
			{
				if (m_Property != other.m_Property)
				{
					m_Property = other.m_Property;
					OnPropertyChangeSignal.Emit(*this);
				}
			}

			return *this;
		}

		operator T()const { return m_Property; }

		Property<T> operator + (const Property<T>& other)
		{
			return m_Property + other.m_Property;
		}

		Property<T>& operator += (const Property<T>& other)
		{
			m_Property += other.m_Property;
			OnPropertyChangeSignal.Emit(*this);
			return *this;
		}

		Property<T> operator - (const Property<T>& other)
		{
			return m_Property - other.m_Property;
		}

		Property<T>& operator -= (const Property<T>& other)
		{
			m_Property -= other.m_Property;
			OnPropertyChangeSignal.Emit(*this);
			return *this;
		}

		Property<T> operator * (const Property<T>& other)
		{
			return m_Property * other.m_Property;
		}

		Property<T>& operator *= (const Property<T>& other)
		{
			m_Property *= other.m_Property;
			OnPropertyChangeSignal.Emit(*this);
			return *this;
		}

		template<typename ... Ts>
		Property& Emplace(Ts&& ... ts)
		{
			::new(&m_Property) T(std::forward<Ts>(ts)...);
		}

		friend std::ostream& operator<<(std::ostream& os, const Property& m){ os << m.m_Property; return os; }
	};



}
 