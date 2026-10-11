// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/installer/rpi_client.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"
#include "orbislink/installer/error_codes.h"
#include "orbislink/net/http_client.h"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace orbislink {

RpiClient::RpiClient(Config config) : config_(std::move(config))
{
	if(config_.port == 0)
		config_.port = 12800;
	if(config_.maxAttempts < 1)
		config_.maxAttempts = 1;
}

std::string RpiClient::endpoint() const
{
	return "http://" + config_.host + ":" + std::to_string(config_.port);
}

bool RpiClient::probe(std::string *detail)
{
	// Any HTTP reply means the installer is open (§5.1).
	HttpClient client(config_.timeoutMs < 4000 ? config_.timeoutMs : 4000);
	const HttpResponse response = client.get(endpoint() + "/api/is_exists");
	if(response.transportOk)
	{
		if(detail)
			*detail = "HTTP " + std::to_string(response.status);
		return true;
	}
	// A PS5 with no installer there may have Prospero Manager running.
	if(config_.ps5 && prosperoAnswers(detail))
		return true;
	if(detail)
		*detail = response.error;
	return false;
}

std::string RpiClient::prosperoEndpoint() const
{
	return "http://" + config_.host + ":" + std::to_string(config_.prosperoPort);
}

bool RpiClient::prosperoAnswers(std::string *detail)
{
	HttpClient client(config_.timeoutMs < 4000 ? config_.timeoutMs : 4000);
	const HttpResponse response = client.get(prosperoEndpoint() + "/api/pkg/status");
	if(!response.transportOk || !Json::parse(response.body)["phase"].isString())
		return false;
	if(detail)
		*detail = "Prospero Manager, port " + std::to_string(config_.prosperoPort);
	return true;
}

RpiClient::Protocol RpiClient::protocol()
{
	if(!config_.ps5)
		return Protocol::Rpi;
	const int known = protocol_.load();
	if(known != static_cast<int>(Protocol::Unknown))
		return static_cast<Protocol>(known);

	// Remote Package Installer's API answers JSON; DPI v2 drops a JSON
	// request and shows its page at "/".
	HttpClient client(config_.timeoutMs < 4000 ? config_.timeoutMs : 4000);
	const HttpResponse api = client.post(endpoint() + "/api/is_exists", R"({ "title_id": "CUSA00000" })");
	if(api.transportOk && Json::parse(api.body).isObject())
	{
		protocol_.store(static_cast<int>(Protocol::Rpi));
		return Protocol::Rpi;
	}
	const HttpResponse page = client.get(endpoint() + "/");
	if(page.transportOk && page.body.find("DPIv2") != std::string::npos)
	{
		logInfo("Installer: etaHEN's DPI v2 answers on " + endpoint() + ".");
		protocol_.store(static_cast<int>(Protocol::DpiV2));
		return Protocol::DpiV2;
	}
	if(!api.transportOk && !page.transportOk && prosperoAnswers())
	{
		logInfo("Installer: Prospero Manager answers on " + prosperoEndpoint() + ".");
		protocol_.store(static_cast<int>(Protocol::ProsperoMgr));
		return Protocol::ProsperoMgr;
	}
	// Nothing told apart (or nothing answered): asked again next time.
	return Protocol::Rpi;
}

InstallerResult RpiClient::installDpiV2(const std::string &url)
{
	HttpClient client(config_.timeoutMs);
	const HttpResponse response =
		client.post(endpoint() + "/", "url=" + urlEncodePath(url), "application/x-www-form-urlencoded");
	if(!response.transportOk)
	{
		protocol_.store(static_cast<int>(Protocol::Unknown));
		return InstallerResult::failure(
			std::string(QT_TRANSLATE_NOOP("Messages",
				"Remote installer unavailable. Open Remote Package Installer on the console."))
			+ " (" + response.error + ")");
	}
	InstallerResult result;
	result.httpStatus = response.status;
	result.rawBody = response.body;
	if(startsWith(response.body, "SUCCESS"))
	{
		result.ok = true;
		return result;
	}
	// "FAILED: Install failed with error <name>, code <n> (0x<hex>) for …"
	const size_t hex = response.body.find("(0x");
	if(hex != std::string::npos)
	{
		result.errorCode = static_cast<uint32_t>(std::strtoul(response.body.c_str() + hex + 3, nullptr, 16));
		result.message = describeConsoleError(result.errorCode);
	}
	else
		result.message = QT_TRANSLATE_NOOP("Messages", "The remote installer rejected the request.");
	logError("Remote installer (DPI v2): " + url + " -> " + response.body.substr(0, 200));
	return result;
}

InstallerResult RpiClient::installProspero(const std::string &target)
{
	HttpClient client(config_.timeoutMs);
	const std::string base = prosperoEndpoint();
	const bool remote = startsWith(target, "http://") || startsWith(target, "https://");
	Json request = Json::makeObject();
	request.set(remote ? "url" : "path", Json::fromString(target));
	auto unavailable = [this](const std::string &why) {
		protocol_.store(static_cast<int>(Protocol::Unknown));
		return InstallerResult::failure(std::string(QT_TRANSLATE_NOOP("Messages",
			"Prospero Manager is not answering: load its payload on the console.")) + " (" + why + ")");
	};
	auto failed = [&target](const std::string &error) {
		InstallerResult result;
		result.rawBody = error;
		const size_t hex = error.find("0x");
		if(hex != std::string::npos)
			result.errorCode = static_cast<uint32_t>(std::strtoul(error.c_str() + hex + 2, nullptr, 16));
		result.message = result.errorCode != 0 ? describeConsoleError(result.errorCode) : error;
		logError("Remote installer (Prospero Manager): " + target + " -> " + error.substr(0, 200));
		return result;
	};

	// One install at a time there: while another is being handed over, it
	// says so (409) and is asked again.
	const int64_t deadline = monotonicMillis() + config_.prosperoWaitMs;
	HttpResponse started;
	for(;;)
	{
		started = client.post(base + "/api/pkg/install", request.dump());
		if(!started.transportOk)
			return unavailable(started.error);
		if(started.status != 409 || monotonicMillis() > deadline)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	if(started.status < 200 || started.status >= 300)
	{
		const std::string error = Json::parse(started.body)["error"].toString();
		return failed(error.empty() ? "HTTP " + std::to_string(started.status) : error);
	}

	// Followed until the console has the install queued (it then shows in its
	// notifications); a package it downloads from the PC is followed by what
	// the PC serves once the bytes start going.
	while(monotonicMillis() <= deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
		const HttpResponse status = client.get(base + "/api/pkg/status");
		if(!status.transportOk)
			return unavailable(status.error);
		const Json json = Json::parse(status.body);
		const std::string phase = json["phase"].toString();
		if(phase == "queued")
		{
			logInfo("Prospero Manager queued the install of " + target + ".");
			InstallerResult result = InstallerResult::success();
			result.rawBody = status.body;
			return result;
		}
		if(phase == "error")
			return failed(json["error"].toString());
		if(remote && phase == "downloading" && json["loaded"].toInt(0) > 0)
			return InstallerResult::success();
	}
	return InstallerResult::failure(QT_TRANSLATE_NOOP("Messages",
		"Prospero Manager did not say whether the console took the install."));
}

InstallerResult RpiClient::call(const std::string &path, const std::string &jsonBody, std::string *body)
{
	const Protocol spoken = protocol();
	if(spoken == Protocol::DpiV2)
		return InstallerResult::failure(QT_TRANSLATE_NOOP("Messages",
			"etaHEN's DPI v2 only starts installs: this is not something it does."));
	if(spoken == Protocol::ProsperoMgr)
		return InstallerResult::failure(QT_TRANSLATE_NOOP("Messages",
			"Prospero Manager only starts installs here: this is not something it does."));

	HttpClient client(config_.timeoutMs);
	const std::string url = endpoint() + path;

	HttpResponse response;
	for(int attempt = 0; attempt < config_.maxAttempts; ++attempt)
	{
		if(attempt > 0)
		{
			const int delayMs = config_.backoffBaseMs * (1 << (attempt - 1)); // 1 s, 2 s, 4 s
			logWarning("Remote installer not answering (" + response.error + "); retrying in "
				+ std::to_string(delayMs) + " ms.");
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
		}
		response = client.post(url, jsonBody);
		if(response.transportOk)
			break;
	}

	if(!response.transportOk)
	{
		protocol_.store(static_cast<int>(Protocol::Unknown));
		return InstallerResult::failure(
			std::string(QT_TRANSLATE_NOOP("Messages",
				"Remote installer unavailable. Open Remote Package Installer on the console."))
			+ " (" + response.error + ")");
	}

	if(body)
		*body = response.body;

	std::string parseError;
	const Json json = Json::parse(response.body, &parseError);
	if(json.isNull() || !json.isObject())
	{
		InstallerResult result = InstallerResult::failure(
			std::string(QT_TRANSLATE_NOOP("Messages", "Unexpected response from the remote installer"))
			+ " (" + parseError + ").");
		result.httpStatus = response.status;
		result.rawBody = response.body;
		return result;
	}

	const std::string status = json["status"].toString();
	if(status == "success")
	{
		InstallerResult result = InstallerResult::success();
		result.httpStatus = response.status;
		result.rawBody = response.body;
		return result;
	}

	// Two failure formats: { "error_code": 0x... } from the API and
	// { "error": "text" } from malformed request errors.
	InstallerResult result;
	result.httpStatus = response.status;
	result.rawBody = response.body;
	if(json.contains("error_code"))
	{
		result.errorCode = static_cast<uint32_t>(json["error_code"].toInt());
		result.message = describeConsoleError(result.errorCode);
	}
	else if(json["error"].isString())
		result.message = json["error"].toString();
	else
		result.message = QT_TRANSLATE_NOOP("Messages", "The remote installer rejected the request.");
	logError("Remote installer: " + path + " -> " + result.message);
	return result;
}

InstallerResult RpiClient::installDirect(const std::vector<std::string> &packageUrls,
	InstallTaskHandle *handle)
{
	if(packageUrls.empty())
		return InstallerResult::failure(QT_TRANSLATE_NOOP("Messages", "No package given to install."));

	const Protocol spoken = protocol();
	if(spoken == Protocol::DpiV2 || spoken == Protocol::ProsperoMgr)
	{
		// One install per request; no task to follow.
		InstallerResult result = InstallerResult::success();
		for(const std::string &url : packageUrls)
		{
			result = spoken == Protocol::DpiV2 ? installDpiV2(url) : installProspero(url);
			if(!result.ok)
				break;
		}
		if(handle)
			handle->taskId = -1;
		return result;
	}

	Json request = Json::makeObject();
	request.set("type", Json::fromString("direct"));
	Json packages = Json::makeArray();
	for(const std::string &url : packageUrls)
		packages.push(Json::fromString(url));
	request.set("packages", packages);

	std::string body;
	InstallerResult result = call("/api/install", request.dump(), &body);
	if(result.ok && handle)
	{
		const Json json = Json::parse(body);
		handle->taskId = static_cast<int>(json["task_id"].toInt(-1));
		handle->title = json["title"].toString();
	}
	return result;
}

InstallerResult RpiClient::installFromReferenceJson(const std::string &url, InstallTaskHandle *handle)
{
	Json request = Json::makeObject();
	request.set("type", Json::fromString("ref_pkg_url"));
	request.set("url", Json::fromString(url));

	std::string body;
	InstallerResult result = call("/api/install", request.dump(), &body);
	if(result.ok && handle)
	{
		const Json json = Json::parse(body);
		handle->taskId = static_cast<int>(json["task_id"].toInt(-1));
		handle->title = json["title"].toString();
	}
	return result;
}

InstallerResult RpiClient::isExists(const std::string &titleId, bool *exists, int64_t *size)
{
	Json request = Json::makeObject();
	request.set("title_id", Json::fromString(titleId));

	std::string body;
	InstallerResult result = call("/api/is_exists", request.dump(), &body);
	if(result.ok)
	{
		const Json json = Json::parse(body);
		// "exists" comes as a string ("true"/"false") in flatz's installer.
		if(exists)
			*exists = json["exists"].toLooseBool(false);
		if(size)
			*size = json.contains("size") ? json["size"].toInt(-1) : -1;
	}
	return result;
}

InstallerResult RpiClient::taskProgress(int taskId, TaskProgress *progress)
{
	Json request = Json::makeObject();
	request.set("task_id", Json::fromInt(taskId));

	std::string body;
	InstallerResult result = call("/api/get_task_progress", request.dump(), &body);
	if(result.ok && progress)
	{
		const Json json = Json::parse(body);
		progress->bits = static_cast<uint32_t>(json["bits"].toInt());
		progress->errorResult = static_cast<int32_t>(json["error"].toInt());
		progress->length = json["length"].toInt();
		progress->transferred = json["transferred"].toInt();
		progress->lengthTotal = json["length_total"].toInt();
		progress->transferredTotal = json["transferred_total"].toInt();
		progress->numIndex = static_cast<uint32_t>(json["num_index"].toInt());
		progress->numTotal = static_cast<uint32_t>(json["num_total"].toInt());
		progress->restSec = static_cast<uint32_t>(json["rest_sec"].toInt());
		progress->restSecTotal = static_cast<uint32_t>(json["rest_sec_total"].toInt());
		progress->preparingPercent = static_cast<int32_t>(json["preparing_percent"].toInt());
		progress->localCopyPercent = static_cast<int32_t>(json["local_copy_percent"].toInt());
	}
	return result;
}

InstallerResult RpiClient::findTask(const std::string &contentId, TaskSubType subType, int *taskId)
{
	Json request = Json::makeObject();
	request.set("content_id", Json::fromString(contentId));
	request.set("sub_type", Json::fromInt(static_cast<int64_t>(subType)));

	std::string body;
	InstallerResult result = call("/api/find_task", request.dump(), &body);
	if(result.ok && taskId)
		*taskId = static_cast<int>(Json::parse(body)["task_id"].toInt(-1));
	return result;
}

InstallerResult RpiClient::taskCommand(const std::string &path, int taskId)
{
	Json request = Json::makeObject();
	request.set("task_id", Json::fromInt(taskId));
	return call(path, request.dump(), nullptr);
}

InstallerResult RpiClient::startTask(int taskId) { return taskCommand("/api/start_task", taskId); }
InstallerResult RpiClient::stopTask(int taskId) { return taskCommand("/api/stop_task", taskId); }
InstallerResult RpiClient::pauseTask(int taskId) { return taskCommand("/api/pause_task", taskId); }
InstallerResult RpiClient::resumeTask(int taskId) { return taskCommand("/api/resume_task", taskId); }

InstallerResult RpiClient::unregisterTask(int taskId)
{
	return taskCommand("/api/unregister_task", taskId);
}

InstallerResult RpiClient::uninstallGame(const std::string &titleId)
{
	Json request = Json::makeObject();
	request.set("title_id", Json::fromString(titleId));
	return call("/api/uninstall_game", request.dump(), nullptr);
}

InstallerResult RpiClient::uninstallPatch(const std::string &titleId)
{
	Json request = Json::makeObject();
	request.set("title_id", Json::fromString(titleId));
	return call("/api/uninstall_patch", request.dump(), nullptr);
}

InstallerResult RpiClient::uninstallAdditionalContent(const std::string &contentId)
{
	Json request = Json::makeObject();
	request.set("content_id", Json::fromString(contentId));
	return call("/api/uninstall_ac", request.dump(), nullptr);
}

InstallerResult RpiClient::uninstallTheme(const std::string &contentId)
{
	Json request = Json::makeObject();
	request.set("content_id", Json::fromString(contentId));
	return call("/api/uninstall_theme", request.dump(), nullptr);
}

} // namespace orbislink
