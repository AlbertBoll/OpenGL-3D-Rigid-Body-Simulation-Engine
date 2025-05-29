#pragma once
#include<memory>
#include<utility>
#include<variant>


#define NONCOPYABLE(className) className(const className&) = delete;\
								className& operator=(const className&) = delete
	
#define NONMOVABLE(className) className(className&&) = delete;\
								className& operator=(className&&) = delete
	
#define NONCOPYMOVABLE(className)  NONCOPYABLE(className);\
									NONMOVABLE(className)
namespace GEngine
{
	template<typename T, typename deleter = std::default_delete<T>>
	using ScopedPtr = std::unique_ptr<T, deleter>;

	//using default deleter
	template<typename T, typename ... Args>
	constexpr ScopedPtr<T> CreateScopedPtr(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	//using custom deleter
	template<typename T, typename deleter, typename ... Args>
	constexpr ScopedPtr<T, deleter> CreateScopedPtr(Args&& ... args)
	{
		return std::unique_ptr<T, deleter>(new T{ std::forward<Args>(args)... });
	}


	template<typename T>
	using RefPtr = std::shared_ptr<T>;

	template<typename T, typename... Args>
	constexpr RefPtr<T> CreateRefPtr(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	template<typename T, typename deleter, typename... Args>
	constexpr RefPtr<T> CreateRefPtr(Args&& ... args)
	{
		return std::shared_ptr<T>(new T{ std::forward<Args>(args)... }, deleter{});
	}

	template<typename Enum, typename Value, typename = std::enable_if_t<std::is_enum_v<Enum>>>
	class BitFlags
	{
	public:
		template<typename ... Args>
		constexpr BitFlags(Args&& ... args) : m_Value{ Create<Enum>(std::forward<Args>(args)...) } {}

		template<typename T, typename ... Args>
		constexpr Value Create(T t, Args&&... args) const {
			return Create(t) | Create(std::forward<Args>(args)...);
		}

		template<typename T>
		constexpr Value Create(T t) const {
			return static_cast<Value>(t);
		}

		template<typename T>
		constexpr Value Create() const { return 0; }

		constexpr bool IsSet(Enum e) const
		{
			return (m_Value & static_cast<Value>(e)) != 0;
		}

		void Combine(const BitFlags& other)
		{
			m_Value |= other.m_Value;
		}

		operator Value()const { return m_Value; }


	private:
		Value m_Value;
	};


	inline std::variant<std::false_type, std::true_type> bool_variant(bool condition)
	{
		if (condition) return std::true_type{};
		else return std::false_type{};
	}


	//*****************************define universal hash function************************"""
	template<typename T>
	static inline void hash_combine(size_t& seed, const T& val)
	{
		seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}


	template<typename T>
	static inline void hash_val(size_t& seed, const T& val)
	{
		hash_combine(seed, val);
	}

	template<typename T, typename... Types>
	static inline void hash_val(size_t& seed, const T& val, const Types&... args)
	{
		hash_combine(seed, val);
		hash_val(seed, args...);
	}

	template<typename... Types>
	static inline size_t hash_val(const Types&... args)
	{
		size_t seed = 0;
		hash_val(seed, args...);
		return seed;
	}

	template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
	// explicit deduction guide (not needed as of C++20)
	template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;


	struct Lambda {
		template<typename Tret, typename T>
		static Tret lambda_ptr_exec(void* data) {
			return (Tret)(*(T*)fn<T>())(data);
		}

		template<typename Tret = void, typename Tfp = Tret(*)(void*), typename T>
		static Tfp ptr(T& t) {
			fn<T>(&t);
			return (Tfp)lambda_ptr_exec<Tret, T>;
		}

		template<typename T>
		static void* fn(void* new_fn = nullptr) {
			static void* fn;
			if (new_fn != nullptr)
				fn = new_fn;
			return fn;
		}
	};

}

