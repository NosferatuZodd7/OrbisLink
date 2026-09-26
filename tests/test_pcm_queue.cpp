// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A fila entre o descodificador e a placa de som. O que se testa aqui é a
// contabilidade: quanto entrou, quanto saiu, e quanto ficou pelo caminho.
// É isso que o diagnóstico usa para dizer em que troço é que o som morre,
// e um contador errado manda-nos procurar avaria no sítio errado.
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
	// Ao contrário do modo em que a placa puxa, aqui não se enche o resto
	// com silêncio: quem escreve é a aplicação, e escrever silêncio que não
	// existe só serve para encher o buffer da placa com nada.
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

	// No modo em que a placa puxa, devolver menos do que ela pediu põe o
	// QAudioSink a dormir. Por isso o resto vai a zeros.
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

	// Atrasar o som é pior do que lhe dar um salto: fica o mais recente.
	CHECK_EQ(fila.queuedBytes(), 100);
	std::vector<char> destino(100, 0);
	CHECK_EQ(fila.take(destino.data(), 100), 100);
	CHECK_EQ(destino[0], 'v');
	CHECK_EQ(destino[99], 'n');
	// O contador de entrada conta tudo o que chegou, mesmo o que se deitou
	// fora: é assim que se vê que o descodificador estava a entregar.
	CHECK_EQ(fila.pushedBytes(), 160);
}

ORBISLINK_TEST(cada_push_avisa_que_chegou_alguma_coisa)
{
	PcmQueue fila;
	CHECK(fila.open(QIODevice::ReadOnly));
	int avisos = 0;
	QObject::connect(&fila, &QIODevice::readyRead, &fila, [&avisos]() { ++avisos; });

	// Sem este aviso há backends de áudio do Qt que nunca chegam a puxar
	// uma única amostra, e o stream fica mudo sem erro nenhum.
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
