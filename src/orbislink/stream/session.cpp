// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/session.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/stream/chiaki_log_bridge.h"
#include "orbislink/stream/credentials.h"
#include "orbislink/stream/stream_trace.h"

#include <chiaki/controller.h>
#include <chiaki/ffmpegdecoder.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/opusencoder.h>
#include <chiaki/session.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <atomic>
#include <cstring>
#include <mutex>

namespace orbislink {

const char *sessionStateName(SessionState state)
{
	switch(state)
	{
		case SessionState::Idle: return "idle";
		case SessionState::Connecting: return "connecting";
		case SessionState::Connected: return "connected";
		case SessionState::Stopped: return "stopped";
		case SessionState::Failed: return "failed";
	}
	return "unknown";
}

struct StreamSession::Impl
{
	std::mutex mutex;
	std::mutex stopMutex;
	ChiakiSession session {};
	ChiakiFfmpegDecoder decoder {};
	bool sessionStarted = false;
	bool decoderReady = false;
	bool usingHardware = false;

	std::atomic<bool> active { false };
	std::atomic<int> state { static_cast<int>(SessionState::Idle) };
	std::atomic<int> width { 0 };
	std::atomic<int> height { 0 };
	std::atomic<uint64_t> frames { 0 };
	ChiakiControllerState controller {};
	bool controllerInitialised = false;

	ChiakiOpusDecoder audioDecoder {};
	bool audioReady = false;

	// Microphone. encoderReady tells "prepared" apart from "sending": the
	// encoder is set up with the session, but nothing is sent until
	// startMicrophone().
	ChiakiOpusEncoder audioEncoder {};
	bool encoderReady = false;
	std::mutex micMutex;
	std::atomic<bool> micActive { false };
	std::atomic<bool> micMuted { false };
	AudioSettingsCallback onAudioSettings;
	AudioCallback onAudio;
	RumbleCallback onRumble;

	FrameCallback onFrame;
	StateCallback onState;
	LoginPinCallback onLoginPin;
	std::string host;

	void publish(SessionState newState, const std::string &detail)
	{
		state.store(static_cast<int>(newState));
		StateCallback callback;
		{
			std::lock_guard<std::mutex> lock(mutex);
			callback = onState;
		}
		if(callback)
			callback(newState, detail);
	}
};

namespace {

const char *quitReasonText(ChiakiQuitReason reason)
{
	switch(reason)
	{
		case CHIAKI_QUIT_REASON_STOPPED:
			return QT_TRANSLATE_NOOP("Messages", "Session ended.");
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
			return QT_TRANSLATE_NOOP("Messages", "The console refused the connection. Check that Remote "
				"Play is enabled.");
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
			return QT_TRANSLATE_NOOP("Messages", "The console is already being used by another Remote "
				"Play session.");
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
			return QT_TRANSLATE_NOOP("Messages", "Remote Play crashed on the console. Restart the "
				"console.");
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
			return QT_TRANSLATE_NOOP("Messages", "The console's Remote Play version is not compatible.");
		case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED:
			return QT_TRANSLATE_NOOP("Messages", "Could not connect to the console's control channel.");
		case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED:
			return QT_TRANSLATE_NOOP("Messages", "The console refused the control channel. Register the "
				"PC again.");
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
			return QT_TRANSLATE_NOOP("Messages", "The console ended the session.");
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
			return QT_TRANSLATE_NOOP("Messages", "The console shut down.");
		case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED:
			return QT_TRANSLATE_NOOP("Messages", "Registration through PSN failed.");
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN:
		case CHIAKI_QUIT_REASON_CTRL_UNKNOWN:
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN:
		case CHIAKI_QUIT_REASON_NONE:
			break;
	}
	return QT_TRANSLATE_NOOP("Messages", "The session ended for an unknown reason.");
}

// The name FFmpeg gives to each system's GPU decoder. Nothing is made
// up: these are the names av_hwdevice_find_type_by_name recognises.
const char *hardwareDecoderName()
{
#if defined(_WIN32)
	return "d3d11va";
#elif defined(__APPLE__)
	return "videotoolbox";
#else
	return "vaapi";
#endif
}

ChiakiVideoResolutionPreset resolutionPreset(int resolution)
{
	switch(resolution)
	{
		case 360: return CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
		case 540: return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
		case 1080: return CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
		case 720:
		default: return CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
	}
}

// Called by the decoder when a frame is ready. Runs on the decoder
// thread.
void frameAvailable(ChiakiFfmpegDecoder *decoder, void *user)
{
	auto *impl = static_cast<StreamSession::Impl *>(user);
	int32_t framesLost = 0;
	ChiakiFfmpegFrame pulled = chiaki_ffmpeg_decoder_pull_frame(decoder, &framesLost);
	if(!pulled.frame)
		return;

	const uint64_t previous = impl->frames.fetch_add(1);
	if(previous == 0)
	{
		StreamTrace::instance().ok(std::to_string(pulled.frame->width) + "x"
			+ std::to_string(pulled.frame->height) + ", formato "
			+ std::to_string(pulled.frame->format));
		StreamTrace::instance().end();
	}
	impl->width.store(pulled.frame->width);
	impl->height.store(pulled.frame->height);

	StreamSession::FrameCallback callback;
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		callback = impl->onFrame;
	}
	if(callback)
		callback(pulled.frame);
	av_frame_free(&pulled.frame);
}

void audioSettings(uint32_t channels, uint32_t rate, void *user)
{
	auto *impl = static_cast<StreamSession::Impl *>(user);
	StreamSession::AudioSettingsCallback callback;
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		callback = impl->onAudioSettings;
	}
	StreamTrace::instance().note("audio at " + std::to_string(rate) + " Hz, "
		+ std::to_string(channels) + " channels");
	if(callback)
		callback(channels, rate);
}

void audioFrame(int16_t *buffer, size_t samples, void *user)
{
	auto *impl = static_cast<StreamSession::Impl *>(user);
	StreamSession::AudioCallback callback;
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		callback = impl->onAudio;
	}
	if(callback)
		callback(buffer, samples);
}

void eventCallback(ChiakiEvent *event, void *user)
{
	auto *impl = static_cast<StreamSession::Impl *>(user);
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			impl->active.store(true);
			StreamTrace::instance().ok("the console accepted the session");
			StreamTrace::instance().step("first frame",
				"waiting for the console to start sending video");
			impl->publish(SessionState::Connected, QT_TRANSLATE_NOOP("Messages", "Connected to the console."));
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
		{
			StreamSession::LoginPinCallback callback;
			{
				std::lock_guard<std::mutex> lock(impl->mutex);
				callback = impl->onLoginPin;
			}
			StreamTrace::instance().note(event->login_pin_request.pin_incorrect
				? "the console says the sign-in PIN was wrong"
				: "the console asked for the account sign-in PIN");
			if(callback)
				callback(event->login_pin_request.pin_incorrect);
			break;
		}
		case CHIAKI_EVENT_RUMBLE:
		{
			StreamSession::RumbleCallback callback;
			{
				std::lock_guard<std::mutex> lock(impl->mutex);
				callback = impl->onRumble;
			}
			if(callback)
				callback(event->rumble.left, event->rumble.right);
			break;
		}
		case CHIAKI_EVENT_NICKNAME_RECEIVED:
			StreamTrace::instance().note(std::string("the console says its name is \"")
				+ event->server_nickname + "\"");
			break;
		case CHIAKI_EVENT_QUIT:
		{
			impl->active.store(false);
			const bool cleaned = event->quit.reason == CHIAKI_QUIT_REASON_STOPPED;
			std::string detail = quitReasonText(event->quit.reason);
			if(event->quit.reason_str && *event->quit.reason_str)
				detail += std::string(" (") + event->quit.reason_str + ")";
			if(cleaned)
				StreamTrace::instance().note("session ended on request");
			else
				StreamTrace::instance().fail(detail);
			StreamTrace::instance().end();
			impl->publish(cleaned ? SessionState::Stopped : SessionState::Failed, detail);
			break;
		}
		default:
			break;
	}
}

} // namespace

StreamSession::StreamSession() : impl_(std::make_unique<Impl>()) {}

StreamSession::~StreamSession() { stop(); }

void StreamSession::setFrameCallback(FrameCallback callback)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->onFrame = std::move(callback);
}

void StreamSession::setStateCallback(StateCallback callback)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->onState = std::move(callback);
}

void StreamSession::setLoginPinCallback(LoginPinCallback callback)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->onLoginPin = std::move(callback);
}

void StreamSession::setAudioCallbacks(AudioSettingsCallback settings, AudioCallback frames)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->onAudioSettings = std::move(settings);
	impl_->onAudio = std::move(frames);
}

void StreamSession::setRumbleCallback(RumbleCallback callback)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->onRumble = std::move(callback);
}

int StreamSession::startTouch(uint16_t x, uint16_t y)
{
	if(!impl_->sessionStarted || !impl_->active.load())
		return -1;
	std::lock_guard<std::mutex> lock(impl_->mutex);
	const int8_t id = chiaki_controller_state_start_touch(&impl_->controller, x, y);
	if(id >= 0)
		chiaki_session_set_controller_state(&impl_->session, &impl_->controller);
	return id;
}

void StreamSession::moveTouch(int id, uint16_t x, uint16_t y)
{
	if(!impl_->sessionStarted || !impl_->active.load() || id < 0)
		return;
	std::lock_guard<std::mutex> lock(impl_->mutex);
	chiaki_controller_state_set_touch_pos(&impl_->controller, static_cast<uint8_t>(id), x, y);
	chiaki_session_set_controller_state(&impl_->session, &impl_->controller);
}

void StreamSession::stopTouch(int id)
{
	if(!impl_->sessionStarted || id < 0)
		return;
	std::lock_guard<std::mutex> lock(impl_->mutex);
	chiaki_controller_state_stop_touch(&impl_->controller, static_cast<uint8_t>(id));
	if(impl_->active.load())
		chiaki_session_set_controller_state(&impl_->session, &impl_->controller);
}

bool StreamSession::active() const { return impl_->active.load(); }

SessionState StreamSession::state() const
{
	return static_cast<SessionState>(impl_->state.load());
}

int StreamSession::frameWidth() const { return impl_->width.load(); }
int StreamSession::frameHeight() const { return impl_->height.load(); }
uint64_t StreamSession::framesDecoded() const { return impl_->frames.load(); }
bool StreamSession::usingHardwareDecoder() const { return impl_->usingHardware; }

bool StreamSession::start(const Config &config, std::string *error)
{
	if(impl_->sessionStarted)
	{
		// A session that has already ended must not block the next one.
		//
		// chiaki's QUIT event publishes the state but tears nothing down — it
		// cannot, it is running on one of chiaki's own threads — and sessionStarted
		// would stay true forever. Here the previous one is cleaned up and we move on.
		if(impl_->active.load())
		{
			if(error)
				*error = QT_TRANSLATE_NOOP("Messages", "A session is already running. End it before "
					"starting another.");
			return false;
		}
		logInfo("Previous session already ended; tidying it up before connecting again.");
		stop();
	}
	if(config.address.empty())
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The console's address is missing.");
		return false;
	}
	if(!config.credentials.valid)
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The console has not been registered yet. Click its "
				"box to register it.");
		return false;
	}

	// Counters for this session: the "first frame" is this one's first, not
	// the first since the application opened.
	impl_->frames.store(0);
	impl_->width.store(0);
	impl_->height.store(0);

	StreamTrace::instance().begin(config.address);
	StreamStep prepareStep("prepare session",
		std::to_string(config.settings.resolution) + "p"
			+ std::to_string(config.settings.fps) + ", console "
			+ (config.credentials.ps5 ? "PS5" : "PS4"));

	ChiakiConnectVideoProfile profile {};
	chiaki_connect_video_profile_preset(&profile,
		resolutionPreset(config.settings.resolution),
		config.settings.fps == 30 ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60);
	if(config.settings.bitrateKbps > 0)
		profile.bitrate = config.settings.bitrateKbps;

	// The decoder has to exist before the session: it receives the video
	// samples.
	//
	// On the GPU it is much lighter, but not every machine manages it —
	// so it is tried, and if it fails it falls back to the CPU instead of
	// leaving the user without a picture. The report says which one was used.
	ChiakiErrorCode decoderResult = CHIAKI_ERR_UNKNOWN;
	impl_->usingHardware = false;
	if(config.settings.hardwareDecoder)
	{
		decoderResult = chiaki_ffmpeg_decoder_init(&impl_->decoder, chiakiLog(), profile.codec,
			static_cast<unsigned int>(config.settings.fps), hardwareDecoderName(), nullptr,
			frameAvailable, impl_.get());
		if(decoderResult == CHIAKI_ERR_SUCCESS)
		{
			impl_->usingHardware = true;
			StreamTrace::instance().note(std::string("decoding on the graphics card (")
				+ hardwareDecoderName() + ")");
		}
		else
		{
			StreamTrace::instance().note(std::string("the graphics card refused (")
				+ hardwareDecoderName() + "): " + chiaki_error_string(decoderResult)
				+ " — using the processor");
		}
	}
	if(!impl_->usingHardware)
	{
		decoderResult = chiaki_ffmpeg_decoder_init(&impl_->decoder, chiakiLog(), profile.codec,
			static_cast<unsigned int>(config.settings.fps), nullptr, nullptr, frameAvailable,
			impl_.get());
		if(decoderResult == CHIAKI_ERR_SUCCESS && config.settings.hardwareDecoder)
			StreamTrace::instance().note("decoding on the processor");
	}
	if(decoderResult != CHIAKI_ERR_SUCCESS)
	{
		prepareStep.fail(std::string("video decoder: ")
			+ chiaki_error_string(decoderResult));
		if(error)
			*error = std::string(QT_TRANSLATE_NOOP("Messages", "Could not set up the video decoder")) + ": "
				+ chiaki_error_string(decoderResult);
		return false;
	}
	impl_->decoderReady = true;

	impl_->host = config.address;

	ChiakiConnectInfo info {};
	info.ps5 = config.credentials.ps5;
	info.host = impl_->host.c_str();
	// The registration key has to fill the whole field, zero-padded on the right.
	std::memset(info.regist_key, 0, sizeof(info.regist_key));
	std::memcpy(info.regist_key, config.credentials.registKey.c_str(),
		std::min(config.credentials.registKey.size(), sizeof(info.regist_key)));
	if(!hexToBytes(config.credentials.rpKeyHex, info.morning, sizeof(info.morning)))
	{
		chiaki_ffmpeg_decoder_fini(&impl_->decoder);
		impl_->decoderReady = false;
		prepareStep.fail("the stored rp_key has "
			+ std::to_string(config.credentials.rpKeyHex.size())
			+ " characters, it should have 32");
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The saved key is corrupted. Register the console "
				"again.");
		return false;
	}
	info.video_profile = profile;
	info.video_profile_auto_downgrade = true;
	info.enable_keyboard = false;
	info.enable_dualsense = false;
	info.auto_regist = false;
	info.packet_loss_max = 0.05;
	info.enable_idr_on_fec_failure = true;

	const ChiakiErrorCode result = chiaki_session_init(&impl_->session, &info, chiakiLog());
	if(result != CHIAKI_ERR_SUCCESS)
	{
		prepareStep.fail(std::string("chiaki_session_init: ") + chiaki_error_string(result));
		chiaki_ffmpeg_decoder_fini(&impl_->decoder);
		impl_->decoderReady = false;
		if(error)
			*error = chiaki_error_string(result);
		return false;
	}

	chiaki_session_set_event_cb(&impl_->session, eventCallback, impl_.get());

	// Audio: chiaki delivers Opus, its decoder returns PCM.
	chiaki_opus_decoder_init(&impl_->audioDecoder, chiakiLog());
	chiaki_opus_decoder_set_cb(&impl_->audioDecoder, audioSettings, audioFrame, impl_.get());
	ChiakiAudioSink audioSink {};
	chiaki_opus_decoder_get_sink(&impl_->audioDecoder, &audioSink);
	chiaki_session_set_audio_sink(&impl_->session, &audioSink);
	impl_->audioReady = true;

	// The microphone path is set up, but silent. The format comes from
	// chiaki-ng and is not negotiable: 2 channels, 16 bits, 48 kHz, frames of
	// 480 samples. chiaki handles Opus; we only deliver PCM.
	chiaki_opus_encoder_init(&impl_->audioEncoder, chiakiLog());
	ChiakiAudioHeader micHeader {};
	chiaki_audio_header_set(&micHeader, static_cast<uint8_t>(kMicrophoneChannels), 16,
		kMicrophoneRate, kMicrophoneFrameSamples);
	chiaki_opus_encoder_header(&micHeader, &impl_->audioEncoder, &impl_->session);
	impl_->encoderReady = true;

	chiaki_session_set_video_sample_cb(&impl_->session, chiaki_ffmpeg_decoder_video_sample_cb,
		&impl_->decoder);

	prepareStep.ok();
	StreamTrace::instance().step("connect",
		std::string(config.credentials.ps5 ? "9302" : "987")
			+ "/UDP discovery, 9295/TCP control, 9296-9297/UDP stream");
	impl_->publish(SessionState::Connecting, QT_TRANSLATE_NOOP("Messages", "Connecting to the console…"));
	const ChiakiErrorCode started = chiaki_session_start(&impl_->session);
	if(started != CHIAKI_ERR_SUCCESS)
	{
		StreamTrace::instance().fail(std::string("chiaki_session_start: ")
			+ chiaki_error_string(started));
		chiaki_session_fini(&impl_->session);
		chiaki_ffmpeg_decoder_fini(&impl_->decoder);
		impl_->decoderReady = false;
		impl_->publish(SessionState::Failed, chiaki_error_string(started));
		if(error)
			*error = chiaki_error_string(started);
		return false;
	}
	impl_->sessionStarted = true;
	return true;
}

void StreamSession::stop()
{
	// stop can come from two places at once: the "end session" button
	// (on a separate thread, so the window does not freeze) and the
	// destructor when the application closes. Without this, the second would
	// touch a session already destroyed.
	std::lock_guard<std::mutex> guard(impl_->stopMutex);
	if(!impl_->sessionStarted)
		return;
	chiaki_session_stop(&impl_->session);
	chiaki_session_join(&impl_->session);
	chiaki_session_fini(&impl_->session);
	impl_->sessionStarted = false;
	impl_->active.store(false);

	if(impl_->decoderReady)
	{
		chiaki_ffmpeg_decoder_fini(&impl_->decoder);
		impl_->decoderReady = false;
	}
	if(impl_->audioReady)
	{
		chiaki_opus_decoder_fini(&impl_->audioDecoder);
		impl_->audioReady = false;
	}
	{
		// The microphone has to close before the encoder: a frame arriving
		// in the middle of fini would touch memory already freed.
		std::lock_guard<std::mutex> micLock(impl_->micMutex);
		impl_->micActive.store(false);
		impl_->micMuted.store(false);
		if(impl_->encoderReady)
		{
			chiaki_opus_encoder_fini(&impl_->audioEncoder);
			impl_->encoderReady = false;
		}
	}
	if(static_cast<SessionState>(impl_->state.load()) != SessionState::Failed)
		impl_->publish(SessionState::Stopped, QT_TRANSLATE_NOOP("Messages", "Session ended."));
}

namespace {

// The chiaki_session_toggle_microphone parameter gives the state the
// microphone *is in*, not the one wanted: true means "unmute" and false
// means "mute" (that is how chiaki-ng itself uses it). Passing the wanted
// state muted the console precisely when the microphone was turned on.
void requestMute(ChiakiSession *session, bool mute)
{
	chiaki_session_toggle_microphone(session, !mute);
}

} // namespace

bool StreamSession::startMicrophone(std::string *error)
{
	if(!impl_->sessionStarted || !impl_->active.load())
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The Remote Play session is not connected.");
		return false;
	}
	std::lock_guard<std::mutex> lock(impl_->micMutex);
	if(!impl_->encoderReady)
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The audio encoder is not ready.");
		return false;
	}
	if(impl_->micActive.load())
		return true;

	// Tell the console. Without this the audio packets arrive and are ignored.
	const ChiakiErrorCode result = chiaki_session_connect_microphone(&impl_->session);
	if(result != CHIAKI_ERR_SUCCESS)
	{
		if(error)
			*error = chiaki_error_string(result);
		logWarning(std::string("The microphone did not start: ") + chiaki_error_string(result));
		return false;
	}
	requestMute(&impl_->session, false);
	impl_->micMuted.store(false);
	impl_->micActive.store(true);
	logInfo("Microphone on: sending to the console.");
	return true;
}

void StreamSession::stopMicrophone()
{
	std::lock_guard<std::mutex> lock(impl_->micMutex);
	if(!impl_->micActive.load())
		return;
	impl_->micActive.store(false);
	if(impl_->sessionStarted)
		requestMute(&impl_->session, true);
	logInfo("Microphone off.");
}

void StreamSession::setMicrophoneMuted(bool muted)
{
	std::lock_guard<std::mutex> lock(impl_->micMutex);
	if(!impl_->micActive.load())
		return;
	impl_->micMuted.store(muted);
	if(impl_->sessionStarted)
		requestMute(&impl_->session, muted);
}

bool StreamSession::microphoneActive() const { return impl_->micActive.load(); }

bool StreamSession::microphoneMuted() const { return impl_->micMuted.load(); }

void StreamSession::sendMicrophoneFrame(const int16_t *pcm, size_t samplesPerChannel)
{
	// Silence while muted: nothing is sent, instead of sending zeros. The
	// console notices the difference and the radio stays free.
	if(!pcm || !impl_->micActive.load() || impl_->micMuted.load())
		return;
	// The frame must be exactly the size the header announced: chiaki
	// reads frame_size samples from the buffer, without checking.
	if(samplesPerChannel != kMicrophoneFrameSamples)
	{
		logWarning("Microphone frame with " + std::to_string(samplesPerChannel)
			+ " samples; expected " + std::to_string(kMicrophoneFrameSamples)
			+ ". Dropped.");
		return;
	}
	std::lock_guard<std::mutex> lock(impl_->micMutex);
	if(!impl_->encoderReady || !impl_->sessionStarted)
		return;
	// chiaki does not modify the buffer, but the signature is not const.
	chiaki_opus_encoder_frame(const_cast<int16_t *>(pcm), &impl_->audioEncoder);
}

void StreamSession::sendController(const ControllerState &state)
{
	if(!impl_->sessionStarted || !impl_->active.load())
		return;

	std::lock_guard<std::mutex> lock(impl_->mutex);
	// Start from the current state so the touchpad touches, which live in
	// the same packet as the buttons, are not wiped.
	ChiakiControllerState fresh = impl_->controller;
	if(!impl_->controllerInitialised)
		chiaki_controller_state_set_idle(&fresh);
	fresh.buttons = state.buttons;
	fresh.l2_state = state.l2;
	fresh.r2_state = state.r2;
	fresh.left_x = state.leftX;
	fresh.left_y = state.leftY;
	fresh.right_x = state.rightX;
	fresh.right_y = state.rightY;

	// Repeating the same state is extra traffic for nothing.
	if(impl_->controllerInitialised && chiaki_controller_state_equals(&impl_->controller, &fresh))
		return;
	impl_->controller = fresh;
	impl_->controllerInitialised = true;
	chiaki_session_set_controller_state(&impl_->session, &fresh);
}

void StreamSession::setLoginPin(const std::string &pin)
{
	if(!impl_->sessionStarted || pin.empty())
		return;
	chiaki_session_set_login_pin(&impl_->session,
		reinterpret_cast<const uint8_t *>(pin.c_str()), pin.size());
}

} // namespace orbislink
