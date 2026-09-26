// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/session.h"

#include <QSet>

namespace orbislink {

// Teclado → comando do PS4.
//
// Não substitui um comando a sério, mas chega para confirmar que o stream
// responde e para navegar nos menus. O mapa segue o que o chiaki-ng usa por
// omissão, para quem vem de lá não ter de reaprender.
class KeyboardMap
{
public:
	// Devolve false se a tecla não estiver mapeada (para o evento seguir
	// o seu caminho normal, por exemplo o Esc a fechar a sessão).
	bool press(int key);
	bool release(int key);
	void clear();

	StreamSession::ControllerState state() const;
	bool empty() const { return pressed_.isEmpty(); }

private:
	QSet<int> pressed_;
};

} // namespace orbislink
