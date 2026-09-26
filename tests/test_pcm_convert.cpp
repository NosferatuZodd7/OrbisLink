// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/pcm_convert.h"
#include "test_support.h"

#include <cmath>
#include <vector>

using namespace orbislink;

namespace {

// Uma onda sinusoidal, que é o que se consegue verificar sem ouvidos.
std::vector<int16_t> seno(int samples, int channels, double hz, int rate)
{
	std::vector<int16_t> out(static_cast<size_t>(samples) * channels);
	for(int i = 0; i < samples; ++i)
	{
		const double v = std::sin(2.0 * 3.14159265358979 * hz * i / rate) * 20000.0;
		for(int c = 0; c < channels; ++c)
			out[static_cast<size_t>(i) * channels + c] = static_cast<int16_t>(v);
	}
	return out;
}

} // namespace

ORBISLINK_TEST(formato_igual_nao_mexe_em_nada)
{
	PcmConverter conv;
	conv.configure(48000, 2, 48000, 2);
	CHECK(!conv.needed());
	CHECK_EQ(conv.outputSamples(480), static_cast<size_t>(480));
}

ORBISLINK_TEST(de_48k_para_44k_encolhe_na_proporcao_certa)
{
	PcmConverter conv;
	conv.configure(48000, 2, 44100, 2);
	CHECK(conv.needed());

	const auto entrada = seno(480, 2, 440.0, 48000);
	const auto &saida = conv.convert(entrada.data(), 480);
	// 480 amostras a 48 kHz são 10 ms; a 44100 são 441. Aceita-se uma de
	// diferença, que é a que fica para a trama seguinte.
	const size_t porCanal = saida.size() / 2;
	CHECK(porCanal >= 439 && porCanal <= 442);
}

ORBISLINK_TEST(a_fase_continua_entre_tramas)
{
	// Sem guardar a posição entre chamadas ouve-se um estalo em cada
	// fronteira, e ao fim de cem tramas o desvio já é audível.
	PcmConverter conv;
	conv.configure(48000, 2, 44100, 2);
	const auto entrada = seno(480, 2, 440.0, 48000);

	size_t total = 0;
	for(int i = 0; i < 100; ++i)
		total += conv.convert(entrada.data(), 480).size() / 2;

	// 100 tramas de 10 ms = 1 segundo, portanto ~44100 amostras. Com uma
	// reamostragem que perdesse a fase, isto afastava-se depressa.
	CHECK(total >= 44050 && total <= 44150);
}

ORBISLINK_TEST(mono_para_estereo_duplica_o_canal)
{
	PcmConverter conv;
	conv.configure(48000, 1, 48000, 2);
	CHECK(conv.needed());

	const auto entrada = seno(64, 1, 440.0, 48000);
	const auto &saida = conv.convert(entrada.data(), 64);
	CHECK(saida.size() >= 2);
	// Os dois canais saem iguais: um lado em silêncio seria pior que mono.
	for(size_t i = 0; i + 1 < saida.size(); i += 2)
		CHECK_EQ(saida[i], saida[i + 1]);
}

ORBISLINK_TEST(estereo_para_mono_nao_perde_metade_do_som)
{
	PcmConverter conv;
	conv.configure(48000, 2, 48000, 1);

	// Canal esquerdo com sinal, direito em silêncio. A média tem de dar
	// metade — e não zero, que é o que daria ignorar um dos canais.
	std::vector<int16_t> entrada(64 * 2, 0);
	for(int i = 0; i < 64; ++i)
		entrada[static_cast<size_t>(i) * 2] = 10000;

	const auto &saida = conv.convert(entrada.data(), 64);
	CHECK(!saida.empty());
	CHECK(saida[0] > 4000 && saida[0] < 6000);
}

ORBISLINK_TEST(nao_estoira_com_entradas_degeneradas)
{
	PcmConverter conv;
	conv.configure(48000, 2, 44100, 2);
	CHECK(conv.convert(nullptr, 100).empty());
	const auto entrada = seno(1, 2, 440.0, 48000);
	// Uma amostra só não dá para interpolar: não deve ler fora do buffer.
	conv.convert(entrada.data(), 1);
	// Um formato absurdo não deve dividir por zero.
	conv.configure(0, 0, 0, 0);
	CHECK(conv.targetRate() > 0);
	CHECK(conv.targetChannels() > 0);
}

TEST_MAIN()
