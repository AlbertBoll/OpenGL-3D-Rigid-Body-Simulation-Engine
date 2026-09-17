#pragma once
#include <unordered_map>
#include <memory>
#include "Assets/Shaders/Shader.h"

//using namespace GEngine::Asset;
//class GEngine::Asset::Shader;

namespace GEngine { class EngineContext; }

namespace GEngine::Manager
{

	using namespace Asset;

	class ShaderManager
	{
	public:
		struct Files
		{
			//std::tuple<types...> keys_filepath;
			std::vector<std::string> keys_filepath;

			Files(const std::initializer_list<std::string>& filePaths)
			{
				keys_filepath.insert(keys_filepath.end(), filePaths);
			}

			operator std::vector<std::string>()const
			{
				return keys_filepath;
			}

			bool operator == (const Files& other) const
			{
				if (keys_filepath.size() != other.keys_filepath.size())return false;
				const size_t size = keys_filepath.size();

				for (size_t i = 0; i < size; i++)
				{
					if (keys_filepath[i] != other.keys_filepath[i])return false;
				}

				return true;
			}

			operator std::vector<std::string>() {
				return keys_filepath;
			}

		};


		struct FileHash
		{

		public:
			size_t operator()(const Files& c) const
			{
				size_t seed = 0;
				for (auto& ele : c.keys_filepath)
				{
					seed ^= std::hash<std::string>{}(ele) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
					//seed += std::hash<std::string>{}(ele);
				}

				return seed;
			}

		};

		using ShaderHashMap = std::unordered_map<Files, std::unique_ptr<Shader>, FileHash>;


	public:
        ~ShaderManager();
        ShaderManager(const ShaderManager&) = delete;
        ShaderManager& operator=(const ShaderManager&) = delete;
        ShaderManager(ShaderManager&&) = delete;
        ShaderManager& operator=(ShaderManager&&) = delete;
        [[nodiscard]] static std::expected<Shader*, ShaderError> GetShaderProgram(const Files& shader_file);
	
		


	private:
        friend class ::GEngine::EngineContext;
        ShaderManager() = default;
        static std::expected<ShaderManager*, ShaderError> Current();
		ShaderHashMap m_ShaderMap;
	};

}
