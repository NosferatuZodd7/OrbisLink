// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

namespace orbislink {

class AppController;

// Junta num só ficheiro tudo o que é preciso para perceber uma avaria
// sem estar à frente da máquina: versões, ambiente, definições (sem os
// segredos), estado dos serviços, o diário da última tentativa de Remote
// Play e o fim do registo.
//
// O objectivo é que baste anexar este ficheiro a uma mensagem.
class Diagnostics
{
public:
	// O relatório em texto. Nunca inclui chaves de registo, rp_key nem o
	// Account ID — o Logger já os mascara, e o que é lido das definições é
	// filtrado aqui outra vez.
	static QString report(AppController *app);

	// Escreve o relatório num ficheiro. Devolve o caminho, ou vazio se
	// falhar. `directory` vazio = ambiente de trabalho.
	static QString write(AppController *app, const QString &directory, QString *error);

	// Nome sugerido, com data e hora para não se sobrepor ao anterior.
	static QString suggestedFileName();
};

} // namespace orbislink
