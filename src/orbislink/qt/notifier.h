// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

namespace orbislink {

// Avisos do sistema: a instalação acabou, a fila parou, o registo falhou —
// coisas que interessam mesmo com a janela minimizada.
//
// Não se usa o QSystemTrayIcon de propósito. Ele vive no QtWidgets e exige
// uma QApplication em vez da QGuiApplication que esta aplicação usa; trocar
// isso traria um módulo inteiro do Qt e um arranque diferente sem ganho
// que o justifique. Em vez disso:
//
//   Windows — Shell_NotifyIconW, a API de sempre da área de notificação.
//   Restantes — a janela pisca na barra de tarefas (QWindow::alert), que é
//   o que o Qt oferece sem dependências novas.
//
// Dentro da janela continua a aparecer o aviso de sempre; isto é um extra.
class Notifier : public QObject
{
	Q_OBJECT

public:
	explicit Notifier(QObject *parent = nullptr);
	~Notifier() override;

	// A janela a piscar quando não houver notificação do sistema.
	void setWindow(QWindow *window) { window_ = window; }

	// Verdadeiro quando há notificações a sério (não só o piscar).
	bool available() const;

	void show(const QString &title, const QString &message, bool error);
	void setEnabled(bool enabled) { enabled_ = enabled; }

private:
	QWindow *window_ = nullptr;
	bool enabled_ = true;
	bool registered_ = false;
};

} // namespace orbislink
