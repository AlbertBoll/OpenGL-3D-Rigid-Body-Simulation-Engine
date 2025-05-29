#pragma once
#include <string>

struct aiMesh;
struct aiScene;
struct aiNode;

namespace GEngine
{
	class Geometry;
	class Entity;

	class RawModel
	{

	public:
		RawModel(const std::string& path);
		void LoadModelGeometry(const std::string& path);
		Geometry* GetGeometry(int index);
	private:

		Geometry* ProcessMesh(aiMesh* mesh, const aiScene* scene);
		void ProcessNode(aiNode* node, const aiScene* scene);
		
	

	private:
		std::vector<Geometry*> m_Geometries;
		//std::string m_Directory;
	};

}