#include "gepch.h"
#include "Assets/Shaders/Shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <stdexcept>

namespace GEngine::Asset
{

	std::map<std::string, ShaderType> extensions = {
	{".vs",   VERTEX},
	{".vert", VERTEX},
	{"_vert.glsl", VERTEX},
	{".vert.glsl", VERTEX },
	{".gs",   GEOMETRY},
	{".geom", GEOMETRY},
	{ ".geom.glsl", GEOMETRY },
	{".tcs",  TESS_CONTROL},
	{ ".tcs.glsl",  TESS_CONTROL },
	{ ".tes",  TESS_EVALUATION },
	{".tes.glsl",  TESS_EVALUATION},
	{".fs",   FRAGMENT},
	{".frag", FRAGMENT},
	{"_frag.glsl", FRAGMENT},
	{".frag.glsl", FRAGMENT},
	{".cs",   COMPUTE},
	{ ".cs.glsl",   COMPUTE }
	};



    namespace
    {
        [[noreturn]] void CreationFailure(ShaderCreationCode code, std::optional<ShaderType> type,
            std::string_view source, std::string log)
        {
            throw ShaderCreationException({code, type, std::string(source), std::move(log)});
        }

        struct ShaderObject
        {
            GLuint name;
            explicit ShaderObject(GLuint value) : name(value) {}
            ShaderObject(const ShaderObject&) = delete;
            ShaderObject& operator=(const ShaderObject&) = delete;
            ~ShaderObject() { if (name) glDeleteShader(name); }
        };

        std::string CreationLog(GLuint name, bool program)
        {
            GLint length = 0;
            if (program) glGetProgramiv(name, GL_INFO_LOG_LENGTH, &length);
            else glGetShaderiv(name, GL_INFO_LOG_LENGTH, &length);
            if (length <= 1) return {};
            std::string log(static_cast<std::size_t>(length), '\0');
            GLsizei written = 0;
            if (program) glGetProgramInfoLog(name, length, &written, log.data());
            else glGetShaderInfoLog(name, length, &written, log.data());
            log.resize(static_cast<std::size_t>(written));
            return log;
        }
    }

    Shader::Shader() noexcept = default;
    Shader::~Shader() { Destroy(); }

    Shader::Shader(Shader&& other) noexcept
        : m_ProgramHandle(std::exchange(other.m_ProgramHandle, 0)),
          m_Linked(std::exchange(other.m_Linked, false)),
          m_UniformLocations(std::move(other.m_UniformLocations)),
          m_ShaderObjects(std::move(other.m_ShaderObjects))
    {}

    Shader& Shader::operator=(Shader&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_ProgramHandle = std::exchange(other.m_ProgramHandle, 0);
            m_Linked = std::exchange(other.m_Linked, false);
            m_UniformLocations = std::move(other.m_UniformLocations);
            m_ShaderObjects = std::move(other.m_ShaderObjects);
        }
        return *this;
    }

    void Shader::CompileShader(const char* fileName)
    {
        try
        {
            if (!fileName || !*fileName)
                CreationFailure(ShaderCreationCode::InvalidInput, {}, {}, "Shader filename is empty");
            const auto it = extensions.find(GetExtension(fileName));
            if (it == extensions.end())
                CreationFailure(ShaderCreationCode::InvalidInput, {}, fileName, "Unrecognized shader extension");
            CompileShader(fileName, it->second);
        }
        catch (...) { if (!m_Linked) Destroy(); throw; }
    }

    void Shader::CompileShader(const char* fileName, ShaderType type)
    {
        try
        {
            if (!fileName || !*fileName)
                CreationFailure(ShaderCreationCode::InvalidInput, type, {}, "Shader filename is empty");
            if (m_Linked)
                CreationFailure(ShaderCreationCode::InvalidState, type, fileName, "Build a new Shader to replace a linked program");
            std::ifstream input(fileName, std::ios::in | std::ios::binary);
            if (!input)
                CreationFailure(ShaderCreationCode::FileRead, type, fileName, "Unable to open shader source");
            std::stringstream code;
            code << input.rdbuf();
            if (input.bad() || code.bad())
                CreationFailure(ShaderCreationCode::FileRead, type, fileName, "Unable to read shader source");
            CompileShader(code.str(), type, fileName);
        }
        catch (...) { if (!m_Linked) Destroy(); throw; }
    }

    void Shader::CompileShader(const std::string& source, ShaderType type, const char* fileName)
    {
        try
        {
            const std::string_view label = fileName ? fileName : "";
            if (m_Linked)
                CreationFailure(ShaderCreationCode::InvalidState, type, label, "Build a new Shader to replace a linked program");
            if (type != VERTEX && type != FRAGMENT && type != GEOMETRY
                && type != TESS_CONTROL && type != TESS_EVALUATION && type != COMPUTE)
                CreationFailure(ShaderCreationCode::InvalidInput, type, label, "Unsupported shader stage");
            if (source.empty() || source.size() > static_cast<std::size_t>((std::numeric_limits<GLint>::max)()))
                CreationFailure(ShaderCreationCode::InvalidInput, type, label, "Shader source is empty or exceeds the GL length limit");
            if (!m_ProgramHandle)
            {
                m_ProgramHandle = glCreateProgram();
                if (!m_ProgramHandle)
                    CreationFailure(ShaderCreationCode::ProgramAllocation, type, label, "Unable to create shader program");
            }
            ShaderObject shader(glCreateShader(type));
            if (!shader.name)
                CreationFailure(ShaderCreationCode::ShaderAllocation, type, label, "Unable to create shader object");
            const char* text = source.data();
            const auto length = static_cast<GLint>(source.size());
            glShaderSource(shader.name, 1, &text, &length);
            glCompileShader(shader.name);
            GLint status = GL_FALSE;
            glGetShaderiv(shader.name, GL_COMPILE_STATUS, &status);
            if (status != GL_TRUE)
                CreationFailure(ShaderCreationCode::Compile, type, label, "Shader compilation failed:\n" + CreationLog(shader.name, false));
            // Allocation can throw here; the local owner still owns the shader.
            if (!m_ShaderObjects)
                m_ShaderObjects = std::make_unique<std::vector<unsigned int>>(0);
            m_ShaderObjects->push_back(shader.name);
            const auto name = std::exchange(shader.name, 0);
            glAttachShader(m_ProgramHandle, name);
        }
        catch (...) { if (!m_Linked) Destroy(); throw; }
    }

    void Shader::Link()
    {
        if (m_Linked) return;
        try
        {
            if (!m_ProgramHandle || !m_ShaderObjects || m_ShaderObjects->empty())
                CreationFailure(ShaderCreationCode::InvalidState, {}, {}, "Program has no compiled shader stages");
            glLinkProgram(m_ProgramHandle);
            GLint status = GL_FALSE;
            glGetProgramiv(m_ProgramHandle, GL_LINK_STATUS, &status);
            if (status != GL_TRUE)
                CreationFailure(ShaderCreationCode::Link, {}, {}, "Program link failed:\n" + CreationLog(m_ProgramHandle, true));
            DetachAndDeleteShaderObjects();
            FindUniformLocations();
            m_Linked = true;
        }
        catch (...) { Destroy(); throw; }
    }

    ShaderCreationResult CreateShaderProgram(std::span<const ShaderSource> sources)
    {
        Shader candidate;
        try
        {
            if (sources.empty())
                CreationFailure(ShaderCreationCode::InvalidInput, {}, {}, "Shader program requires source stages");
            for (const auto& source : sources)
            {
                const std::string label(source.label);
                candidate.CompileShader(std::string(source.source), source.type, label.c_str());
            }
            candidate.Link();
            return candidate;
        }
        catch (const ShaderCreationException& failure) { return failure.Error(); }
    }

    ShaderCreationResult CreateShaderProgramFromFiles(std::span<const std::string> files)
    {
        Shader candidate;
        try
        {
            if (files.empty())
                CreationFailure(ShaderCreationCode::InvalidInput, {}, {}, "Shader program requires source files");
            for (const auto& file : files) candidate.CompileShader(file.c_str());
            candidate.Link();
            return candidate;
        }
        catch (const ShaderCreationException& failure) { return failure.Error(); }
    }

	void Shader::Validate() const
	{
		ASSERT(IsLinked(), "Program is not linked")

		GLint status;
		glValidateProgram(m_ProgramHandle);
		glGetProgramiv(m_ProgramHandle, GL_VALIDATE_STATUS, &status);

		if (GL_FALSE == status) {
			// Store log and return false
			int length = 0;
			std::string logString;

			glGetProgramiv(m_ProgramHandle, GL_INFO_LOG_LENGTH, &length);

			if (length > 0) {
				char* c_log = new char[length];
				int written = 0;
				glGetProgramInfoLog(m_ProgramHandle, length, &written, c_log);
				logString = c_log;
				delete[] c_log;
			}

			ASSERT(status == GL_TRUE, std::string("Program failed to validate\n") + logString)

		}
	}

	void Shader::Bind() const
	{
		ASSERT(m_ProgramHandle > 0 && m_Linked, "Shader program is invalid or has not been linked")

		glUseProgram(m_ProgramHandle);
	}

	void Shader::UnBind() const
	{
		glUseProgram(0);
	}

	int Shader::GetHandle() const
	{
		return m_ProgramHandle;
	}

	bool Shader::IsLinked() const
	{
		return m_Linked;
	}


    void Shader::Destroy() noexcept
    {
        DetachAndDeleteShaderObjects();
        if (m_ProgramHandle) glDeleteProgram(std::exchange(m_ProgramHandle, 0));
        m_Linked = false;
        m_UniformLocations.reset();
    }

	void Shader::BindAttribLocation(unsigned int location, const char* name) const
	{
		glBindAttribLocation(m_ProgramHandle, location, name);
	}


	void Shader::BindFragDataLocation(unsigned int location, const char* name) const
	{
		glBindFragDataLocation(m_ProgramHandle, location, name);
	}


	const char* Shader::GetTypeString(unsigned int type)
	{
		// There are many more types than are covered here, but
		// these are the most common in these examples.
		switch (type) {
		case GL_FLOAT:
			return "float";
		case GL_FLOAT_VEC2:
			return "vec2";
		case GL_FLOAT_VEC3:
			return "vec3";
		case GL_FLOAT_VEC4:
			return "vec4";
		case GL_DOUBLE:
			return "double";
		case GL_INT:
			return "int";
		case GL_UNSIGNED_INT:
			return "unsigned int";
		case GL_BOOL:
			return "bool";
		case GL_FLOAT_MAT2:
			return "mat2";
		case GL_FLOAT_MAT3:
			return "mat3";
		case GL_FLOAT_MAT4:
			return "mat4";
		default:
			return "?";
		}
	}


    void Shader::FindUniformLocations()
    {
        auto locations = std::make_unique<UniformLocations>();
        GLint count = 0;
        glGetProgramInterfaceiv(m_ProgramHandle, GL_UNIFORM, GL_ACTIVE_RESOURCES, &count);
        const GLenum properties[] = { GL_NAME_LENGTH, GL_LOCATION, GL_BLOCK_INDEX };
        for (GLint i = 0; i < count; ++i)
        {
            GLint values[3]{};
            glGetProgramResourceiv(m_ProgramHandle, GL_UNIFORM, i, 3, properties, 3, nullptr, values);
            if (values[2] != -1) continue;
            std::string name(static_cast<std::size_t>(values[0]), '\0');
            GLsizei written = 0;
            glGetProgramResourceName(m_ProgramHandle, GL_UNIFORM, i, values[0], &written, name.data());
            name.resize(static_cast<std::size_t>(written));
            locations->emplace(std::move(name), values[1]);
        }
        m_UniformLocations = std::move(locations);
    }

	void Shader::PrintActiveUniforms() const
	{
		// For OpenGL 4.3 and above, use glGetProgramResource
		GLint numUniforms = 0;
		glGetProgramInterfaceiv(m_ProgramHandle, GL_UNIFORM, GL_ACTIVE_RESOURCES, &numUniforms);
	
		GLenum properties[] = { GL_NAME_LENGTH, GL_TYPE, GL_LOCATION, GL_BLOCK_INDEX };

		printf("Active uniforms:\n");
		for (int i = 0; i < numUniforms; ++i) {
			GLint results[4];
			glGetProgramResourceiv(m_ProgramHandle, GL_UNIFORM, i, 4, properties, 4, nullptr, results);

			if (results[3] != -1) continue;  // Skip uniforms in blocks
			const GLint nameBufSize = results[0] + 1;
			char* name = new char[nameBufSize];
			glGetProgramResourceName(m_ProgramHandle, GL_UNIFORM, i, nameBufSize, nullptr, name);
			printf("%-5d %s (%s)\n", results[2], name, GetTypeString(results[1]));
			delete[] name;
		}
	}


	void Shader::PrintActiveUniformBlocks() const
	{
		GLint numBlocks = 0;

		glGetProgramInterfaceiv(m_ProgramHandle, GL_UNIFORM_BLOCK, GL_ACTIVE_RESOURCES, &numBlocks);
		GLenum blockProps[] = { GL_NUM_ACTIVE_VARIABLES, GL_NAME_LENGTH };
		GLenum blockIndex[] = { GL_ACTIVE_VARIABLES };
		GLenum props[] = { GL_NAME_LENGTH, GL_TYPE, GL_BLOCK_INDEX };

		for (int block = 0; block < numBlocks; ++block) {
			GLint blockInfo[2];
			glGetProgramResourceiv(m_ProgramHandle, GL_UNIFORM_BLOCK, block, 2, blockProps, 2, nullptr, blockInfo);
			const GLint numUnis = blockInfo[0];

			char* blockName = new char[blockInfo[1] + 1];
			glGetProgramResourceName(m_ProgramHandle, GL_UNIFORM_BLOCK, block, blockInfo[1] + 1, nullptr, blockName);
			printf("Uniform block \"%s\":\n", blockName);
			delete[] blockName;

			const auto unifIndexes = new GLint[numUnis];
			glGetProgramResourceiv(m_ProgramHandle, GL_UNIFORM_BLOCK, block, 1, blockIndex, numUnis, nullptr, unifIndexes);

			for (int unif = 0; unif < numUnis; ++unif) {
				const GLint uniIndex = unifIndexes[unif];
				GLint results[3];
				glGetProgramResourceiv(m_ProgramHandle, GL_UNIFORM, uniIndex, 3, props, 3, nullptr, results);

				const GLint nameBufSize = results[0] + 1;
				char* name = new char[nameBufSize];
				glGetProgramResourceName(m_ProgramHandle, GL_UNIFORM, uniIndex, nameBufSize, nullptr, name);
				printf("    %s (%s)\n", name, GetTypeString(results[1]));
				delete[] name;
			}

			delete[] unifIndexes;
		}
	}


	void Shader::PrintActiveAttribs() const
	{
		// >= OpenGL 4.3, use glGetProgramResource
		GLint numAttribs;
		glGetProgramInterfaceiv(m_ProgramHandle, GL_PROGRAM_INPUT, GL_ACTIVE_RESOURCES, &numAttribs);

		GLenum properties[] = { GL_NAME_LENGTH, GL_TYPE, GL_LOCATION };

		printf("Active attributes:\n");
		for (int i = 0; i < numAttribs; ++i) {
			GLint results[3];
			glGetProgramResourceiv(m_ProgramHandle, GL_PROGRAM_INPUT, i, 3, properties, 3, NULL, results);

			const GLint nameBufSize = results[0] + 1;
			char* name = new char[nameBufSize];
			glGetProgramResourceName(m_ProgramHandle, GL_PROGRAM_INPUT, i, nameBufSize, NULL, name);
			printf("%-5d %s (%s)\n", results[2], name, GetTypeString(results[1]));
			delete[] name;
		}
	}


	void Shader::BindTextureUniform(unsigned int TexID, unsigned int TexUnit, unsigned int TexTarget)
	{
		glActiveTexture(GL_TEXTURE0 + TexUnit);
		glBindTexture(TexTarget, TexID);
	}

	void Shader::SetUniform(const char* name, bool data)
	{
		const GLint loc = GetUniformLocation(name);
		glUniform1i(loc, data);
	}


    int Shader::GetUniformLocation(const char* name)
    {
        if (!m_Linked || !name) return -1;
        auto& locations = GetUniformLocations();
        if (const auto found = locations.find(name); found != locations.end()) return found->second;
        const GLint location = glGetUniformLocation(m_ProgramHandle, name);
        locations.emplace(name, location); // Cache missing (-1) locations too.
        return location;
    }

    void Shader::DetachAndDeleteShaderObjects() noexcept
    {
        if (!m_ShaderObjects) return;
        for (const auto shader : *m_ShaderObjects)
        {
            glDetachShader(m_ProgramHandle, shader);
            glDeleteShader(shader);
        }
        m_ShaderObjects.reset();
    }

	std::string Shader::GetExtension(const char* name)
	{
		const std::string nameStr(name);

		if (const size_t dotLoc = nameStr.find_last_of('.'); dotLoc != std::string::npos) {
			if (std::string ext = nameStr.substr(dotLoc); ext == ".glsl") {

				size_t loc = nameStr.find_last_of('.', dotLoc - 1);
				if (loc == std::string::npos) {
					loc = nameStr.find_last_of('_', dotLoc - 1);
				}
				if (loc != std::string::npos) {
					return nameStr.substr(loc);
				}
			}
			else {
				return ext;
			}
		}
		return "";
	}
	
	//template specialization of SetUniform for std::pair
	template<>
	void Shader::SetUniform<std::pair<GLuint, GLuint>>(const char* name, const std::pair<GLuint, GLuint>& textureBinding)
	{
		auto& [textureID, textureUnit] = textureBinding;
		glActiveTexture(GL_TEXTURE0 + textureUnit);
		glBindTexture(GL_TEXTURE_2D, textureID);
		const GLint loc = GetUniformLocation(name);
		glUniform1i(loc, textureUnit);
	}


	//template specialization of SetUniform for std::pair(std::pair)
	template<>
	void Shader::SetUniform<std::pair<GLuint, std::pair<GLuint, GLuint>>>(const char* name, const std::pair<GLuint, std::pair<GLuint, GLuint>>& textureBinding)
	{
		auto& [textureID, textureUnit] = textureBinding.second;
		glActiveTexture(GL_TEXTURE0 + textureUnit);
		glBindTexture(textureBinding.first, textureID);
		const GLint loc = GetUniformLocation(name);
		glUniform1i(loc, textureUnit);
	}

	template<>
	void Shader::SetUniform<Math::Mat4>(const char* name, const std::vector<Math::Mat4>& data)
	{
		if (data.size() > static_cast<size_t>(std::numeric_limits<GLsizei>::max())) {
			throw std::length_error("Matrix uniform count exceeds GLsizei");
		}
		glUniformMatrix4fv(GetUniformLocation(name), static_cast<GLsizei>(data.size()), false,
			data.empty() ? nullptr : glm::value_ptr(data.front()));
	}
	
	using namespace Math;

	//SET_UNIFORM_IMPL(glUniform1iv, bool, int);
	SET_UNIFORM_IMPL(glUniform1iv, Vec1i, int);
	SET_UNIFORM_IMPL(glUniform2iv, Vec2i, int);
	SET_UNIFORM_IMPL(glUniform3iv, Vec3i, int);
	SET_UNIFORM_IMPL(glUniform4iv, Vec4i, int);

	SET_UNIFORM_IMPL(glUniform1fv, Vec1f, float);
	SET_UNIFORM_IMPL(glUniform2fv, Vec2f, float);
	SET_UNIFORM_IMPL(glUniform3fv, Vec3f, float);
	SET_UNIFORM_IMPL(glUniform4fv, Vec4f, float);

	SET_UNIFORM_IMPL(glUniform4fv, Quat, float);

	SET_MATRIX_IMPL(glUniformMatrix2fv, Mat2, float);
	SET_MATRIX_IMPL(glUniformMatrix3fv, Mat3, float);
	SET_MATRIX_IMPL(glUniformMatrix4fv, Mat4, float);

}
