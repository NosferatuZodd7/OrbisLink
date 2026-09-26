// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orbislink {

// Converte PCM de 16 bits intercalado entre formatos.
//
// Existe porque a consola não negoceia: manda sempre 48 kHz estéreo. A
// placa de som do PC pode não aceitar exactamente isso — muitas estão a
// 44100, outras são mono — e sem conversão ficaria tudo mudo.
//
// A reamostragem é interpolação linear. Não é um resampler de estúdio, mas
// para voz e efeitos de um jogo a diferença não se ouve, e o custo é
// desprezável ao pé de descodificar vídeo.
class PcmConverter
{
public:
	void configure(int sourceRate, int sourceChannels, int targetRate, int targetChannels);

	bool needed() const { return needed_; }
	int targetRate() const { return targetRate_; }
	int targetChannels() const { return targetChannels_; }

	// Devolve as amostras convertidas, intercaladas. `samples` é o número
	// de amostras por canal à entrada.
	const std::vector<int16_t> &convert(const int16_t *pcm, size_t samples);

	// Quantas amostras por canal saem para um dado número à entrada.
	size_t outputSamples(size_t inputSamples) const;

private:
	int sourceRate_ = 48000;
	int sourceChannels_ = 2;
	int targetRate_ = 48000;
	int targetChannels_ = 2;
	bool needed_ = false;
	// A fase guarda-se entre chamadas: sem isso ouvia-se um estalo na
	// fronteira de cada trama.
	double position_ = 0.0;
	std::vector<int16_t> out_;
};

} // namespace orbislink
