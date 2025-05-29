#include <string>
#include"Math/Math.h"

enum FMOD_STUDIO_PLAYBACK_STATE;

namespace GEngine::Audio
{

	enum AUDIO_PLAYBACK_STATE
	{
		PLAYBACK_PLAYING,               /* Currently playing. */
		PLAYBACK_SUSTAINING,            /* The timeline cursor is paused on a sustain point. */
		PLAYBACK_STOPPED,               /* Not playing. */
		PLAYBACK_STARTING,              /* Start has been called but the instance is not fully started yet. */
		PLAYBACK_STOPPING,
	};

	class AudioSystem;

	class SoundEvent
	{

	public:

		SoundEvent();
		// Returns true if associated FMOD event still exists
		[[nodiscard]] bool IsValid() const;

		// Restart event from begining
		void Restart() const;

		// Stop this event
		void Stop(bool allowFadeOut = true) const;

		// Setters
		void SetPaused(bool pause) const;
		void SetVolume(float value) const;
		void SetPitch(float value) const;
		void SetParameter(const std::string& name, float value) const;
		AUDIO_PLAYBACK_STATE GetPlayState()const;


		// Getters
		[[nodiscard]] bool GetPaused() const;
		[[nodiscard]] float GetVolume() const;
		[[nodiscard]] float GetPitch() const;
		[[nodiscard]] float GetParameter(const std::string& name) const;

		// Positional
		[[nodiscard]] bool Is3D() const;
		void Set3DAttributes(const Math::Mat4& worldTrans) const;



	//protected:
		// Make this constructor protected and AudioSystem a friend
		// so that only AudioSystem can access this constructor.
		//friend class AudioSystem;
		SoundEvent(AudioSystem* system, unsigned int id);

	private:

		AudioSystem* m_AudioSystem{};
		unsigned int m_ID{};

	};

}
