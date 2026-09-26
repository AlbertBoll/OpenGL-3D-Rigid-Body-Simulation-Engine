#include "Renderer/FrameScheduler.h"
#include "BreakoutApp.h"
#include "Core/RuntimeAssets.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShapeManager.h"
#include "Managers/ShaderManager.h"
#include "Camera/OrthographicCamera.h"
#include "EntryPoint.h"
#include <Material/SpriteMaterial.h>
#include <Sprite/SpriteEntity.h>
#include "GameObject/BallObject.h"
#include"Core/Renderer2D.h"
#include <Core/Log.h>
#include "Core/Window.h"
#include "Core/Renderer.h"
#include "Core/Scene.h"
#include "Audio/AudioSystem.h"
//using namespace GEngine::BreakoutApp;

static constexpr ::GEngine::RuntimeAssets::Directory levelPath{ "Breakout/levels/" };
static std::string levelFileExtension = ".lvl";
static constexpr ::GEngine::RuntimeAssets::Directory ImagePath{ "Breakout/images/" };
static std::string ImageExtension = ".png";


// Initial size of the player paddle
static const Vec2f PLAYER_SIZE(100.0f, 20.0f);
// Initial velocity of the player paddle
static const float PLAYER_VELOCITY(500.0f);

// Initial velocity of the Ball
static const Vec2f INITIAL_BALL_VELOCITY(100.0f, -350.0f);
// Radius of the ball object
static const float BALL_RADIUS = 12.5f;

namespace GEngine
{
	typedef BreakoutApp::CollisionDirection CollisionDir;
	typedef BreakoutApp::AABBCircleCollisionInfo CollisionInfo;

	CollisionDir BreakoutApp::VectorDirection(const Vec2f& target)
	{
		Vec2f compass[] = {
	    Vec2f(0.0f, 1.0f),	// up
	    Vec2f(1.0f, 0.0f),	// right
	    Vec2f(0.0f, -1.0f),	// down
	    Vec2f(-1.0f, 0.0f)	// left
		};

		float max = 0.0f;
		unsigned int best_match = -1;
		for (unsigned int i = 0; i < 4; i++)
		{
			float dot_product = glm::dot(glm::normalize(target), compass[i]);
			if (dot_product > max)
			{
				max = dot_product;
				best_match = i;
			}
		}
		return (CollisionDir)best_match;
	}

	BreakoutApp::~BreakoutApp()
	{
		// These objects are app-owned; shared asset caches are released by BaseApp.
		if (m_Background) delete m_Background;
		if (m_Player) delete m_Player;
		delete m_Ball;
		delete m_EditorCamera;


	}

	void BreakoutApp::Update(Timestep ts)
	{
		BaseApp::Update(ts);
		m_Ball->Move(ts, m_Width);
		DoCollisions();

		if (m_Ball->Get2DPosition().y >= m_Height)
		{
			ResetLevel();
			ResetPlayer();

		}

		m_AudioSystem->Update(ts);



		auto backgroundMusic = m_MusicEvent.GetPlayState();
		if (!backgroundMusic)
		{
		    const auto& error = backgroundMusic.error();
		    FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure, "Audio",
		        std::string(Audio::PlaybackStateErrorLabel(error.code)), error.operation,
		        "event=" + std::to_string(error.eventId), error.message});
		    return;
		}
		if (*backgroundMusic == Audio::PLAYBACK_STOPPING)
			m_MusicEvent.Restart();
		
	}

	void BreakoutApp::DoCollisions()
	{
		float penetration{};
		CollisionDir dir;
		CollisionInfo collision;
		Vec2f diff_vector;

		for (auto& brick : m_Levels[m_Level].m_Bricks)
		{
			if (!brick->GetIsDestroy())
			{
				collision = CheckCollisions(m_Ball, brick.get());
				if(collision.IsCollide)
				{
					GENGINE_CORE_INFO("Collision");
					if (!brick->GetIsSolid())
					{
						brick->SetIsDestroy(true);
					}

					dir = collision.Direction;
					diff_vector = collision.DifferenceDirection;
					if (dir == CollisionDirection::LEFT || dir == CollisionDirection::RIGHT)
					{
						m_Ball->GetSpriteComponent().Velocity.x *= -1.0f;
						penetration = m_Ball->m_Radius - std::abs(diff_vector.x);
						if (dir == CollisionDirection::LEFT)
							m_Ball->IncrementPosX(penetration); //move ball to right
							
						else
							m_Ball->IncrementPosX(-penetration); // move ball to left;
					}

					else
					{
						m_Ball->GetSpriteComponent().Velocity.y *= -1.0f;
						penetration = m_Ball->m_Radius - std::abs(diff_vector.y);
						if (dir == CollisionDirection::UP)
							m_Ball->IncrementPosY(-penetration);

						else
							m_Ball->IncrementPosY(penetration);
					}

				}
			}
		}

		CollisionInfo result = CheckCollisions(m_Ball, m_Player);
		if (!m_Ball->m_IsStuck && result.IsCollide)
		{
			// check where it hit the board, and change velocity based on where it hit the board
			//m_Player->Get2DPosition().x
			float centerBoard = m_Player->GetPosition().x + m_Player->GetSpriteComponent().Size.x / 2.0f;
			float distance = (m_Ball->GetPosition().x + m_Ball->m_Radius) - centerBoard;
			float percentage = distance / (m_Player->GetSpriteComponent().Size.x / 2.0f);
			// then move accordingly
			float strength = 2.0f;
			Vec2f oldVelocity = m_Ball->GetSpriteComponent().Velocity;
			m_Ball->GetSpriteComponent().Velocity.x = INITIAL_BALL_VELOCITY.x * percentage * strength;
			//Ball->Velocity.y = -Ball->Velocity.y;
			m_Ball->GetSpriteComponent().Velocity = glm::normalize(m_Ball->GetSpriteComponent().Velocity) * glm::length(oldVelocity); // keep speed consistent over both axes (multiply by length of old velocity, so total strength is not changed)
			// fix sticky paddle
			m_Ball->GetSpriteComponent().Velocity.y = -1.0f * abs(m_Ball->GetSpriteComponent().Velocity.y);
		}


	}



	ApplicationInitializationResult BreakoutApp::Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)
	{
		using namespace Camera;
		using namespace Manager;

		//Initialize BaseApp 
		if (auto initialized = BaseApp::Initialize(WindowsPropertyList); !initialized) return initialized;

		GENGINE_CORE_INFO("Initialize Audio System...");
		m_AudioSystem = new Audio::AudioSystem();
		m_AudioSystem->Initialize();

		m_GameState = GameState::ACTIVE;

		m_Width = WindowsPropertyList.begin()->m_Width;
		m_Height = WindowsPropertyList.begin()->m_Height;

		GENGINE_CORE_INFO("Initialize 2D Renderer...");
		Renderer2D::Initialize({m_Width, m_Height});

		//configurate orthographic camera;
	

		GENGINE_CORE_INFO("Initialize Orthographic Camera...");
		m_EditorCamera = new OrthographicCamera(0.0f, (float)m_Width, (float)m_Height, 0.f);


		Component::SpriteComponent sprite;
		sprite.SpriteColor.Name = "u_spriteColor";
		sprite.SpriteColor.Data = { 1.f, 1.f, 1.f };
		sprite.Velocity = INITIAL_BALL_VELOCITY;
		sprite.Size = { 2 * BALL_RADIUS, 2 * BALL_RADIUS };

		auto backgroundTexResult = AssetsManager::GetTextureOrFallback(ImagePath + "background" + ImageExtension);
		if (!backgroundTexResult) { GENGINE_CORE_ERROR("Texture {}: {}", backgroundTexResult.error().source, backgroundTexResult.error().message); m_Running = false; return std::unexpected(backgroundTexResult.error()); }
		auto* backgroundTex = *backgroundTexResult;
		auto backgroundMaterialResult = Material::Create<SpriteMaterial>(backgroundTex);
		if (!backgroundMaterialResult) return std::unexpected(backgroundMaterialResult.error());
		auto backgroundMaterial = std::move(*backgroundMaterialResult);
		auto spriteGeoResult = ShapeManager::FindShape("SpriteGeometry");
        if (!spriteGeoResult) return std::unexpected(spriteGeoResult.error());
        if (!*spriteGeoResult) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState,
            "BreakoutApp::Initialize", "Required SpriteGeometry shape is unavailable"});
        auto* spriteGeo = *spriteGeoResult;
		m_Background = new SpriteEntity(spriteGeo, backgroundMaterial);
		m_Background->SetSpriteComponent(sprite);
		m_Background->SetScale({ m_Width, m_Height, 0 });

		Vec2f playerPos = glm::vec2(m_Width / 2.0f - PLAYER_SIZE.x / 2.0f, m_Height - PLAYER_SIZE.y);

		


		auto paddleTexResult = AssetsManager::GetTextureOrFallback(ImagePath + "paddle" + ImageExtension);
		if (!paddleTexResult) { GENGINE_CORE_ERROR("Texture {}: {}", paddleTexResult.error().source, paddleTexResult.error().message); m_Running = false; return std::unexpected(paddleTexResult.error()); }
		auto* paddleTex = *paddleTexResult;
		auto paddleMaterialResult = Material::Create<SpriteMaterial>(paddleTex);
		if (!paddleMaterialResult) return std::unexpected(paddleMaterialResult.error());
		auto paddleMaterial = std::move(*paddleMaterialResult);
		m_Player = new SpriteEntity(spriteGeo, paddleMaterial);
		m_Player->SetSpriteComponent(sprite);
		m_Player->Set2DTransform(playerPos, PLAYER_SIZE);


		Vec2f ballPos = playerPos + Vec2f(PLAYER_SIZE.x / 2.0f - BALL_RADIUS,
			-BALL_RADIUS * 2.0f);


		auto ballTexResult = AssetsManager::GetTextureOrFallback(ImagePath + "awesomeface_r" + ImageExtension);
		if (!ballTexResult) { GENGINE_CORE_ERROR("Texture {}: {}", ballTexResult.error().source, ballTexResult.error().message); m_Running = false; return std::unexpected(ballTexResult.error()); }
		auto* ballTex = *ballTexResult;
		auto ballMaterialResult = Material::Create<SpriteMaterial>(ballTex);
		if (!ballMaterialResult) return std::unexpected(ballMaterialResult.error());
		auto ballMaterial = std::move(*ballMaterialResult);
		m_Ball = new BallEntity(spriteGeo, ballMaterial);
		m_Ball->SetParams(ballPos, BALL_RADIUS).SetSpriteComponent(sprite);
		
		
		GameLevel one;   
		if (auto loaded = one.Load(levelPath + "one" + levelFileExtension, m_Width, m_Height / 2); !loaded) return loaded;
		GameLevel two;  
		if (auto loaded = two.Load(levelPath + "two" + levelFileExtension, m_Width, m_Height / 2); !loaded) return loaded;
		GameLevel three; 
		if (auto loaded = three.Load(levelPath + "three" + levelFileExtension, m_Width, m_Height / 2); !loaded) return loaded;
		GameLevel four; 
		if (auto loaded = four.Load(levelPath + "four" + levelFileExtension, m_Width, m_Height / 2); !loaded) return loaded;

		m_Levels.push_back(std::move(one));
		m_Levels.push_back(std::move(two));
		m_Levels.push_back(std::move(three));
		m_Levels.push_back(std::move(four));

		auto size = m_Levels[m_Level].m_Bricks.size();

		std::vector<SpriteEntity*> bricks;
		bricks.reserve(size);

		for (auto& brick : m_Levels[m_Level].m_Bricks)
		{
			bricks.push_back(brick.get());
		}
			
	

		m_GroupsLookUp.Set(*backgroundMaterial, { {m_Background}, {m_Player}, bricks, {m_Ball} });

		m_MusicEvent = m_AudioSystem->PlayEvent("event:/EnteringValley");
		
	    return {};
	}

	ApplicationInitializationResult BreakoutApp::Initialize(const WindowProperties& prop)
	{
	    return Initialize(std::initializer_list<WindowProperties>{prop});
	}

    void BreakoutApp::Render()
    {
        RenderContext context{*GetWindow(),*GetWindowManager()};
        context.legacyScene={this,[](void* user)->ScheduleResult {
            auto& app=*static_cast<BreakoutApp*>(user);
            const auto pixels=app.GetWindow()->GetFramebufferPixelSize();
            if(!pixels.Width || !pixels.Height) return {};
            static_cast<Camera::OrthographicCamera*>(app.m_EditorCamera)->SetOrthographic(0.0f,
                float(app.m_Height)*float(pixels.Width)/float(pixels.Height),float(app.m_Height),0.0f);
            Renderer2D::SetSurfaceSize(static_cast<int>(pixels.Width),static_cast<int>(pixels.Height));
            Renderer2D::RenderBegin(app.m_EditorCamera);
            Renderer2D::RenderSetup();
            if(app.m_GameState==GameState::ACTIVE) {
                Renderer2D::Render(std::vector{app.m_Background,app.m_Player},app.m_EditorCamera);
                app.m_Levels[app.m_Level].Render(app.m_EditorCamera);
                Renderer2D::Render(app.m_Ball,app.m_EditorCamera);
            }
            return {};
        }};
        if(auto result=FrameScheduler::Render(context);!result) {
            GENGINE_CORE_ERROR("{}",DescribeScheduleError(result.error())); ShutDown();
        }
    }

	void BreakoutApp::ProcessInput(Timestep ts)
	{
		BaseApp::ProcessInput(ts);
		float delta = ts * PLAYER_VELOCITY;
		float next_dist{};
		auto input = GetInputManager();
		auto& keyboardState = input->GetKeyboardState();
		if (keyboardState.IsKeyHeld(GENGINE_KEY_A))
		{
			next_dist = m_Player->GetPosition().x - delta;
			if (next_dist >= 0)
			{
				m_Player->SetPositionX(next_dist);
				if (m_Ball->GetIsStucked()) 
					m_Ball->SetPositionX(m_Ball->GetPosition().x - delta);
			}
		}

		if (keyboardState.IsKeyHeld(GENGINE_KEY_D))
		{
			next_dist = m_Player->GetPosition().x + delta;
			if (next_dist <= m_Width - PLAYER_SIZE.x)
			{
				m_Player->SetPositionX(next_dist);
				if (m_Ball->GetIsStucked()) 
					m_Ball->SetPositionX(m_Ball->GetPosition().x + delta);
			}
		}

		if (keyboardState.IsKeyPressed(GENGINE_KEY_SPACE))
			m_Ball->GetIsStucked() = false;

		if (keyboardState.IsKeyPressed(GENGINE_KEY_B))
		{
			//GENGINE_CORE_INFO("B pressed");
			
			//m_AudioSystem->PlayEvent("event:/Explosion2D");
		}

	}

	void BreakoutApp::ResetLevel()
	{
		if (m_Level == 0) { auto loaded = m_Levels[0].Load(levelPath + "one" + levelFileExtension, m_Width, m_Height / 2); if (!loaded) { ReportApplicationError(loaded.error()); m_Running = false; } }
		else if (m_Level == 1) { auto loaded = m_Levels[1].Load(levelPath + "two" + levelFileExtension, m_Width, m_Height / 2); if (!loaded) { ReportApplicationError(loaded.error()); m_Running = false; } }
		else if (m_Level == 2) { auto loaded = m_Levels[2].Load(levelPath + "three" + levelFileExtension, m_Width, m_Height / 2); if (!loaded) { ReportApplicationError(loaded.error()); m_Running = false; } }
		else if (m_Level == 3) { auto loaded = m_Levels[3].Load(levelPath + "four" + levelFileExtension, m_Width, m_Height / 2); if (!loaded) { ReportApplicationError(loaded.error()); m_Running = false; } }
	}


	void BreakoutApp::ResetPlayer()
	{
		m_Player->GetSpriteComponent().Size = PLAYER_SIZE;
		m_Player->SetPosition(Vec3f(m_Width / 2.0f - PLAYER_SIZE.x / 2.0f, m_Height - PLAYER_SIZE.y, 0.f));
		m_Ball->Reset(m_Player->Get2DPosition() + Vec2f{ PLAYER_SIZE.x / 2.0f - BALL_RADIUS, -(BALL_RADIUS * 2.0f) }, INITIAL_BALL_VELOCITY);
		m_Ball->m_IsStuck = true;
	}

	CollisionInfo BreakoutApp::CheckCollisions(BallEntity* ball, SpriteEntity* sprite)
	{
		// get center point circle first 
		Vec2f center(Vec2f{ ball->GetPosition().x, ball->GetPosition().y } + ball->m_Radius);
		// calculate AABB info (center, half-extents)
		Vec2f aabb_half_extents(sprite->GetSpriteComponent().Size.x / 2.0f, sprite->GetSpriteComponent().Size.y / 2.0f);
		Vec2f aabb_center(
			sprite->GetPosition().x + aabb_half_extents.x,
			sprite->GetPosition().y + aabb_half_extents.y
		);
		// get difference vector between both centers
		Vec2f difference = center - aabb_center;
		Vec2f clamped = glm::clamp(difference, -aabb_half_extents, aabb_half_extents);
		// add clamped value to AABB_center and we get the value of box closest to circle
		Vec2f closest = aabb_center + clamped;
		// retrieve vector between center circle and closest point AABB and check if length <= radius
		difference = closest - center;

		if (glm::length(difference) < ball->m_Radius)
		{
			return { true, VectorDirection(difference), difference };
		}

		else
			return { false, CollisionDirection::UP, Vec2f(0.f, 0.f) };
	}

}

BaseApp* CreateApp()
{
	return new BreakoutApp();
}
