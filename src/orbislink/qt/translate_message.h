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
inline QString translateMessage(const std::string &message)
{
	const QString original = QString::fromStdString(message);
	const QString whole = QCoreApplication::translate("Messages", message.c_str());
	if(whole != original)
		return whole;
	for(size_t pos = 0; pos < message.size(); ++pos)
	{
		const bool separator = message.compare(pos, 2, ": ") == 0 || message.compare(pos, 2, " (") == 0;
		if(!separator)
			continue;
		const std::string start = message.substr(0, pos);
		const QString translated = QCoreApplication::translate("Messages", start.c_str());
		if(translated != QString::fromStdString(start))
			return translated + QString::fromStdString(message.substr(pos));
	}
	return original;
}

} // namespace orbislink
