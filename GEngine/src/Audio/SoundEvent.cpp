#include"gepch.h"
#include"Audio/SoundEvent.h"
#include <fmod/fmod_studio.hpp>
#include"Audio/AudioSystem.h"
#include"Math/Matrix.h"


namespace GEngine::Audio
{
	SoundEvent::SoundEvent():
		m_AudioSystem(nullptr), m_ID(0)
	{

	}

	SoundEvent::SoundEvent(AudioSystem* system, unsigned int id) 
		:m_AudioSystem(system), m_ID(id)
	{

	}

	bool SoundEvent::IsValid() const
	{
		return (m_AudioSystem && m_AudioSystem->GetEventInstance(m_ID) != nullptr);
	}

	void SoundEvent::Restart() const
	{
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID): nullptr; event)
		{
			event->start();
		}
	}

	void SoundEvent::Stop(bool allowFadeOut /* true */) const
	{
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			FMOD_STUDIO_STOP_MODE mode = allowFadeOut ?
				FMOD_STUDIO_STOP_ALLOWFADEOUT :
				FMOD_STUDIO_STOP_IMMEDIATE;
			event->stop(mode);
		}
	}



	void SoundEvent::SetPaused(bool pause) const
	{
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			event->setPaused(pause);
		}
	}

	void SoundEvent::SetVolume(float value) const
	{
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			event->setVolume(value);
		}
	}

	void SoundEvent::SetPitch(float value) const
	{
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			event->setPitch(value);
		}
	}

	void SoundEvent::SetParameter(const std::string& name, float value) const
	{
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			event->setParameterValue(name.c_str(), value);
		}
	}

	AUDIO_PLAYBACK_STATE SoundEvent::GetPlayState()const
	{
		// TODO: insert return statement here

		FMOD_STUDIO_PLAYBACK_STATE state;
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			auto it = event->getPlaybackState(&state);
			if (it == FMOD_RESULT::FMOD_OK)
			{
				return (AUDIO_PLAYBACK_STATE)state;

			}

		}
	}

	bool SoundEvent::GetPaused() const
	{
		bool retVal = false;
		if (const auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			event->getPaused(&retVal);
		}
		return retVal;
	}

	float SoundEvent::GetVolume() const
	{
		float retVal = 0.0f;
		if (const auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			event->getVolume(&retVal);
		}
		return retVal;
	}

	float SoundEvent::GetPitch() const
	{
		float retVal = 0.0f;
		if (const auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			event->getPitch(&retVal);
		}
		return retVal;
	}

	float SoundEvent::GetParameter(const std::string& name) const
	{
		float retVal = 0.0f;
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			event->getParameterValue(name.c_str(), &retVal);
		}
		return retVal;
	}

	bool SoundEvent::Is3D() const
	{
		bool retVal = false;
		if (const auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			// Get the event description
			FMOD::Studio::EventDescription* ed = nullptr;
			event->getDescription(&ed);
			if (ed)
			{
				ed->is3D(&retVal);
			}
		}
		return retVal;
	}

	namespace
	{
		FMOD_VECTOR VecToFMOD(const Math::Vec3f& in)
		{
			// Convert from our coordinates (+x forward, +y right, +z up)
			// to FMOD (+z forward, +x right, +y up)
			FMOD_VECTOR v;
			v.x = in.y;
			v.y = in.z;
			v.z = in.x;
			return v;
		}
	}

	void SoundEvent::Set3DAttributes(const Math::Mat4& worldTrans) const
	{

		using namespace Math;
		if (auto event = m_AudioSystem ? m_AudioSystem->GetEventInstance(m_ID) : nullptr; event)
		{
			FMOD_3D_ATTRIBUTES attr;
			// Set position, forward, up
			attr.position = VecToFMOD(Matrix::GetTranslation(worldTrans));
			// In world transform, first row is forward
			attr.forward = VecToFMOD(Matrix::GetXAxis(worldTrans));
			// Third row is up
			attr.up = VecToFMOD(Matrix::GetZAxis(worldTrans));
			// Set velocity to zero (fix if using Doppler effect)
			attr.velocity = { 0.0f, 0.0f, 0.0f };
			event->set3DAttributes(&attr);
		}
	}



}