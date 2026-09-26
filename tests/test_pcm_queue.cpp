// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The queue between the decoder and the sound card. What is tested here is
// the accounting: how much went in, how much came out, and how much was lost
// on the way. That is what the diagnostics use to say at which stretch the
// sound dies, and a wrong counter sends us looking for the fault in the wrong place.
#include "orbislink/qt/audio_output.h"
#include "test_support.h"

#include <vector>

using namespace orbislink;

namespace {

std::vector<char> bloco(int bytes, char valor)
{
	return std::vector<char>(static_cast<size_t>(bytes), valor);
}

} // namespace

ORBISLINK_TEST(fila_conta_o_que_entra_e_o_que_sai)
{
	PcmQueue fila;
	CHECK(fila.open(QIODevice::ReadOnly));
	CHECK_EQ(fila.pushedBytes(), 0);
	CHECK_EQ(fila.pulledBytes(), 0);

	const std::vector<char> dados = bloco(1000, 'a');
	fila.push(dados.data(), static_cast<qint64>(dados.size()));
	CHECK_EQ(fila.pushedBytes(), 1000);
	CHECK_EQ(fila.queuedBytes(), 1000);
	CHECK_EQ(fila.pulledBytes(), 0);

	std::vector<char> destino(400, 0);
	const qint64 lidos = fila.take(destino.data(), 400);
	CHECK_EQ(lidos, 400);
	CHECK_EQ(destino[0], 'a');
	CHECK_EQ(fila.pulledBytes(), 400);
	CHECK_EQ(fila.queuedBytes(), 600);
}

ORBISLINK_TEST(take_nao_inventa_bytes_que_nao_tem)
{
	PcmQueue fila;
	CHECK(fila.open(QIODevice::ReadOnly));
	const std::vector<char> dados = bloco(64, 'b');
	fila.push(dados.data(), 64);

	std::vector<char> destino(500, 0x7f);
	// Unlike the mode where the card pulls, here the rest is not padded with
	// silence: the application does the writing, and writing silence that
	// does not exist only fills the card's buffer with nothing.
	CHECK_EQ(fila.take(destino.data(), 500), 64);
	CHECK_EQ(destino[64], 0x7f);
	CHECK_EQ(fila.take(destino.data(), 500), 0);
}

ORBISLINK_TEST(leitura_enche_o_resto_com_silencio)
{
	PcmQueue fila;
	CHECK(fila.open(QIODevice::ReadOnly));
	const std::vector<char> dados = bloco(10, 'c');
	fila.push(dados.data(), 10);

	// In the mode where the card pulls, returning less than it asked for puts
	// QAudioSink to sleep. So the rest is zero-filled.
	std::vector<char> destino(100, 0x5a);
	CHECK_EQ(fila.read(destino.data(), 100), 100);
	CHECK_EQ(destino[9], 'c');
	CHECK_EQ(destino[10], 0);
	CHECK_EQ(fila.pulledBytes(), 10);
}

ORBISLINK_TEST(fila_deita_fora_o_mais_antigo_quando_enche)
{
	PcmQueue fila;
	CHECK(fila.open(QIODevice::ReadOnly));
	fila.setLimit(100);

	const std::vector<char> velho = bloco(80, 'v');
	const std::vector<char> novo = bloco(80, 'n');
	fila.push(velho.data(), 80);
	fila.push(novo.data(), 80);

	// Delaying the sound is worse than making it skip: the most recent stays.
	CHECK_EQ(fila.queuedBytes(), 100);
	std::vector<char> destino(100, 0);
	CHECK_EQ(fila.take(destino.data(), 100), 100);
	CHECK_EQ(destino[0], 'v');
	CHECK_EQ(destino[99], 'n');
	// The input counter counts everything that arrived, even what was thrown
	// away: that is how you see the decoder was delivering.
	CHECK_EQ(fila.pushedBytes(), 160);
}

ORBISLINK_TEST(cada_push_avisa_que_chegou_alguma_coisa)
{
	PcmQueue fila;
	CHECK(fila.open(QIODevice::ReadOnly));
	int avisos = 0;
	QObject::connect(&fila, &QIODevice::readyRead, &fila, [&avisos]() { ++avisos; });

	// Without this signal some Qt audio backends never pull a single
	// sample, and the stream stays silent with no error at all.
	const std::vector<char> dados = bloco(16, 'd');
	fila.push(dados.data(), 16);
	fila.push(dados.data(), 16);
	CHECK_EQ(avisos, 2);
}

ORBISLINK_TEST(limpar_repoe_os_contadores)
{
	PcmQueue fila;
	CHECK(fila.open(QIODevice::ReadOnly));
	const std::vector<char> dados = bloco(32, 'e');
	fila.push(dados.data(), 32);
	std::vector<char> destino(32, 0);
	fila.take(destino.data(), 32);

	fila.clear();
	CHECK_EQ(fila.pushedBytes(), 0);
	CHECK_EQ(fila.pulledBytes(), 0);
	CHECK_EQ(fila.queuedBytes(), 0);
}

TEST_MAIN()
