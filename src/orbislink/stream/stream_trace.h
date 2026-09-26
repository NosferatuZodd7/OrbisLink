// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

// Diário de uma tentativa de Remote Play.
//
// Isto existe por uma razão concreta: o Remote Play nunca correu contra uma
// consola verdadeira. Quando falhar — e há-de falhar — o que faz falta não
// é "não deu", é saber em que passo parou, quanto tempo demorou até lá e o
// que a consola respondeu.
//
// Cada passo fica registado com o instante em que começou e acabou. No fim,
// summary() dá um texto que se pode colar numa mensagem.
class StreamTrace
{
public:
	enum class Result { Running, Ok, Failed, Skipped };

	struct Step
	{
		std::string name;
		std::string detail;
		Result result = Result::Running;
		int64_t startedMs = 0;
		int64_t endedMs = 0;
		int64_t durationMs() const { return endedMs > startedMs ? endedMs - startedMs : 0; }
	};

	static StreamTrace &instance();

	// Começa uma tentativa nova. Apaga a anterior.
	void begin(const std::string &address);
	// Guarda o endereço se ainda não houver nenhum. A descoberta periódica
	// usa isto: não abre uma tentativa, mas o relatório fica a saber a quem
	// se estava a perguntar.
	void addressIfUnset(const std::string &address);
	// Abre um passo. Fechar o anterior é automático, com sucesso.
	void step(const std::string &name, const std::string &detail = std::string());
	// Fecha o passo aberto.
	void ok(const std::string &detail = std::string());
	void fail(const std::string &detail);
	void skip(const std::string &name, const std::string &reason);
	// Nota solta, sem abrir passo (ex.: primeiro fotograma, mudança de estado).
	void note(const std::string &text);
	void end();

	std::vector<Step> steps() const;
	std::vector<std::string> notes() const;
	std::string summary() const;
	bool active() const;

private:
	StreamTrace() = default;
};

// Fecha o passo com sucesso ao sair do âmbito, a não ser que já tenha sido
// fechado — para não ficarem passos abertos quando se sai por um return.
class StreamStep
{
public:
	StreamStep(const std::string &name, const std::string &detail = std::string());
	~StreamStep();
	void ok(const std::string &detail = std::string());
	void fail(const std::string &detail);

private:
	bool closed_ = false;
};

} // namespace orbislink
