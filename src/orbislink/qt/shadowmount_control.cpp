// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/shadowmount_control.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/net/http_client.h"
#include "orbislink/net/payload_sender.h"
#include "orbislink/payloads/payload_layout.h"
#include "orbislink/qt/payloads_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace orbislink {
namespace shadowmount {

bool ask(const std::string &address, const std::string &route, const std::string &body)
{
	// Its API listens only on the PS5 itself unless "allow LAN access" is on;
	// then this goes straight to it. A short wait: most do not answer.
	HttpClient http(3000);
	const HttpResponse response = http.post("http://" + address + ":10101" + route, body);
	return response.transportOk && response.status == 200;
}

bool restart(const FtpClient::Config &config, const std::string &address, const std::atomic<bool> *cancel)
{
	// Its file on the console, where the payload managers keep it.
	FtpClient ftp(config);
	std::vector<std::string> folders = { "/data/etaHEN/payloads", "/data/etaHEN/plugins", "/data/ps5_autoloader",
		"/data/shadowmount" };
	std::vector<FtpEntry> pldmgr;
	if(ftp.list("/data/pldmgr/payloads", &pldmgr).ok)
		for(const FtpEntry &entry : pldmgr)
			if(entry.isDirectory && entry.name != "." && entry.name != "..")
				folders.push_back(entry.path);
	std::string found;
	for(const std::string &folder : folders)
	{
		std::vector<FtpEntry> entries;
		if(!ftp.list(folder, &entries).ok)
			continue;
		for(const FtpEntry &entry : entries)
			if(!entry.isDirectory && startsWith(toLower(entry.name), "shadowmount") && endsWith(toLower(entry.name), ".elf"))
				found = entry.path;
		if(!found.empty())
			break;
	}
	std::vector<uint8_t> bytes;
	if(!found.empty())
	{
		const QString local = QDir(QDir::tempPath()).filePath(
			QStringLiteral("orbislink-smp-%1.elf").arg(QCoreApplication::applicationPid()));
		if(ftp.download(found, local.toStdString()).ok)
		{
			QFile file(local);
			if(file.open(QIODevice::ReadOnly))
			{
				const QByteArray data = file.readAll();
				bytes.assign(data.constData(), data.constData() + data.size());
			}
		}
		QFile::remove(local);
	}
	QString why;
	if(bytes.empty() && !PayloadsController::fetchFromLibrary(QStringLiteral("ShadowMountPlus"), cancel, &bytes, nullptr, &why))
	{
		logWarning("Store: ShadowMountPlus could not be started again: " + why.toStdString());
		return false;
	}
	PayloadSender::Options options;
	options.listenMs = 1500;
	options.cancel = cancel;
	const PayloadSender::Result sent = PayloadSender::send(address, payloads::loaderPort(payloads::Kind::Ps5, "ShadowMountPlus.elf"), bytes, options);
	if(!sent.sent)
	{
		logWarning("Store: ShadowMountPlus could not be started again: " + sent.error);
		return false;
	}
	logInfo("Store: ShadowMountPlus started again (" + (found.empty() ? std::string("from the payload library") : found)
		+ ") to scan for the new app.");
	return true;
}

std::string rescan(const FtpClient::Config &config, const std::string &address, const std::atomic<bool> *cancel)
{
	if(ask(address, "/api/v1/scan", "{\"reset_attempts\":true}"))
		return "api";
	return restart(config, address, cancel) ? "restarted" : std::string();
}

} // namespace shadowmount
} // namespace orbislink
