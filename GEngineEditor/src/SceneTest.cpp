#include "SceneTest.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShapeManager.h"
#include "Managers/ShaderManager.h"

SceneTest::~SceneTest()
{
	Manager::AssetsManager::FreeAllResources();
	Manager::ShaderManager::FreeShader();
	Manager::ShapeManager::FreeShape();
}

//
//BaseApp* CreateApp()
//{
//	return new SceneTest();
//}