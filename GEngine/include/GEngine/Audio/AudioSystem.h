#include <unordered_map>
#include <string>
#include"Math/Math.h"


namespace FMOD
{
	class System;

	namespace Studio
	{
		class Bank;
		class EventDescription;
		class EventInstance;
		class System;
		class Bus;
	}
}

namespace GEngine
{
	namespace Audio
	{
		class SoundEvent;

		class AudioSystem
		{
		public:
			AudioSystem();
			~AudioSystem() = default;

			void Initialize();
			void Shutdown();

			// Load/unload banks
			void LoadBank(const std::string& name);
			void UnloadBank(const std::string& name);
			void UnloadAllBanks();

			//ScopedPtr<SoundEvent> PlayEvent(const std::string& name);
			SoundEvent PlayEvent(const std::string& name);
			void Update(float deltaTime);

			// For positional audio
			void SetListener(const Math::Mat4& viewMatrix) const;

			// Control buses
			[[nodiscard]] float GetBusVolume(const std::string& name) const;
			[[nodiscard]] bool GetBusPaused(const std::string& name) const;
			void SetBusVolume(const std::string& name, float volume);
			void SetBusPaused(const std::string& name, bool pause);

		protected:
			friend class SoundEvent;
			FMOD::Studio::EventInstance* GetEventInstance(unsigned int id);

		private:

			// Tracks the next ID to use for event instances
			inline static unsigned int sNextID = 0;

			// Map of loaded banks
			std::unordered_map<std::string, FMOD::Studio::Bank*> mBanks;

			// Map of event name to EventDescription
			std::unordered_map<std::string, FMOD::Studio::EventDescription*> mEvents;

			// Map of event id to EventInstance
			std::unordered_map<unsigned int, FMOD::Studio::EventInstance*> mEventInstances;

			// Map of buses
			std::unordered_map<std::string, FMOD::Studio::Bus*> mBuses;

			// FMOD studio system
			FMOD::Studio::System* mSystem{};

			// FMOD Low-level system (in case needed)
			FMOD::System* mLowLevelSystem{};
		};
	}
}