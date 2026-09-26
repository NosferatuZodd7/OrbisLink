// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/pcm_convert.h"

#include <algorithm>
#include <cmath>

namespace orbislink {

namespace {

// Lê um canal de uma amostra da origem, tratando mono e estéreo.
inline int16_t sampleAt(const int16_t *pcm, size_t frame, int channels, int channel)
{
	if(channels <= 1)
		return pcm[frame];
	return pcm[frame * static_cast<size_t>(channels) + static_cast<size_t>(channel)];
}

inline int16_t clamp16(double value)
{
	if(value > 32767.0)
		return 32767;
	if(value < -32768.0)
		return -32768;
	return static_cast<int16_t>(std::lround(value));
}

} // namespace

void PcmConverter::configure(int sourceRate, int sourceChannels, int targetRate,
	int targetChannels)
{
	sourceRate_ = sourceRate > 0 ? sourceRate : 48000;
	sourceChannels_ = sourceChannels > 0 ? sourceChannels : 2;
	targetRate_ = targetRate > 0 ? targetRate : sourceRate_;
	targetChannels_ = targetChannels > 0 ? targetChannels : sourceChannels_;
	needed_ = sourceRate_ != targetRate_ || sourceChannels_ != targetChannels_;
	position_ = 0.0;
	out_.clear();
}

size_t PcmConverter::outputSamples(size_t inputSamples) const
{
	if(!needed_ || sourceRate_ == targetRate_)
		return inputSamples;
	return static_cast<size_t>(static_cast<double>(inputSamples) * targetRate_ / sourceRate_);
}

const std::vector<int16_t> &PcmConverter::convert(const int16_t *pcm, size_t samples)
{
	out_.clear();
	if(!pcm || samples == 0)
		return out_;

	const double passo = static_cast<double>(sourceRate_) / static_cast<double>(targetRate_);
	// Enquanto houver um par de amostras de origem para interpolar. A
	// última amostra da trama fica para a seguinte, através do position_.
	out_.reserve(outputSamples(samples) * static_cast<size_t>(targetChannels_) + 8);

	while(position_ < static_cast<double>(samples) - 1.0)
	{
		const size_t indice = static_cast<size_t>(position_);
		const double fraccao = position_ - static_cast<double>(indice);

		for(int canal = 0; canal < targetChannels_; ++canal)
		{
			// Mais canais à saída do que à entrada: repete-se o último
			// (mono para estéreo dá o mesmo som dos dois lados).
			const int canalOrigem = std::min(canal, sourceChannels_ - 1);
			const double a = sampleAt(pcm, indice, sourceChannels_, canalOrigem);
			const double b = sampleAt(pcm, indice + 1, sourceChannels_, canalOrigem);
			double valor = a + (b - a) * fraccao;

			// Estéreo para mono: soma-se e divide-se, senão perdia-se
			// metade do som.
			if(targetChannels_ == 1 && sourceChannels_ > 1)
			{
				double soma = 0.0;
				for(int c = 0; c < sourceChannels_; ++c)
				{
					const double x = sampleAt(pcm, indice, sourceChannels_, c);
					const double y = sampleAt(pcm, indice + 1, sourceChannels_, c);
					soma += x + (y - x) * fraccao;
				}
				valor = soma / sourceChannels_;
			}
			out_.push_back(clamp16(valor));
		}
		position_ += passo;
	}

	// O que sobrou desta trama conta para a seguinte.
	position_ -= static_cast<double>(samples);
	if(position_ < 0.0)
		position_ = 0.0;
	return out_;
}

} // namespace orbislink
