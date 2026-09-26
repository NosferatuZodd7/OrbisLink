// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/console/console_manager.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/http/local_http_server.h"
#include "orbislink/installer/rpi_client.h"
#include "orbislink/qt/ftp_model.h"
#include "orbislink/qt/queue_model.h"
#include "orbislink/queue/install_queue.h"
#include "orbislink/settings/settings_store.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace orbislink {

// Ponte entre o núcleo (threads próprias) e o QML (thread da UI).
// O núcleo não conhece Qt: é aqui que os listeners são reencaminhados para
// sinais, sempre marshalled para a thread da UI.
class AppController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString consoleName READ consoleName NOTIFY settingsChanged)
	Q_PROPERTY(QString consoleAddress READ consoleAddress NOTIFY settingsChanged)
	// As consolas guardadas: [{ name, address, active }].
	Q_PROPERTY(QVariantList consoles READ consoles NOTIFY settingsChanged)
	Q_PROPERTY(QString remotePlayState READ remotePlayState NOTIFY statusChanged)
	Q_PROPERTY(QString remotePlayHint READ remotePlayHint NOTIFY statusChanged)
	Q_PROPERTY(QString ftpState READ ftpState NOTIFY statusChanged)
	Q_PROPERTY(QString ftpHint READ ftpHint NOTIFY statusChanged)
	Q_PROPERTY(QString installerState READ installerState NOTIFY statusChanged)
	Q_PROPERTY(QString installerHint READ installerHint NOTIFY statusChanged)
	Q_PROPERTY(bool canInstallDirectly READ canInstallDirectly NOTIFY statusChanged)
	Q_PROPERTY(bool canUseFtp READ canUseFtp NOTIFY statusChanged)
	Q_PROPERTY(bool queuePaused READ queuePaused NOTIFY queueStateChanged)
	Q_PROPERTY(QString pauseReason READ pauseReason NOTIFY queueStateChanged)
	Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
	Q_PROPERTY(QString httpServerAddress READ httpServerAddress NOTIFY statusChanged)
	Q_PROPERTY(QString ftpPath READ ftpPath NOTIFY ftpPathChanged)
	Q_PROPERTY(bool ftpBusy READ ftpBusy NOTIFY ftpBusyChanged)
	Q_PROPERTY(bool downloadActive READ downloadActive NOTIFY downloadChanged)
	Q_PROPERTY(QString downloadName READ downloadName NOTIFY downloadChanged)
	Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged)
	Q_PROPERTY(QStringList ftpShortcuts READ ftpShortcuts CONSTANT)
	Q_PROPERTY(orbislink::QueueModel *queue READ queue CONSTANT)
	Q_PROPERTY(orbislink::FtpModel *files READ files CONSTANT)
	Q_PROPERTY(QString version READ version CONSTANT)
	// Verdadeiro quando a aplicação foi compilada com o Remote Play.
	Q_PROPERTY(bool streamAvailable READ streamAvailable CONSTANT)

	// Actualizações: "parado", "a-verificar", "disponivel", "a-descarregar",
	// "pronto", "sem-novidades" ou "erro".
	Q_PROPERTY(QString updateState READ updateState NOTIFY updateChanged)
	Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateChanged)
	Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateChanged)
	Q_PROPERTY(QString updateNotes READ updateNotes NOTIFY updateChanged)
	Q_PROPERTY(QString updatePageUrl READ updatePageUrl NOTIFY updateChanged)
	Q_PROPERTY(bool updateCanInstall READ updateCanInstall NOTIFY updateChanged)
	Q_PROPERTY(double updateProgress READ updateProgress NOTIFY updateChanged)

public:
	explicit AppController(QObject *parent = nullptr);
	~AppController() override;

	QString consoleName() const;
	QString consoleAddress() const;
	QVariantList consoles() const;
	QString remotePlayState() const;
	QString remotePlayHint() const;
	QString ftpState() const;
	QString ftpHint() const;
	QString installerState() const;
	QString installerHint() const;
	bool canInstallDirectly() const;
	bool canUseFtp() const;
	bool queuePaused() const;
	QString pauseReason() const;
	QString statusMessage() const { return statusMessage_; }
	QString httpServerAddress() const;
	QString ftpPath() const { return ftpPath_; }
	bool ftpBusy() const { return ftpBusy_; }
	bool downloadActive() const { return downloadActive_; }
	QString downloadName() const { return downloadName_; }
	double downloadProgress() const { return downloadProgress_; }
	QStringList ftpShortcuts() const;
	QueueModel *queue() { return &queueModel_; }
	FtpModel *files() { return &ftpModel_; }
	QString version() const;
	// As definições em vigor, para quem precisa delas inteiras (o
	// controlador do Remote Play).
	const Settings &settings() const { return settings_; }
	// Guarda o Account ID da PSN, para não ser preciso escrevê-lo de cada vez.
	Q_INVOKABLE void rememberAccountId(const QString &accountId);
	bool streamAvailable() const
	{
#ifdef ORBISLINK_HAS_STREAM
		return true;
#else
		return false;
#endif
	}

	// Largar ficheiros: mode 0 = instalação direta, 1 = envio por FTP.
	Q_INVOKABLE void dropUrls(const QList<QUrl> &urls, int mode);
	Q_INVOKABLE void addPaths(const QStringList &paths, int mode);
	Q_INVOKABLE void checkServicesNow();
	// O estado do Remote Play vem do chiaki (StreamController), não da
	// verificação periódica: é por aqui que entra no indicador da barra.
	Q_INVOKABLE void reportRemotePlayState(const QString &state, const QString &detail);
	// O QML avisa aqui cada vez que um arrasto entra, sai ou é largado.
	//
	// Sem contadores não se distingue o Windows a não entregar o evento
	// (aplicação elevada, por exemplo) de nós a recusá-lo. Com eles, zero
	// arrastos vistos é uma resposta, e não um palpite.
	Q_INVOKABLE void noteDrag(const QString &evento, bool comFicheiros);
	QString dragSummary() const;
	// O diagnóstico precisa de perguntar ao Remote Play como está o caminho
	// do som, mas o AppController não conhece o StreamController (nem sempre
	// há um). Fica só com a pergunta, não com quem a responde.
	void setAudioProbe(std::function<QString()> probe);
	QString audioProbe() const;
	void setVideoProbe(std::function<QString()> probe);
	QString videoProbe() const;
	Q_INVOKABLE void cancelTask(const QString &id);
	Q_INVOKABLE void retryTask(const QString &id);
	Q_INVOKABLE void removeTask(const QString &id);
	Q_INVOKABLE void moveTaskUp(const QString &id);
	Q_INVOKABLE void moveTaskDown(const QString &id);
	Q_INVOKABLE void pauseQueue();
	Q_INVOKABLE void resumeQueue();

	Q_INVOKABLE void ftpNavigate(const QString &path);
	Q_INVOKABLE void ftpRefresh();
	Q_INVOKABLE void ftpUp();
	Q_INVOKABLE void ftpDelete(const QString &path, bool isDirectory);
	Q_INVOKABLE void ftpMakeDirectory(const QString &name);
	Q_INVOKABLE void ftpRename(const QString &path, const QString &newName);

	// Trazer da consola para o PC (§5.5). `destination` vazio = pasta por
	// omissão (ambiente de trabalho).
	Q_INVOKABLE void ftpDownload(const QString &remotePath, const QString &name,
		const QString &destinationDir);
	// Igual, mas para a cache local que alimenta o arrastar para fora.
	Q_INVOKABLE void ftpPrepareForDrag(const QString &remotePath, const QString &name, qint64 size);
	// URL local do ficheiro se já estiver na cache com o tamanho certo, senão vazio.
	Q_INVOKABLE QString cachedFileUrl(const QString &remotePath, qint64 size) const;
	Q_INVOKABLE void cancelDownload();
	Q_INVOKABLE QString defaultDownloadDirectory() const;
	Q_INVOKABLE void openLocalFolder(const QString &path) const;
	Q_INVOKABLE void copyToClipboard(const QString &text) const;
	Q_INVOKABLE void setFtpUploadDirectory(const QString &path);

	// Verificação pontual de um endereço ainda não guardado: as definições
	// chamam isto enquanto se escreve o IP, sem mexer nas definições em vigor.
	Q_INVOKABLE void probeConsole(const QString &address, int ftpPort, int installerPort);

	// Diagnóstico: o registo ao vivo na janela, e o relatório num ficheiro
	// que se pode anexar a uma mensagem.
	Q_INVOKABLE QStringList recentLog(int lines = 300) const;
	Q_INVOKABLE QString diagnosticsReport() const;
	Q_INVOKABLE QString exportDiagnostics(const QString &directory = QString());
	Q_INVOKABLE void copyDiagnosticsToClipboard();
	Q_INVOKABLE QString logFilePath() const;
	Q_INVOKABLE void setStreamVerbose(bool verbose);
	Q_INVOKABLE bool streamVerbose() const { return streamVerbose_; }

	// Actualizações. A verificação e a descarga acontecem fora do fio da
	// interface; o estado chega por updateChanged().
	Q_INVOKABLE void checkForUpdatesNow(bool silentWhenUpToDate = false);
	// Descarrega o instalador, confirma o SHA-256 quando há um publicado, e
	// corre-o. Em sistemas sem instalador publicado, abre a página.
	Q_INVOKABLE void installUpdate();
	Q_INVOKABLE void openUpdatePage() const;
	Q_INVOKABLE void dismissUpdate();
	// Só para as capturas de ecrã (--demo-update): preenche os campos a
	// partir de uma resposta de exemplo, pelo mesmo caminho de código que a
	// resposta verdadeira segue.
	Q_INVOKABLE void loadDemoUpdate();
	QString updateState() const { return updateState_; }
	QString updateMessage() const { return updateMessage_; }
	QString updateVersion() const { return updateVersion_; }
	QString updateNotes() const { return updateNotes_; }
	QString updatePageUrl() const { return updatePageUrl_; }
	bool updateCanInstall() const { return !updateAssetUrl_.isEmpty(); }
	double updateProgress() const { return updateProgress_; }

	Q_INVOKABLE QVariantMap settingsMap() const;
	Q_INVOKABLE void applySettings(const QVariantMap &values);
	// Passa a usar a consola com este endereço (tem de estar na lista).
	Q_INVOKABLE void selectConsole(const QString &address);
	// Junta uma consola à lista e passa a usá-la. Se o endereço já lá
	// estiver, só a escolhe.
	// `type` é "ps4", "ps5" ou vazio (desconhecido).
	Q_INVOKABLE void addConsole(const QString &name, const QString &address,
		const QString &type = QString());
	// Lembra o tipo de uma consola que respondeu, para o mostrar mesmo quando
	// ela estiver desligada. Só grava se mudou.
	Q_INVOKABLE void rememberConsoleType(const QString &address, bool ps5);
	// Tira da lista uma consola que não esteja em uso.
	Q_INVOKABLE void removeConsole(const QString &address);
	// Grava só o tema. O applySettings reconstrói os serviços todos (fila,
	// servidor HTTP, gestor da consola) — mudar de tema a meio de uma
	// instalação pararia a transferência.
	Q_INVOKABLE void setTheme(const QString &theme);
	// Grava só o que a verificação de actualizações precisa, pela mesma
	// razão: carregar em "Verificar agora" não pode mexer em mais nada.
	// Grava as teclas do teclado como comando (acção → tecla), sem
	// reconstruir os serviços.
	void saveKeyBindings(const std::map<std::string, int> &bindings);
	Q_INVOKABLE void saveUpdateSettings(bool checkForUpdates, const QString &repository,
		const QString &channel);

signals:
	void settingsChanged();
	// Uma linha nova no registo, já mascarada. A janela de diagnóstico
	// liga-se a isto para se ver o que acontece em tempo real.
	void logLine(const QString &level, const QString &text);
	void statusChanged();
	void queueStateChanged();
	void statusMessageChanged();
	void ftpPathChanged();
	void ftpBusyChanged();
	void notify(const QString &title, const QString &message, bool error);
	void consoleProbed(const QString &address, bool ftpOk, bool installerOk,
		const QString &detail);
	void downloadChanged();
	// Emitido quando um ficheiro fica pronto na cache local: o FtpBrowser
	// passa então a poder arrastá-lo para fora da janela.
	void dragFileReady(const QString &remotePath, const QString &localUrl);
	void updateChanged();
	// Emitido quando há uma versão nova e a verificação não foi silenciosa:
	// a janela abre o diálogo a partir daqui.
	void updateAvailable(const QString &version);

private:
	void rebuildBackends();
	// Diz porque é que um clique não fez nada, em vez de o engolir em
	// silêncio. Devolve false quando a operação não pode seguir.
	bool ftpReady(const QString &operacao);
	void refreshQueueModel();
	void setStatusMessage(const QString &message);
	void setFtpBusy(bool busy);
	static QStringList collectPkgFiles(const QStringList &paths);
	void registerIcons(const QStringList &paths, const QStringList &taskIds);

	Settings settings_;
	SettingsStore store_;
	ConsoleStatus status_;

	std::unique_ptr<ConsoleManager> console_;
	std::unique_ptr<LocalHttpServer> httpServer_;
	std::unique_ptr<RpiClient> installer_;
	std::unique_ptr<FtpClient> ftp_;
	std::unique_ptr<InstallQueue> queue_;

	QueueModel queueModel_;
	FtpModel ftpModel_;
	QString statusMessage_;
	QString ftpPath_ = QStringLiteral("/data/pkg/");
	bool ftpBusy_ = false;
	// Só a verificação mais recente interessa: as anteriores são descartadas
	// quando chegam, para o resultado nunca contradizer o que está escrito.
	std::atomic<uint64_t> probeGeneration_ { 0 };
	bool streamVerbose_ = false;
	int dragsVistos_ = 0;
	int dragsLargados_ = 0;
	int dragsRecusados_ = 0;
	std::function<QString()> audioProbe_;
	std::function<QString()> videoProbe_;
	// O último estado do Remote Play que o StreamController reportou, para
	// o repor depois de os serviços serem reconstruídos.
	QString lastRemotePlayState_;
	QString lastRemotePlayDetail_;

	void startDownload(const QString &remotePath, const QString &name, const QString &localPath,
		bool forDrag);
	static QString dragCacheDirectory();
	static QString cachePathFor(const QString &remotePath);
	static QString uniqueLocalPath(const QString &wanted);

	bool downloadActive_ = false;
	QString downloadName_;
	double downloadProgress_ = 0.0;
	std::atomic<bool> downloadCancel_ { false };

	void setUpdateState(const QString &state, const QString &message);
	QString updateState_ = QStringLiteral("parado");
	QString updateMessage_;
	QString updateVersion_;
	QString updateNotes_;
	QString updatePageUrl_;
	QString updateAssetUrl_;
	QString updateAssetName_;
	QString updateAssetSha256_;
	QString updateAssetSha256Url_;
	int64_t updateAssetSize_ = 0;
	double updateProgress_ = 0.0;
	std::atomic<bool> updateBusy_ { false };
};

} // namespace orbislink
