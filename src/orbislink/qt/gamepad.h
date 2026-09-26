// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/stream/session.h"

#include <QObject>
#include <QString>
#include <QTimer>

namespace orbislink {

// Comando físico (DualShock 4, DualSense, ou qualquer outro que o SDL
// reconheça) traduzido para o estado que a consola espera.
//
// O SDL é opcional: sem ele a aplicação compila e funciona, só que o
// comando não entra e o teclado fica a ser a única forma de jogar.
class Gamepad : public QObject
{
	Q_OBJECT

public:
	explicit Gamepad(QObject *parent = nullptr);
	~Gamepad() override;

	// Verdadeiro quando a aplicação foi compilada com suporte a comandos.
	static bool supported();

	// Começa e pára de ler o comando. Só faz sentido durante a sessão.
	void start();
	void stop();

	// Nome do comando ligado, vazio se não houver nenhum.
	QString name() const { return name_; }

	// Vibração pedida pela consola. 0-255 em cada motor; a duração é curta
	// e renovada a cada pedido, para parar sozinha se a consola se calar.
	void rumble(quint8 left, quint8 right);
	void setRumbleEnabled(bool enabled) { rumbleEnabled_ = enabled; }

signals:
	// Emitido só quando alguma coisa muda.
	void stateChanged(const StreamSession::ControllerState &state);
	void connectedChanged(const QString &name);

private:
	void poll();

	QTimer timer_;
	QString name_;
	void *controller_ = nullptr; // SDL_GameController*, sem arrastar o SDL para aqui
	StreamSession::ControllerState last_;
	bool haveLast_ = false;
	bool initialised_ = false;
	bool rumbleEnabled_ = true;
};

} // namespace orbislink
