#pragma once

#include "Core\BaseApp.h"
#include <GameLevel.h>
#include "Audio/SoundEvent.h"

class BallEntity;

namespace GEngine
{
	class SpriteEntity;
	class AudioSystem;

	

	class BreakoutApp : public BaseApp
	{

	public:
		enum class GameState
		{
			ACTIVE,
			MENU,
			WIN
		};


		enum class CollisionDirection
		{
			UP,
			RIGHT,
			DOWN,
			LEFT,
			NONE
		};

		struct AABBCircleCollisionInfo
		{
			bool IsCollide{};
			CollisionDirection Direction = CollisionDirection::NONE;
			Vec2f DifferenceDirection{};
			
		};



	public:
		BreakoutApp() : BaseApp(), m_GameState(GameState::ACTIVE) {}
		~BreakoutApp() override;

		void Update(Timestep ts) override;

		void Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)override;
		void Initialize(const WindowProperties& prop = WindowProperties{}) override;

		void Render()override;

		void ProcessInput(Timestep ts) override;

		void DoCollisions();

	private:
		AABBCircleCollisionInfo CheckCollisions(BallEntity* ball, SpriteEntity* sprite);
		void ResetLevel();
		void ResetPlayer();
		CollisionDirection VectorDirection(const Vec2f& target);


	private:
		std::unordered_map<unsigned int, std::vector<std::vector<SpriteEntity*>>> m_GroupsLookUp;
		std::vector<GameLevel> m_Levels;
		SpriteEntity* m_Background{};
		SpriteEntity* m_Player{};
		BallEntity* m_Ball{};
		Audio::AudioSystem* m_AudioSystem{};

		Audio::SoundEvent m_MusicEvent;
		Audio::SoundEvent m_ReverbSnap;
		GameState m_GameState;
		uint32_t m_Level{0};
		uint32_t m_Width;
		uint32_t m_Height;

	};

}