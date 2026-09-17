#pragma once
#include <vector>
#include "Core/RenderTarget.h"
#include <Sprite/SpriteEntity.h>

namespace GEngine
{
    class CameraBase;
}


using namespace GEngine;
class GameLevel
{
 
    
public:
  
    // constructor
    GameLevel() = default;

    GameLevel(GameLevel&& other) = default;

    // loads level from file
    [[nodiscard]] ApplicationInitializationResult Load(const std::string& file, unsigned int levelWidth, unsigned int levelHeight);
    // render level
    void Render(CameraBase* camera);
    // check if the level is completed (all non-solid tiles are destroyed)
    bool IsCompleted();

private:
    [[nodiscard]] ApplicationInitializationResult Initialize(std::vector<std::vector<unsigned int>> tileData, unsigned int levelWidth, unsigned int levelHeight);

public:
	std::vector<ScopedPtr<SpriteEntity>> m_Bricks;

};

