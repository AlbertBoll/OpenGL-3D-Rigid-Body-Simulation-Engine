#pragma once

#include <array>
#include <cstddef>

namespace GEngine::SolverMath
{
	// Contact-only fixed storage. Keep float accumulation in ascending index order
	// so replacing Math::Vec/Mat does not change the approved solver arithmetic.
	template<std::size_t N>
	class Vector
	{
	public:
		float operator[](std::size_t index) const { return m_Data[index]; }
		float& operator[](std::size_t index) { return m_Data[index]; }
		void Zero() { m_Data.fill(0.0f); }

		Vector operator-(const Vector& rhs) const
		{
			Vector result = *this;
			for (std::size_t i = 0; i < N; ++i) result[i] -= rhs[i];
			return result;
		}

		Vector operator*(float rhs) const
		{
			Vector result = *this;
			for (std::size_t i = 0; i < N; ++i) result[i] *= rhs;
			return result;
		}

		float Dot(const Vector& rhs) const
		{
			float sum = 0.0f;
			for (std::size_t i = 0; i < N; ++i) sum += m_Data[i] * rhs[i];
			return sum;
		}

	private:
		std::array<float, N> m_Data{};
	};

	template<std::size_t Rows, std::size_t Columns>
	class Matrix
	{
	public:
		const Vector<Columns>& operator[](std::size_t row) const { return m_Data[row]; }
		Vector<Columns>& operator[](std::size_t row) { return m_Data[row]; }
		void Zero() { for (auto& row : m_Data) row.Zero(); }

		Matrix<Columns, Rows> Transpose() const
		{
			Matrix<Columns, Rows> result;
			for (std::size_t column = 0; column < Columns; ++column)
				for (std::size_t row = 0; row < Rows; ++row)
					result[column][row] = m_Data[row][column];
			return result;
		}

		Vector<Rows> operator*(const Vector<Columns>& rhs) const
		{
			Vector<Rows> result;
			for (std::size_t row = 0; row < Rows; ++row) result[row] = m_Data[row].Dot(rhs);
			return result;
		}

		template<std::size_t ResultColumns>
		Matrix<Rows, ResultColumns> operator*(const Matrix<Columns, ResultColumns>& rhs) const
		{
			Matrix<Rows, ResultColumns> result;
			const auto transpose = rhs.Transpose();
			for (std::size_t row = 0; row < Rows; ++row)
				for (std::size_t column = 0; column < ResultColumns; ++column)
					result[row][column] = m_Data[row].Dot(transpose[column]);
			return result;
		}

	private:
		std::array<Vector<Columns>, Rows> m_Data{};
	};
}
