// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QCoreApplication>
#include <QString>

#include <string>

namespace orbislink {

// Os textos que nascem fora do Qt (fila, FTP, instalador, Remote Play,
// actualizações) vêm em português, marcados com
// QT_TRANSLATE_NOOP("Messages", ...) — ver orbislink/common/tr.h. Traduzem-se
// aqui, ao chegar à interface.
//
// Os que levam um valor no fim ("texto: valor" ou "texto (detalhe)")
// traduzem-se até ao valor, que fica como veio.
inline QString translateMessage(const std::string &texto)
{
	const QString original = QString::fromStdString(texto);
	const QString inteiro = QCoreApplication::translate("Messages", texto.c_str());
	if(inteiro != original)
		return inteiro;
	for(size_t pos = 0; pos < texto.size(); ++pos)
	{
		const bool separador = texto.compare(pos, 2, ": ") == 0 || texto.compare(pos, 2, " (") == 0;
		if(!separador)
			continue;
		const std::string inicio = texto.substr(0, pos);
		const QString traduzido = QCoreApplication::translate("Messages", inicio.c_str());
		if(traduzido != QString::fromStdString(inicio))
			return traduzido + QString::fromStdString(texto.substr(pos));
	}
	return original;
}

} // namespace orbislink
