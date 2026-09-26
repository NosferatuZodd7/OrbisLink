// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/session.h"

#include "orbislink/common/log.h"
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
		case SessionState::Idle: return "parado";
		case SessionState::Connecting: return "a ligar";
		case SessionState::Connected: return "ligado";
		case SessionState::Stopped: return "terminado";
		case SessionState::Failed: return "falhou";
	}
	return "desconhecido";
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

	// Microfone. O encoderReady distingue "preparado" de "a enviar": o
	// codificador é montado com a sessão, mas só se fala depois de
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
			return "Sessão terminada.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
			return "A consola recusou a ligação. Confirma que o Remote Play está activado.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
			return "A consola já está a ser usada por outra sessão de Remote Play.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
			return "O Remote Play estoirou na consola. Reinicia a consola.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
			return "A versão do Remote Play da consola não é compatível.";
		case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED:
			return "Não consegui ligar ao canal de controlo da consola.";
		case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED:
			return "A consola recusou o canal de controlo. Volta a registar o PC.";
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
			return "A consola desligou a sessão.";
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
			return "A consola desligou-se.";
		case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED:
			return "Falhou o registo pela PSN.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN:
		case CHIAKI_QUIT_REASON_CTRL_UNKNOWN:
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN:
		case CHIAKI_QUIT_REASON_NONE:
			break;
	}
	return "A sessão terminou por uma razão desconhecida.";
}

// O nome que o FFmpeg dá ao descodificador da placa gráfica de cada
// sistema. Não se inventa nada: são os nomes que o
// av_hwdevice_find_type_by_name reconhece.
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

// Chamado pelo descodificador quando há um fotograma pronto. Corre na
// thread do descodificador.
void frameAvailable(ChiakiFfmpegDecoder *decoder, void *user)
{
	auto *impl = static_cast<StreamSession::Impl *>(user);
	int32_t framesLost = 0;
	ChiakiFfmpegFrame pulled = chiaki_ffmpeg_decoder_pull_frame(decoder, &framesLost);
	if(!pulled.frame)
		return;

	const uint64_t anteriores = impl->frames.fetch_add(1);
	if(anteriores == 0)
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
	StreamTrace::instance().note("áudio a " + std::to_string(rate) + " Hz, "
		+ std::to_string(channels) + " canais");
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
			StreamTrace::instance().ok("a consola aceitou a sessão");
			StreamTrace::instance().step("primeiro fotograma",
				"à espera que a consola comece a enviar vídeo");
			impl->publish(SessionState::Connected, "Ligado à consola.");
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
		{
			StreamSession::LoginPinCallback callback;
			{
				std::lock_guard<std::mutex> lock(impl->mutex);
				callback = impl->onLoginPin;
			}
			StreamTrace::instance().note(event->login_pin_request.pin_incorrect
				? "a consola diz que o PIN de sessão estava errado"
				: "a consola pediu o PIN de início de sessão da conta");
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
			StreamTrace::instance().note(std::string("a consola diz chamar-se \"")
				+ event->server_nickname + "\"");
			break;
		case CHIAKI_EVENT_QUIT:
		{
			impl->active.store(false);
			const bool limpo = event->quit.reason == CHIAKI_QUIT_REASON_STOPPED;
			std::string detalhe = quitReasonText(event->quit.reason);
			if(event->quit.reason_str && *event->quit.reason_str)
				detalhe += std::string(" (") + event->quit.reason_str + ")";
			if(limpo)
				StreamTrace::instance().note("sessão terminada a pedido");
			else
				StreamTrace::instance().fail(detalhe);
			StreamTrace::instance().end();
			impl->publish(limpo ? SessionState::Stopped : SessionState::Failed, detalhe);
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
		// Uma sessão que já terminou não pode impedir a seguinte.
		//
		// O evento QUIT do chiaki publica o estado mas não desmonta nada — não
		// pode, está a correr numa thread do próprio chiaki — e o sessionStarted
		// ficaria a true para sempre. Aqui arruma-se a anterior e segue-se.
		if(impl_->active.load())
		{
			if(error)
				*error = "Já há uma sessão a decorrer. Termina-a antes de ligar outra.";
			return false;
		}
		logInfo("Sessão anterior já terminada; a arrumá-la antes de ligar de novo.");
		stop();
	}
	if(config.address.empty())
	{
		if(error)
			*error = "Falta o endereço da consola.";
		return false;
	}
	if(!config.credentials.valid)
	{
		if(error)
			*error = "A consola ainda não foi registada. Usa \"Registar consola\".";
		return false;
	}

	StreamTrace::instance().begin(config.address);
	StreamStep passoPreparar("preparar sessão",
		std::to_string(config.settings.resolution) + "p"
			+ std::to_string(config.settings.fps) + ", consola "
			+ (config.credentials.ps5 ? "PS5" : "PS4"));

	ChiakiConnectVideoProfile profile {};
	chiaki_connect_video_profile_preset(&profile,
		resolutionPreset(config.settings.resolution),
		config.settings.fps == 30 ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60);
	if(config.settings.bitrateKbps > 0)
		profile.bitrate = config.settings.bitrateKbps;

	// O descodificador tem de existir antes da sessão: é ele que recebe as
	// amostras de vídeo.
	//
	// Com a placa gráfica é muito mais leve, mas nem todas as máquinas
	// conseguem — por isso tenta-se, e se não der volta-se ao processador
	// em vez de deixar o utilizador sem imagem. O relatório diz qual saiu.
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
			StreamTrace::instance().note(std::string("descodificação pela placa gráfica (")
				+ hardwareDecoderName() + ")");
		}
		else
		{
			StreamTrace::instance().note(std::string("a placa gráfica recusou (")
				+ hardwareDecoderName() + "): " + chiaki_error_string(decoderResult)
				+ " — a usar o processador");
		}
	}
	if(!impl_->usingHardware)
	{
		decoderResult = chiaki_ffmpeg_decoder_init(&impl_->decoder, chiakiLog(), profile.codec,
			static_cast<unsigned int>(config.settings.fps), nullptr, nullptr, frameAvailable,
			impl_.get());
		if(decoderResult == CHIAKI_ERR_SUCCESS && config.settings.hardwareDecoder)
			StreamTrace::instance().note("descodificação pelo processador");
	}
	if(decoderResult != CHIAKI_ERR_SUCCESS)
	{
		passoPreparar.fail(std::string("descodificador de vídeo: ")
			+ chiaki_error_string(decoderResult));
		if(error)
			*error = std::string("Não consegui preparar o descodificador de vídeo: ")
				+ chiaki_error_string(decoderResult);
		return false;
	}
	impl_->decoderReady = true;

	impl_->host = config.address;

	ChiakiConnectInfo info {};
	info.ps5 = config.credentials.ps5;
	info.host = impl_->host.c_str();
	// A chave de registo tem de preencher o campo todo, com zeros à direita.
	std::memset(info.regist_key, 0, sizeof(info.regist_key));
	std::memcpy(info.regist_key, config.credentials.registKey.c_str(),
		std::min(config.credentials.registKey.size(), sizeof(info.regist_key)));
	if(!hexToBytes(config.credentials.rpKeyHex, info.morning, sizeof(info.morning)))
	{
		chiaki_ffmpeg_decoder_fini(&impl_->decoder);
		impl_->decoderReady = false;
		passoPreparar.fail("a rp_key guardada tem "
			+ std::to_string(config.credentials.rpKeyHex.size())
			+ " caracteres, deviam ser 32");
		if(error)
			*error = "A chave guardada está corrompida. Volta a registar a consola.";
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
		passoPreparar.fail(std::string("chiaki_session_init: ") + chiaki_error_string(result));
		chiaki_ffmpeg_decoder_fini(&impl_->decoder);
		impl_->decoderReady = false;
		if(error)
			*error = chiaki_error_string(result);
		return false;
	}

	chiaki_session_set_event_cb(&impl_->session, eventCallback, impl_.get());

	// Áudio: o chiaki entrega Opus, o descodificador dele devolve PCM.
	chiaki_opus_decoder_init(&impl_->audioDecoder, chiakiLog());
	chiaki_opus_decoder_set_cb(&impl_->audioDecoder, audioSettings, audioFrame, impl_.get());
	ChiakiAudioSink audioSink {};
	chiaki_opus_decoder_get_sink(&impl_->audioDecoder, &audioSink);
	chiaki_session_set_audio_sink(&impl_->session, &audioSink);
	impl_->audioReady = true;

	// O caminho do microfone fica montado, mas calado. O formato vem do
	// chiaki-ng e não é negociável: 2 canais, 16 bits, 48 kHz, tramas de
	// 480 amostras. O chiaki trata do Opus; nós só entregamos PCM.
	chiaki_opus_encoder_init(&impl_->audioEncoder, chiakiLog());
	ChiakiAudioHeader micHeader {};
	chiaki_audio_header_set(&micHeader, static_cast<uint8_t>(kMicrophoneChannels), 16,
		kMicrophoneRate, kMicrophoneFrameSamples);
	chiaki_opus_encoder_header(&micHeader, &impl_->audioEncoder, &impl_->session);
	impl_->encoderReady = true;

	chiaki_session_set_video_sample_cb(&impl_->session, chiaki_ffmpeg_decoder_video_sample_cb,
		&impl_->decoder);

	passoPreparar.ok();
	StreamTrace::instance().step("ligar",
		"987/UDP descoberta, 9295/TCP controlo, 9296-9297/UDP stream");
	impl_->publish(SessionState::Connecting, "A ligar a " + config.address + "…");
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
	// O stop pode vir de dois sítios ao mesmo tempo: do botão "terminar
	// sessão" (numa thread à parte, para a janela não congelar) e do
	// destrutor quando se fecha a aplicação. Sem isto, o segundo mexeria numa
	// sessão já destruída.
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
		// O microfone tem de fechar antes do codificador: uma trama a
		// chegar a meio do fini mexeria em memória já libertada.
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
		impl_->publish(SessionState::Stopped, "Sessão terminada.");
}

bool StreamSession::startMicrophone(std::string *error)
{
	if(!impl_->sessionStarted || !impl_->active.load())
	{
		if(error)
			*error = "A sessão de Remote Play não está ligada.";
		return false;
	}
	std::lock_guard<std::mutex> lock(impl_->micMutex);
	if(!impl_->encoderReady)
	{
		if(error)
			*error = "O codificador de áudio não ficou pronto.";
		return false;
	}
	if(impl_->micActive.load())
		return true;

	// Avisa a consola. Sem isto os pacotes de áudio chegam e são ignorados.
	const ChiakiErrorCode result = chiaki_session_connect_microphone(&impl_->session);
	if(result != CHIAKI_ERR_SUCCESS)
	{
		if(error)
			*error = chiaki_error_string(result);
		logWarning(std::string("O microfone não arrancou: ") + chiaki_error_string(result));
		return false;
	}
	chiaki_session_toggle_microphone(&impl_->session, false);
	impl_->micMuted.store(false);
	impl_->micActive.store(true);
	logInfo("Microfone ligado: a enviar para a consola.");
	return true;
}

void StreamSession::stopMicrophone()
{
	std::lock_guard<std::mutex> lock(impl_->micMutex);
	if(!impl_->micActive.load())
		return;
	impl_->micActive.store(false);
	if(impl_->sessionStarted)
		chiaki_session_toggle_microphone(&impl_->session, true);
	logInfo("Microfone desligado.");
}

void StreamSession::setMicrophoneMuted(bool muted)
{
	std::lock_guard<std::mutex> lock(impl_->micMutex);
	if(!impl_->micActive.load())
		return;
	impl_->micMuted.store(muted);
	if(impl_->sessionStarted)
		chiaki_session_toggle_microphone(&impl_->session, muted);
}

bool StreamSession::microphoneActive() const { return impl_->micActive.load(); }

bool StreamSession::microphoneMuted() const { return impl_->micMuted.load(); }

void StreamSession::sendMicrophoneFrame(const int16_t *pcm, size_t samplesPerChannel)
{
	// Silêncio enquanto estiver em mute: não se envia nada, em vez de
	// enviar zeros. A consola percebe a diferença e o rádio fica livre.
	if(!pcm || !impl_->micActive.load() || impl_->micMuted.load())
		return;
	// A trama tem de ter exactamente o tamanho que o cabeçalho anunciou: o
	// chiaki lê frame_size amostras do buffer, sem verificar.
	if(samplesPerChannel != kMicrophoneFrameSamples)
	{
		logWarning("Trama de microfone com " + std::to_string(samplesPerChannel)
			+ " amostras; esperavam-se " + std::to_string(kMicrophoneFrameSamples)
			+ ". Descartada.");
		return;
	}
	std::lock_guard<std::mutex> lock(impl_->micMutex);
	if(!impl_->encoderReady || !impl_->sessionStarted)
		return;
	// O chiaki não altera o buffer, mas a assinatura não é const.
	chiaki_opus_encoder_frame(const_cast<int16_t *>(pcm), &impl_->audioEncoder);
}

void StreamSession::sendController(const ControllerState &state)
{
	if(!impl_->sessionStarted || !impl_->active.load())
		return;

	std::lock_guard<std::mutex> lock(impl_->mutex);
	// Parte-se do estado actual para não apagar os toques do touchpad, que
	// vivem no mesmo pacote que os botões.
	ChiakiControllerState novo = impl_->controller;
	if(!impl_->controllerInitialised)
		chiaki_controller_state_set_idle(&novo);
	novo.buttons = state.buttons;
	novo.l2_state = state.l2;
	novo.r2_state = state.r2;
	novo.left_x = state.leftX;
	novo.left_y = state.leftY;
	novo.right_x = state.rightX;
	novo.right_y = state.rightY;

	// Repetir o mesmo estado é tráfego a mais sem nada em troca.
	if(impl_->controllerInitialised && chiaki_controller_state_equals(&impl_->controller, &novo))
		return;
	impl_->controller = novo;
	impl_->controllerInitialised = true;
	chiaki_session_set_controller_state(&impl_->session, &novo);
}

void StreamSession::setLoginPin(const std::string &pin)
{
	if(!impl_->sessionStarted || pin.empty())
		return;
	chiaki_session_set_login_pin(&impl_->session,
		reinterpret_cast<const uint8_t *>(pin.c_str()), pin.size());
}

} // namespace orbislink
