#include"gepch.h"
#include "Core/RuntimeAssets.h"
#include"Audio/AudioSystem.h"
#include "Math/Matrix.h"
#include "fmod/fmod_studio.hpp"
#include "fmod/fmod_errors.h"
#include"Audio/SoundEvent.h"




namespace GEngine::Audio
{

	AudioSystem::AudioSystem(): 
		mSystem(nullptr), mLowLevelSystem(nullptr)
	{

	}

    namespace
    {
        PlatformError AudioFailure(const char* operation, FMOD_RESULT result, const std::string& resource = {})
        {
            return {PlatformErrorCode::Initialization, operation, "resource=" + resource +
                "; audio-code=" + std::to_string(static_cast<int>(result)) + "; " + FMOD_ErrorString(result)};
        }
    }
    PlatformResult AudioSystem::Initialize()
    {
        if (mSystem) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "audio startup", "Audio system is already initialized"});
        std::array<std::string, 3> banks;
        const std::array names{"Audio/Bank/Master Bank.strings.bank", "Audio/Bank/Master Bank.bank", "Audio/Bank/ZeldaTheme.bank"};
        for (size_t i = 0; i < names.size(); ++i) {
            auto path = RuntimeAssets::TryFile(names[i]);
            if (!path) return std::unexpected(path.error());
            banks[i] = std::move(*path);
        }
        // Debug logging availability is optional; device/system/bank failures are not.
        FMOD::Debug_Initialize(FMOD_DEBUG_LEVEL_ERROR, FMOD_DEBUG_MODE_TTY);
        struct Rollback {
            AudioSystem& owner; bool committed = false;
            ~Rollback() { if (!committed) owner.Shutdown(); }
        } rollback{*this};
        if (auto result = FMOD::Studio::System::create(&mSystem); result != FMOD_OK)
            return std::unexpected(AudioFailure("audio system creation", result));
        if (auto result = mSystem->initialize(512, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr); result != FMOD_OK)
            return std::unexpected(AudioFailure("audio system initialization", result));
        if (auto result = mSystem->getLowLevelSystem(&mLowLevelSystem); result != FMOD_OK)
            return std::unexpected(AudioFailure("audio low-level system", result));
        for (const auto& bank : banks) if (auto loaded = LoadBank(bank); !loaded) return loaded;
        rollback.committed = true;
        return {};
    }

    void AudioSystem::Shutdown()
    {
        UnloadAllBanks();
        mBuses.clear();
        mEventInstances.clear();
        if (mSystem) mSystem->release();
        mSystem = nullptr;
        mLowLevelSystem = nullptr;
    }

    PlatformResult AudioSystem::LoadBank(const std::string& name)
    {
        if (!mSystem) return std::unexpected(PlatformError{PlatformErrorCode::InvalidState, "audio bank load", "Audio system is not initialized: " + name});
        if (mBanks.contains(name)) return {};
        FMOD::Studio::Bank* bank = nullptr;
        if (auto result = mSystem->loadBankFile(name.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank); result != FMOD_OK)
            return std::unexpected(AudioFailure("audio bank load", result, name));
        struct Rollback {
            FMOD::Studio::Bank* bank; bool committed = false;
            ~Rollback() { if (!committed) { bank->unloadSampleData(); bank->unload(); } }
        } rollback{bank};
        if (auto result = bank->loadSampleData(); result != FMOD_OK)
            return std::unexpected(AudioFailure("audio bank samples", result, name));
        int numEvents = 0, numBuses = 0;
        if (auto result = bank->getEventCount(&numEvents); result != FMOD_OK)
            return std::unexpected(AudioFailure("audio event count", result, name));
        if (auto result = bank->getBusCount(&numBuses); result != FMOD_OK)
            return std::unexpected(AudioFailure("audio bus count", result, name));
        std::unordered_map<std::string, FMOD::Studio::EventDescription*> pendingEvents;
        std::unordered_map<std::string, FMOD::Studio::Bus*> pendingBuses;
        if (numEvents > 0) {
            std::vector<FMOD::Studio::EventDescription*> events(numEvents);
            if (auto result = bank->getEventList(events.data(), numEvents, &numEvents); result != FMOD_OK)
                return std::unexpected(AudioFailure("audio event list", result, name));
            for (int i = 0; i < numEvents; ++i) {
                char path[512]{};
                if (auto result = events[i]->getPath(path, sizeof(path), nullptr); result != FMOD_OK)
                    return std::unexpected(AudioFailure("audio event path", result, name));
                pendingEvents.emplace(path, events[i]);
            }
        }
        if (numBuses > 0) {
            std::vector<FMOD::Studio::Bus*> buses(numBuses);
            if (auto result = bank->getBusList(buses.data(), numBuses, &numBuses); result != FMOD_OK)
                return std::unexpected(AudioFailure("audio bus list", result, name));
            for (int i = 0; i < numBuses; ++i) {
                char path[512]{};
                if (auto result = buses[i]->getPath(path, sizeof(path), nullptr); result != FMOD_OK)
                    return std::unexpected(AudioFailure("audio bus path", result, name));
                pendingBuses.emplace(path, buses[i]);
            }
        }
        mBanks.emplace(name, bank);
        mEvents.merge(pendingEvents);
        mBuses.merge(pendingBuses);
        rollback.committed = true;
        return {};
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