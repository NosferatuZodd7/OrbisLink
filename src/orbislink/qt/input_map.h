// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/session.h"

#include <QHash>
#include <QSet>

#include <map>
#include <string>
#include <vector>

namespace orbislink {

// Teclado → comando do PS4.
//
// Não substitui um comando a sério, mas chega para confirmar que o stream
// responde e para navegar nos menus. O mapa por omissão é o do chiaki-ng,
// para quem vem de lá não ter de reaprender; cada acção pode ser mudada para
// outra tecla, e o que mudou fica nas definições.
class KeyboardMap
{
public:
	// Uma acção do comando: um botão (button != 0) ou metade de um eixo.
	struct Action
	{
		const char *id;   // o nome que fica nas definições, ex.: "cross"
		uint32_t button;  // CHIAKI_CONTROLLER_BUTTON_*, ou 0 se for um eixo
		int defaultKey;   // Qt::Key
	};
	static const std::vector<Action> &actions();

	// acção → tecla. As que não vierem ficam com a tecla por omissão.
	using Bindings = std::map<std::string, int>;
	static Bindings defaults();

	// Teclas que não se podem dar a uma acção: o Esc sai do stream e o F11
	// muda o ecrã inteiro.
	static bool reserved(int key);

	// Dá `key` à acção `action`. Se a tecla já era de outra acção, as duas
	// trocam — nenhuma acção fica sem tecla. Devolve false (sem mexer em
	// nada) para uma acção desconhecida ou uma tecla reservada.
	static bool rebind(Bindings &bindings, const std::string &action, int key);

	KeyboardMap();
	void setBindings(const Bindings &custom);
	const Bindings &bindings() const { return bindings_; }

	// Devolve false se a tecla não estiver mapeada (para o evento seguir
	// o seu caminho normal, por exemplo o Esc a fechar a sessão).
	bool press(int key);
	bool release(int key);
	void clear();

	StreamSession::ControllerState state() const;
	bool empty() const { return pressed_.isEmpty(); }

private:
	Bindings bindings_;
	QHash<int, std::string> byKey_;
	QSet<int> pressed_;
};

} // namespace orbislink
