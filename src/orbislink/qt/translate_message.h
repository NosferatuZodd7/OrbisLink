// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QCoreApplication>
#include <QString>

#include <string>

namespace orbislink {

// Texts born outside Qt (queue, FTP, installer, Remote Play, updates) come
// in English, marked with QT_TRANSLATE_NOOP("Messages", ...) — see
// orbislink/common/tr.h. They are translated here, when they reach the
// interface.
//
// Those carrying a value at the end ("text: value" or "text (detail)")
// are translated up to the value, which stays as it came.
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
