#pragma once
//#define SDL_MAIN_HANDLED
#include "Core/BaseApp.h"
//#include"Physics/ShapeSphere.h"
#include<iostream>

using namespace GEngine;

//To be implemented in client app
BaseApp* CreateApp();

//define in client app
extern WindowProperties winProp;


int main(int argc, char* args[])
{

	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	auto app = CreateApp();
	
	app->Initialize({ winProp });

	app->Run();


	delete app;
	
	//using namespace GEngine;

	//PhysicalShape* sphere = new ShapeSphere(0.5f);
	//PhysicalShape* sphere_ = new ShapeSphere(0.4f);
	

	//std::cout << typeid(sphere).hash_code() << std::endl;

	//std::cout << typeid(sphere_).hash_code() << std::endl;




















	//SceneApp app{};
	//auto f = [](float u, float v) {return Vec3f{ u, v, 1.f }; };
	//Surface<decltype(f)> s(f);
	//std::vector<float> vertexArcLengthData{ 0, 1 };
	//vertexArcLengthData.reserve(10);

	////std::cout << g.CountVertices() << std::endl;


	//int i = 0;


	//BaseApp app;


	//WindowProperties winProp1{ .m_Title = "GEngine Editor App1",
	//									.m_WinPos = WindowPos::Center,
	//									.m_XPaddingToCenterY = 0,
	//									.m_YPaddingToCenterX = 0,
	//};

	/*WindowProperties winProp2{ .m_Title = "GEngine Editor App2",
										.m_WinPos = WindowPos::TopRight,
										.m_XPaddingToCenterY = 5,
										.m_YPaddingToCenterX = 20,
	};*/

	//app.Initialize(winProp1);




	//auto& engine = app.GetEngine();
	//engine.GetWindowManager()->AddWindows(winProp1, winProp2);
	////Factory::ShapeFactory::RegisterShape("Box", new Shape::Box{});
	//RegisterClass(Box, new Shape::Box);
	//auto box = Factory::ShapeFactory::GetShape("Box");
	//Shape::Box box;
	//Geometry g;
	//Geometry g_;
	//g.m_Attributes[0] = Buffer::Attribute<Vec3f>({ {1.f, 2.f, 3.f}, {2.f, 3.f, 4.f}, {3.f, 4.f, 5.f} });
	//g.CountVertices();

	//g_.m_Attributes[0] = Buffer::Attribute<Vec3f>({ {1.f, 2.f, 3.f}, {2.f, 3.f, 4.f}, {3.f, 4.f, 5.f} });
	//g_.CountVertices();
	//g.Merge(&g_);
	//app.Run();

	//std::cout << "Finish" << std::endl;

	return 0;

}
