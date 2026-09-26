// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace orbislink {

// Modo de transferência escolhido no DropOverlay (§5.7).
enum class TransferMode { DirectInstall, FtpUpload };

const char *transferModeName(TransferMode mode);
TransferMode transferModeFromName(const std::string &name, TransferMode fallback);

// Todas as definições do OrbisLink que não pertencem ao chiaki-ng.
// As definições de stream (resolução, fps, bitrate) continuam a ser geridas
// pelo chiaki-ng e não são duplicadas aqui.
struct Settings
{
	// Consola
	std::string consoleName = "PS4";
	std::string consoleAddress;
	uint16_t ftpPort = 2121;
	uint16_t installerPort = 12800;

	// Instalação
	TransferMode defaultMode = TransferMode::DirectInstall;
	std::string ftpUploadDirectory = "/data/pkg/";
	bool checkAlreadyInstalled = true;
	bool installAfterUpload = false;
	// Depois de enviar por FTP e instalar, apagar a cópia da consola: só
	// faz sentido com installAfterUpload ligado.
	bool deleteFromConsoleAfterInstall = false;

	// Servidor HTTP local
	uint16_t httpPort = 8765;
	std::string httpBindAddress;       // vazio = escolher pela sub-rede da consola
	bool restrictToConsoleIp = true;
	bool firewallNoticeShown = false;  // aviso da firewall do Windows (§5.3)

	// FTP
	int ftpMaxConnections = 1;
	bool ftpAdvancedMode = false;

	// Remote Play
	int streamResolution = 720;      // 360, 540, 720 ou 1080
	int streamFps = 60;              // 30 ou 60
	int streamBitrateKbps = 0;       // 0 = o que o preset do chiaki definir
	bool streamHardwareDecode = true;
	bool streamFullscreenOnConnect = false;
	bool streamRumble = true;
	bool streamTouchpadFromMouse = true;
	std::string streamAccountId;     // Account ID da PSN, em base64

	// Aplicação
	std::string theme = "escuro";    // "escuro", "vidro" ou "claro"
	std::string language = "auto";  // "auto", "pt_PT" ou "en"
	bool debugLogging = false;
	// "Updates pela internet". Ligado por omissão: com o repositório público
	// e uma compilação por push, uma app que nunca pergunta fica para trás
	// sem ninguém dar por isso. Continua desligável (§9) — desligado, a app
	// não vai à internet por causa de actualizações — e nunca instala sem
	// perguntar.
	bool checkForUpdates = true;
	// Onde procurar lançamentos, na forma "dono/nome". É uma definição e não
	// uma constante no código: uma cópia do projecto noutro repositório aponta
	// a app para si sem a recompilar.
	// Ver ORBISLINK_REPOSITORY no CMakeLists.txt: é o CI que o preenche.
	std::string updateRepository = ORBISLINK_REPOSITORY_STRING;
	// "estavel" só vê lançamentos finais; "testes" vê também as
	// pré-lançamentos que o CI publica por cada build.
	std::string updateChannel = "estavel";
	// O assistente de primeira utilização só aparece uma vez; depois disso
	// abre-se a partir das definições.
	bool firstRunDone = false;

	std::string toJson() const;
	static Settings fromJson(const std::string &text, bool *ok = nullptr);
};

// O repositório de actualizações que fica depois de ler as definições.
//
// O repositório gravado é o que estava por omissão na compilação que o
// gravou. Se o projecto mudar de repositório, isso deixaria a app nova a
// procurar versões no antigo. Por isso grava-se também qual era o valor
// por omissão ("storedDefault"): se o gravado for igual a ele, ninguém o
// escolheu e segue o desta compilação; se for diferente, foi escrito à mão
// e fica. Ficheiros sem storedDefault (nullptr) não permitem distinguir, e
// vale o desta compilação.
std::string resolveUpdateRepository(const std::string &stored, const std::string *storedDefault,
	const std::string &compiledDefault);

// Persistência em JSON na pasta de dados da aplicação.
class SettingsStore
{
public:
	explicit SettingsStore(std::string path);

	const std::string &path() const { return path_; }
	bool load(Settings *settings) const;
	bool save(const Settings &settings) const;

	// %APPDATA%/OrbisLink no Windows, ~/.config/orbislink no resto.
	static std::string defaultDirectory();
	static std::string defaultSettingsPath();
	static std::string defaultQueuePath();
	static std::string defaultLogPath();
	static bool ensureDirectory(const std::string &path);

private:
	std::string path_;
};

} // namespace orbislink
