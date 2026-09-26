// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/stream_types.h"

#include <functional>
#include <memory>
#include <string>

struct AVFrame;

namespace orbislink {

enum class SessionState
{
	Idle,
	Connecting,
	Connected,
	Stopped,
	Failed,
};

const char *sessionStateName(SessionState state);

struct SessionSettings
{
	// Presets do chiaki: 360p, 540p, 720p, 1080p.
	int resolution = 720;
	int fps = 60;
	// 0 = deixa o chiaki escolher pelo preset.
	unsigned int bitrateKbps = 0;
	bool hardwareDecoder = true;
};

// A Remote Play session. It runs on chiaki's threads; notifications come
// out through callbacks that may come from any thread.
class StreamSession
{
public:
	struct Config
	{
		std::string address;
		StreamCredentials credentials;
		SessionSettings settings;
	};

	// A decoded frame. The AVFrame belongs to the decoder and is only valid
	// during the call — whoever wants to keep it must copy or reference it
	// (av_frame_ref).
	using FrameCallback = std::function<void(AVFrame *frame)>;
	using StateCallback = std::function<void(SessionState state, const std::string &detail)>;
	// The console asks for the login PIN (when the user has a PIN set on
	// the account). Answer with setLoginPin().
	using LoginPinCallback = std::function<void(bool incorrect)>;
	// The console asks for rumble. 0-255 per motor.
	using RumbleCallback = std::function<void(uint8_t left, uint8_t right)>;
	// Already decoded audio: interleaved 16-bit PCM. `samples` is the number
	// of samples per channel.
	using AudioSettingsCallback = std::function<void(unsigned int channels, unsigned int rate)>;
	using AudioCallback = std::function<void(const int16_t *pcm, size_t samples)>;

	StreamSession();
	~StreamSession();

	void setFrameCallback(FrameCallback callback);
	void setStateCallback(StateCallback callback);
	void setLoginPinCallback(LoginPinCallback callback);
	void setAudioCallbacks(AudioSettingsCallback settings, AudioCallback frames);
	void setRumbleCallback(RumbleCallback callback);

	bool start(const Config &config, std::string *error = nullptr);
	void stop();
	bool active() const;
	SessionState state() const;

	void setLoginPin(const std::string &pin);

	// Controller state sent to the console. The buttons are chiaki's mask
	// (ChiakiControllerButton); the axes range from -32768 to 32767 and the
	// triggers from 0 to 255.
	struct ControllerState
	{
		uint32_t buttons = 0;
		uint8_t l2 = 0;
		uint8_t r2 = 0;
		int16_t leftX = 0;
		int16_t leftY = 0;
		int16_t rightX = 0;
		int16_t rightY = 0;
	};
	// Only sends when something changes: the console does not need repetition.
	void sendController(const ControllerState &state);

	// Touchpad. The coordinates are those of the controller's touchpad: 1920x942.
	// Returns the touch id, or -1 if there is no room.
	int startTouch(uint16_t x, uint16_t y);
	void moveTouch(int id, uint16_t x, uint16_t y);
	void stopTouch(int id);
	static constexpr uint16_t kTouchpadWidth = 1920;
	static constexpr uint16_t kTouchpadHeight = 942;

	// ── Microphone (this PC's sound to the console)
	//
	// Off by default and never turned on by itself. An application that opens
	// the microphone without being seen is something other than Remote Play,
	// so this only starts on request and the interface has to show it while
	// it is capturing.
	//
	// The format is not our choice: it is what the console expects and what
	// chiaki encodes — 48 kHz, 2 channels, 16 bits, 480 samples per frame
	// (10 ms). See chiaki_audio_header_set() in chiaki-ng's
	// streamsession.cpp.
	static constexpr unsigned int kMicrophoneRate = 48000;
	static constexpr unsigned int kMicrophoneChannels = 2;
	static constexpr unsigned int kMicrophoneFrameSamples = 480;

	// Avisa a consola de que vamos falar e prepara o codificador.
	bool startMicrophone(std::string *error = nullptr);
	void stopMicrophone();
	// Stays on, but stops sending. This is what the console expects from a
	// "mute" — really turning it off would require a new connection.
	void setMicrophoneMuted(bool muted);
	bool microphoneActive() const;
	bool microphoneMuted() const;
	// Uma trama de PCM intercalado, com exactamente kMicrophoneFrameSamples
	// amostras por canal. Chamado pela captura, fora da thread do chiaki.
	void sendMicrophoneFrame(const int16_t *pcm, size_t samplesPerChannel);

	// Dimensions of the last frame received (0 before the first).
	int frameWidth() const;
	int frameHeight() const;
	// Frames delivered since start — lets the UI know there is a picture
	// even before drawing it.
	uint64_t framesDecoded() const;
	// True when the video is being decoded on the GPU.
	bool usingHardwareDecoder() const;

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace orbislink
