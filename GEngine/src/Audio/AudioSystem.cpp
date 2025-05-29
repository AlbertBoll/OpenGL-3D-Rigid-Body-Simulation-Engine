#include"gepch.h"
#include"Audio/AudioSystem.h"
#include "Math/Matrix.h"
#include "fmod/fmod_studio.hpp"
#include "fmod/fmod_errors.h"
#include"Audio/SoundEvent.h"

static std::string bankPath = "../GEngine/include/GEngine/Assets/Audio/Bank/";


namespace GEngine::Audio
{

	AudioSystem::AudioSystem(): 
		mSystem(nullptr), mLowLevelSystem(nullptr)
	{

	}

	void AudioSystem::Initialize()
	{
		// Initialize debug logging
		FMOD::Debug_Initialize(
			FMOD_DEBUG_LEVEL_ERROR, // Log only errors
			FMOD_DEBUG_MODE_TTY // Output to stdout
		);

	
		FMOD_RESULT result = FMOD::Studio::System::create(&mSystem);
		ASSERT(result == FMOD_OK, "Failed to create FMOD system: " + std::string(FMOD_ErrorString(result)));


		// Initialize FMOD studio system
		result = mSystem->initialize(
			512, // Max number of concurrent sounds
			FMOD_STUDIO_INIT_NORMAL, // Use default settings
			FMOD_INIT_NORMAL, // Use default settings
			nullptr // Usually null
		);

		ASSERT(result == FMOD_OK, "Failed to initialize FMOD system: " + std::string(FMOD_ErrorString(result)));


		// Save the low-level system pointer
		mSystem->getLowLevelSystem(&mLowLevelSystem);

		// Load the master banks (strings first)
		LoadBank(bankPath + "Master Bank.strings.bank");
		LoadBank(bankPath + "Master Bank.bank");
		//LoadBank(bankPath + "Zelda.bank");
		LoadBank(bankPath + "ZeldaTheme.bank");

	}

	void AudioSystem::Shutdown()
	{
		// Unload all banks
		UnloadAllBanks();
		// Shutdown FMOD system
		if (mSystem)
		{
			mSystem->release();
		}
	}

	void AudioSystem::LoadBank(const std::string& name)
	{
		// Prevent double-loading
		if (mBanks.find(name) != mBanks.end())
		{
			return;
		}
	
		// Try to load bank
		FMOD::Studio::Bank* bank = nullptr;
		const FMOD_RESULT result = mSystem->loadBankFile(
			name.c_str(), // File name of bank
			FMOD_STUDIO_LOAD_BANK_NORMAL, // Normal loading
			&bank // Save pointer to bank
		);

		const int maxPathLength = 512;
		if (result == FMOD_OK)
		{
			// Add bank to map
			mBanks.emplace(name, bank);
			// Load all non-streaming sample data
			bank->loadSampleData();
			// Get the number of events in this bank
			int numEvents = 0;
			bank->getEventCount(&numEvents);
			if (numEvents > 0)
			{
				// Get list of event descriptions in this bank
				std::vector<FMOD::Studio::EventDescription*> events(numEvents);
				bank->getEventList(events.data(), numEvents, &numEvents);
				char eventName[maxPathLength];
				for (int i = 0; i < numEvents; i++)
				{
					FMOD::Studio::EventDescription* e = events[i];
					// Get the path of this event (like event:/Explosion2D)
					e->getPath(eventName, maxPathLength, nullptr);
					// Add to event map
					mEvents.emplace(eventName, e);
				}
			}
			// Get the number of buses in this bank
			int numBuses = 0;
			bank->getBusCount(&numBuses);
			if (numBuses > 0)
			{
				// Get list of buses in this bank
				std::vector<FMOD::Studio::Bus*> buses(numBuses);
				bank->getBusList(buses.data(), numBuses, &numBuses);
				char busName[512];
				for (int i = 0; i < numBuses; i++)
				{
					FMOD::Studio::Bus* bus = buses[i];
					// Get the path of this bus (like bus:/SFX)
					bus->getPath(busName, 512, nullptr);
					// Add to buses map
					mBuses.emplace(busName, bus);
				}
			}
		}
	}

	void AudioSystem::UnloadBank(const std::string& name)
	{
		// Ignore if not loaded
		auto iter = mBanks.find(name);
		if (iter == mBanks.end())
		{
			return;
		}

		// First we need to remove all events from this bank
		FMOD::Studio::Bank* bank = iter->second;
		int numEvents = 0;
		bank->getEventCount(&numEvents);
		if (numEvents > 0)
		{
			// Get event descriptions for this bank
			std::vector<FMOD::Studio::EventDescription*> events(numEvents);
			// Get list of events
			bank->getEventList(events.data(), numEvents, &numEvents);
			char eventName[512];
			for (int i = 0; i < numEvents; i++)
			{
				FMOD::Studio::EventDescription* e = events[i];
				// Get the path of this event
				e->getPath(eventName, 512, nullptr);
				// Remove this event
				auto eventi = mEvents.find(eventName);
				if (eventi != mEvents.end())
				{
					mEvents.erase(eventi);
				}
			}
		}
		// Get the number of buses in this bank
		int numBuses = 0;
		bank->getBusCount(&numBuses);
		if (numBuses > 0)
		{
			// Get list of buses in this bank
			std::vector<FMOD::Studio::Bus*> buses(numBuses);
			bank->getBusList(buses.data(), numBuses, &numBuses);
			char busName[512];
			for (int i = 0; i < numBuses; i++)
			{
				FMOD::Studio::Bus* bus = buses[i];
				// Get the path of this bus (like bus:/SFX)
				bus->getPath(busName, 512, nullptr);
				// Remove this bus
				auto busi = mBuses.find(busName);
				if (busi != mBuses.end())
				{
					mBuses.erase(busi);
				}
			}
		}

		// Unload sample data and bank
		bank->unloadSampleData();
		bank->unload();
		// Remove from banks map
		mBanks.erase(iter);
	}

	void AudioSystem::UnloadAllBanks()
	{
		for (auto& iter : mBanks)
		{
			// Unload the sample data, then the bank itself
			iter.second->unloadSampleData();
			iter.second->unload();
		}
		mBanks.clear();
		// No banks means no events
		mEvents.clear();
	}

	//ScopedPtr<SoundEvent> AudioSystem::PlayEvent(const std::string& name)
	//{
	//	unsigned int retID = 0;
	//	if (const auto iter = mEvents.find(name); iter != mEvents.end())
	//	{
	//		// Create instance of event
	//		FMOD::Studio::EventInstance* event = nullptr;
	//		iter->second->createInstance(&event);
	//		if (event)
	//		{
	//			// Start the event instance
	//			event->start();
	//			// Get the next id, and add to map
	//			sNextID++;
	//			retID = sNextID;
	//			mEventInstances.emplace(retID, event);
	//		}
	//	}
	//	//return new SoundEvent(this, retID);
	//	
	//	return CreateScopedPtr<SoundEvent>(this, retID);
	//}

	SoundEvent AudioSystem::PlayEvent(const std::string& name)
	{
		unsigned int retID = 0;
		if (const auto iter = mEvents.find(name); iter != mEvents.end())
		{
			// Create instance of event
			FMOD::Studio::EventInstance* event = nullptr;
			iter->second->createInstance(&event);
			if (event)
			{
				// Start the event instance
				event->start();
				// Get the next id, and add to map
				sNextID++;
				retID = sNextID;
				mEventInstances.emplace(retID, event);
			}
		}

		return SoundEvent(this, retID);

	}

	void AudioSystem::Update(float deltaTime)
	{
		// Find any stopped event instances
		std::vector<unsigned int> done;
		for (auto& iter : mEventInstances)
		{
			FMOD::Studio::EventInstance* e = iter.second;
			// Get the state of this event
			FMOD_STUDIO_PLAYBACK_STATE state;
			e->getPlaybackState(&state);
			if (state == FMOD_STUDIO_PLAYBACK_STOPPED)
			{
				// Release the event and add id to done
				e->release();
				done.emplace_back(iter.first);
			}

		}

		// Remove done event instances from map
		for (auto id : done)
		{
			mEventInstances.erase(id);
		}

		// Update FMOD
		mSystem->update();
	}

	namespace
	{
		FMOD_VECTOR VecToFMOD(const Math::Vec3f& in)
		{
			// Convert from our coordinates (-z forward, +x right, +y up)
			// to FMOD (+z forward, +x right, +y up)
			FMOD_VECTOR v;
			v.x = in.x;
			v.y = in.y;
			v.z = -in.z;
			return v;
		}
	}

	void AudioSystem::SetListener(const Math::Mat4& viewMatrix) const
	{
		// Invert the view matrix to get the correct vectors
		using namespace GEngine::Math;
		auto invView = glm::inverse(viewMatrix);

		FMOD_3D_ATTRIBUTES listener;
		// Set position, forward, up
		listener.position = VecToFMOD(Matrix::GetTranslation(invView));
		// In the inverted view, third row is forward
		listener.forward = VecToFMOD(Matrix::GetZAxis(invView));
		// In the inverted view, second row is up
		listener.up = VecToFMOD(Matrix::GetYAxis(invView));
		// Set velocity to zero (fix if using Doppler effect)
		listener.velocity = { 0.0f, 0.0f, 0.0f };
		// Send to FMOD
		mSystem->setListenerAttributes(0, &listener);
	}

	float AudioSystem::GetBusVolume(const std::string& name) const
	{
		float retVal = 0.0f;
		const auto iter = mBuses.find(name);
		if (iter != mBuses.end())
		{
			iter->second->getVolume(&retVal);
		}
		return retVal;
	}

	bool AudioSystem::GetBusPaused(const std::string& name) const
	{
		bool retVal = false;
		const auto iter = mBuses.find(name);
		if (iter != mBuses.end())
		{
			iter->second->getPaused(&retVal);
		}
		return retVal;
	}

	void AudioSystem::SetBusVolume(const std::string& name, float volume)
	{
		if (auto iter = mBuses.find(name); iter != mBuses.end())
		{
			iter->second->setVolume(volume);
		}
	}

	void AudioSystem::SetBusPaused(const std::string& name, bool pause)
	{
		if (auto iter = mBuses.find(name); iter != mBuses.end())
		{
			iter->second->setPaused(pause);
		}
	}

	FMOD::Studio::EventInstance* AudioSystem::GetEventInstance(unsigned int id)
	{
		FMOD::Studio::EventInstance* event = nullptr;
		if (const auto iter = mEventInstances.find(id); iter != mEventInstances.end())
		{
			event = iter->second;
		}
		return event;
	}


	



}