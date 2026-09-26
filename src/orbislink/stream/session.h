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

// Uma sessão de Remote Play. Corre nas threads do chiaki; os avisos saem
// por callbacks que podem vir de qualquer thread.
class StreamSession
{
public:
	struct Config
	{
		std::string address;
		StreamCredentials credentials;
		SessionSettings settings;
	};

	// Um fotograma descodificado. O AVFrame pertence ao descodificador e só
	// é válido durante a chamada — quem o quiser guardar tem de o copiar ou
	// referenciar (av_frame_ref).
	using FrameCallback = std::function<void(AVFrame *frame)>;
	using StateCallback = std::function<void(SessionState state, const std::string &detail)>;
	// A consola pede o PIN de início de sessão (quando o utilizador tem PIN
	// definido na conta). Responder com setLoginPin().
	using LoginPinCallback = std::function<void(bool incorrect)>;
	// A consola pede vibração. 0-255 em cada motor.
	using RumbleCallback = std::function<void(uint8_t left, uint8_t right)>;
	// Áudio já descodificado: PCM 16 bits intercalado. `samples` é o número
	// de amostras por canal.
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

	// Estado do comando enviado à consola. Os botões são a máscara do
	// chiaki (ChiakiControllerButton); os eixos vão de -32768 a 32767 e os
	// gatilhos de 0 a 255.
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
	// Só envia quando alguma coisa muda: a consola não precisa de repetição.
	void sendController(const ControllerState &state);

	// Touchpad. As coordenadas são as do touchpad do comando: 1920x942.
	// Devolve o id do toque, ou -1 se não houver espaço.
	int startTouch(uint16_t x, uint16_t y);
	void moveTouch(int id, uint16_t x, uint16_t y);
	void stopTouch(int id);
	static constexpr uint16_t kTouchpadWidth = 1920;
	static constexpr uint16_t kTouchpadHeight = 942;

	// ── Microfone (o som deste PC para a consola)
	//
	// Desligado por omissão e nunca ligado sozinho. Uma aplicação que abre
	// o microfone sem se ver é outra coisa que não um Remote Play, por isso
	// isto só arranca a pedido e a interface tem de o mostrar enquanto
	// estiver a captar.
	//
	// O formato não é escolha nossa: é o que a consola espera e o que o
	// chiaki codifica — 48 kHz, 2 canais, 16 bits, 480 amostras por trama
	// (10 ms). Ver chiaki_audio_header_set() em streamsession.cpp do
	// chiaki-ng.
	static constexpr unsigned int kMicrophoneRate = 48000;
	static constexpr unsigned int kMicrophoneChannels = 2;
	static constexpr unsigned int kMicrophoneFrameSamples = 480;

	// Avisa a consola de que vamos falar e prepara o codificador.
	bool startMicrophone(std::string *error = nullptr);
	void stopMicrophone();
	// Continua ligado, mas deixa de enviar. É o que a consola espera de um
	// "mute" — desligar a sério obrigaria a nova ligação.
	void setMicrophoneMuted(bool muted);
	bool microphoneActive() const;
	bool microphoneMuted() const;
	// Uma trama de PCM intercalado, com exactamente kMicrophoneFrameSamples
	// amostras por canal. Chamado pela captura, fora da thread do chiaki.
	void sendMicrophoneFrame(const int16_t *pcm, size_t samplesPerChannel);

	// Dimensões do último fotograma recebido (0 antes do primeiro).
	int frameWidth() const;
	int frameHeight() const;
	// Fotogramas entregues desde o arranque — serve para a UI saber que há
	// imagem mesmo antes de a desenhar.
	uint64_t framesDecoded() const;
	// Verdadeiro quando o vídeo está a ser descodificado pela placa gráfica.
	bool usingHardwareDecoder() const;

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace orbislink
