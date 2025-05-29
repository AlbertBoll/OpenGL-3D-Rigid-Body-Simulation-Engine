#include "gepch.h"
#include "Assets/Shaders/Shader.h"

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



	Shader::Shader(): m_ProgramHandle{0}, m_Linked{false}
	{

	}


	Shader::~Shader()
	{
		Destroy();
	}

	Shader::Shader(Shader&& other) noexcept
	{
		m_Linked = other.m_Linked;
		m_ProgramHandle = other.m_ProgramHandle;

		other.m_Linked = false;
		other.m_ProgramHandle = 0; //invalid handle
		m_UniformLocations = std::move(other.m_UniformLocations);
	}


	Shader& Shader::operator=(Shader&& other) noexcept
	{
		if (this != &other)
		{
			Destroy();
			m_Linked = other.m_Linked;
			m_ProgramHandle = other.m_ProgramHandle;

			other.m_Linked = false;
			other.m_ProgramHandle = 0; //invalid handle

			m_UniformLocations = std::move(other.m_UniformLocations);
		}
		return *this;
	}


	void Shader::CompileShader(const char* fileName)
	{
		// Check the file name's extension to determine the shader type
		const std::string ext = GetExtension(fileName);
		ShaderType type = VERTEX; //default is vertex shader

		const auto it = extensions.find(ext);

		ASSERT(it != extensions.end(), "Unrecognized extension: " + ext);

		type = it->second;

		// Pass the discovered shader type along
		CompileShader(fileName, type);
	}

	void Shader::CompileShader(const char* fileName, ShaderType type)
	{
		//Assert filename exists
		ASSERT(FileExists(fileName), std::string("Shader: ") + fileName + " not found.")


			if (m_ProgramHandle <= 0)
			{
				m_ProgramHandle = glCreateProgram();

				//Assert shader program was created successfully
				ASSERT(m_ProgramHandle, "Unable to create shader program.")
			}

		std::ifstream inFile(fileName, std::ios::in);

		ASSERT(inFile, std::string("Unable to open: ") + fileName)

		// Get file contents
		std::stringstream code;
		code << inFile.rdbuf();
		inFile.close();

		CompileShader(code.str(), type, fileName);
	}


	void Shader::CompileShader(const std::string& source, ShaderType type, const char* fileName)
	{
		if (m_ProgramHandle <= 0) {
			m_ProgramHandle = glCreateProgram();

			//Assert shader program was created successfully
			ASSERT(m_ProgramHandle, "Unable to create shader program.")
		}

		const GLuint shaderHandle = glCreateShader(type);

		const char* c_code = source.c_str();
		glShaderSource(shaderHandle, 1, &c_code, nullptr);

		// Compile the shader
		glCompileShader(shaderHandle);

		// Check for errors
		int result;
		glGetShaderiv(shaderHandle, GL_COMPILE_STATUS, &result);

		if (GL_FALSE == result) {
			// Compile failed, get log
			std::string msg;
			if (fileName) {
				msg = std::string(fileName) + ": shader compilation failed\n";
			}
			else {
				msg = "Shader compilation failed.\n";
			}

			int length = 0;
			glGetShaderiv(shaderHandle, GL_INFO_LOG_LENGTH, &length);
			if (length > 0) {
				std::string log(length, ' ');
				int written = 0;
				glGetShaderInfoLog(shaderHandle, length, &written, &log[0]);
				msg += log;
			}

			ASSERT(result != GL_FALSE, msg)

		}

		else {
			// Compile succeeded, attach shader
			glAttachShader(m_ProgramHandle, shaderHandle);
		}
	}

	void Shader::Link()
	{
		if (m_Linked) return;
		ASSERT(m_ProgramHandle > 0, "Program has not been compiled.")

		glLinkProgram(m_ProgramHandle);

		int status = 0;
		std::string errString;
		glGetProgramiv(m_ProgramHandle, GL_LINK_STATUS, &status);
		if (GL_FALSE == status) {
			// Store log and return false
			int length = 0;
			glGetProgramiv(m_ProgramHandle, GL_INFO_LOG_LENGTH, &length);
			errString += "Program link failed:\n";
			if (length > 0) {
				std::string log(length, ' ');
				int written = 0;
				glGetProgramInfoLog(m_ProgramHandle, length, &written, &log[0]);
				errString += log;
			}
		}
		else {
			FindUniformLocations();
			m_Linked = true;
		}

		DetachAndDeleteShaderObjects();

		ASSERT(status == GL_TRUE, errString)
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


	void Shader::Destroy()
	{
		if (m_ProgramHandle == 0) return;
		DetachAndDeleteShaderObjects();

		// Delete the program
		glDeleteProgram(m_ProgramHandle);
		m_ProgramHandle = 0;
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
		m_UniformLocations.clear();

		GLint numUniforms = 0;

		// For OpenGL 4.3 and above, use glGetProgramResource
		glGetProgramInterfaceiv(m_ProgramHandle, GL_UNIFORM, GL_ACTIVE_RESOURCES, &numUniforms);

		GLenum properties[] = { GL_NAME_LENGTH, GL_TYPE, GL_LOCATION, GL_BLOCK_INDEX };

		for (GLint i = 0; i < numUniforms; ++i) {
			GLint results[4];
			glGetProgramResourceiv(m_ProgramHandle, GL_UNIFORM, i, 4, properties, 4, nullptr, results);

			if (results[3] != -1) continue;  // Skip uniforms in blocks
			const GLint nameBufSize = results[0] + 1;
			char* name = new char[nameBufSize];
			glGetProgramResourceName(m_ProgramHandle, GL_UNIFORM, i, nameBufSize, nullptr, name);
			m_UniformLocations[name] = results[2];
			delete[] name;
		}
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


	GLuint Shader::GetUniformLocation(const char* name)
	{
		if (const auto pos = m_UniformLocations.find(name); pos != m_UniformLocations.end())
		{
			return pos->second;
		}

		const GLint loc = glGetUniformLocation(m_ProgramHandle, name);
		m_UniformLocations[name] = loc;
		return loc;

	}


	void Shader::DetachAndDeleteShaderObjects() const
	{
		// Detach and delete the shader objects (if they are not already removed)
		GLint numShaders = 0;
		glGetProgramiv(m_ProgramHandle, GL_ATTACHED_SHADERS, &numShaders);
		std::vector<GLuint> shaderNames(numShaders);
		glGetAttachedShaders(m_ProgramHandle, numShaders, nullptr, shaderNames.data());
		for (GLuint shader : shaderNames) {
			glDetachShader(m_ProgramHandle, shader);
			glDeleteShader(shader);
		}
	}


	bool Shader::FileExists(const std::string& fileName)
	{

		struct stat info {};
		int ret = -1;

		ret = stat(fileName.c_str(), &info);
		return 0 == ret;
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
		glUniformMatrix4fv(GetUniformLocation(name), data.size(), false, (float*)&data[0][0]);
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