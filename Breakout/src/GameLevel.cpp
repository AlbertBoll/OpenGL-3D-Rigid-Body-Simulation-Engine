#include "GameLevel.h"
#include "Core/Renderer2D.h"
#include "Camera/Camera.h"
#include <fstream>
#include <sstream>
#include <Material/SpriteMaterial.h>
#include "Managers/ShapeManager.h"
#include "Managers/AssetsManager.h"



static std::string ImagePath = "../Breakout/include/images/";
static std::string ImageExtension = ".png";

void GameLevel::Load(const std::string& file, unsigned int levelWidth, unsigned int levelHeight)
{
    // clear old data
    m_Bricks.clear();
    // load from file
    unsigned int tileCode;
    GameLevel level;
    std::string line;
    std::ifstream fstream(file);
    std::vector<std::vector<unsigned int>> tileData;
    if (fstream)
    {
        while (std::getline(fstream, line)) // read each line from level file
        {
            std::istringstream sstream(line);
            std::vector<unsigned int> row;
            while (sstream >> tileCode) // read each word separated by spaces
                row.push_back(tileCode);
            tileData.push_back(row);
        }
        if (tileData.size() > 0)
            Initialize(tileData, levelWidth, levelHeight);
    }
}

void GameLevel::Render(CameraBase* camera)
{
	for (auto& brick : m_Bricks)
	{
		if (!brick->GetIsDestroy())
		{
			Renderer2D::Render(brick.get(), camera);
		}
	}
}

bool GameLevel::IsCompleted()
{
	for (auto& tile : m_Bricks)
	{
		if (!tile->GetIsDestroy() && !tile->GetIsSolid())
			return false;
	}

	return true;
}

void GameLevel::Initialize(std::vector<std::vector<unsigned int>> tileData, unsigned int levelWidth, unsigned int levelHeight)
{
    using namespace Manager;

    Component::SpriteComponent sprite;
    sprite.SpriteColor.Name = "u_spriteColor";
   

    auto blockTex = AssetsManager::GetTexture(ImagePath + "block" + ImageExtension);
    auto solidBlockTex = AssetsManager::GetTexture(ImagePath + "block_solid" + ImageExtension);
    auto blockMaterial = CreateRefPtr<SpriteMaterial>(blockTex);
    auto blockSolidMaterial = CreateRefPtr<SpriteMaterial>(solidBlockTex);

    auto blockGeo = ShapeManager::GetShape("SpriteGeometry");

    Vec3f color(1.0f); // original: white
    ScopedPtr<SpriteEntity> obj = nullptr;
    // calculate dimensions
    auto height = tileData.size();
    auto width = tileData[0].size(); // note we can index vector at [0] since this function is only called if height > 0
    float unit_width = levelWidth / static_cast<float>(width), unit_height = levelHeight / static_cast<float>(height);
    // initialize level tiles based on tileData		
    for (unsigned int y = 0; y < height; ++y)
    {
        for (unsigned int x = 0; x < width; ++x)
        {
            // check block type from level data (2D level array)
            if (tileData[y][x] == 1) // solid
            {
                Vec2f pos(unit_width * x, unit_height * y);
                Vec2f size(unit_width, unit_height);
                sprite.SpriteColor.Data = { 0.8f, 0.8f, 0.7f};
        
              
                obj = CreateScopedPtr<SpriteEntity>(blockGeo, blockSolidMaterial);
                sprite.Size = { size.x, size.y };
                obj->SetScale({ size.x, size.y, 0 }, false);
                obj->Translate(pos.x, pos.y, 0.f, false);
                obj->SetIsSolid(true);
                //m_Bricks.push_back(std::move(obj));
            }
            else if (tileData[y][x] > 1)	// non-solid; now determine its color based on level data
            {
                
                if (tileData[y][x] == 2)
                    color = { 0.2f, 0.6f, 1.0f };
                else if (tileData[y][x] == 3)
                    color = { 0.0f, 0.7f, 0.0f };
                else if (tileData[y][x] == 4)
                    color = { 0.8f, 0.8f, 0.4f };
                else if (tileData[y][x] == 5)
                    color = { 1.0f, 0.5f, 0.0f };

                sprite.SpriteColor.Data = color;
               
                Vec2f pos(unit_width * x, unit_height * y);
                Vec2f size(unit_width, unit_height);
              
                obj = CreateScopedPtr<SpriteEntity>(blockGeo, blockMaterial);
                sprite.Size = { size.x, size.y };
                obj->SetScale({ size.x, size.y, 0 }, false);
                obj->Translate(pos.x, pos.y, 0.f, false);
                //m_Bricks.push_back(std::move(obj));
            }
            if (obj)
            {
                obj->SetSpriteComponent(sprite);
                m_Bricks.push_back(std::move(obj));
            }


        }
    }
}
