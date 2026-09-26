// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/installer/rpi_client.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/installer/error_codes.h"
#include "orbislink/net/http_client.h"

#include <chrono>
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
	// Qualquer resposta HTTP significa que o instalador está aberto (§5.1).
	HttpClient client(config_.timeoutMs < 4000 ? config_.timeoutMs : 4000);
	const HttpResponse response = client.get(endpoint() + "/api/is_exists");
	if(response.transportOk)
	{
		if(detail)
			*detail = "HTTP " + std::to_string(response.status);
		return true;
	}
	if(detail)
		*detail = response.error;
	return false;
}

InstallerResult RpiClient::call(const std::string &path, const std::string &jsonBody, std::string *body)
{
	HttpClient client(config_.timeoutMs);
	const std::string url = endpoint() + path;

	HttpResponse response;
	for(int attempt = 0; attempt < config_.maxAttempts; ++attempt)
	{
		if(attempt > 0)
		{
			const int delayMs = config_.backoffBaseMs * (1 << (attempt - 1)); // 1 s, 2 s, 4 s
			logWarning("Instalador remoto sem resposta (" + response.error + "); nova tentativa em "
				+ std::to_string(delayMs) + " ms.");
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
		}
		response = client.post(url, jsonBody);
		if(response.transportOk)
			break;
	}

	if(!response.transportOk)
	{
		return InstallerResult::failure(
			"Instalador remoto indisponível. Abre o Remote Package Installer na consola. ("
			+ response.error + ")");
	}

	if(body)
		*body = response.body;

	std::string parseError;
	const Json json = Json::parse(response.body, &parseError);
	if(json.isNull() || !json.isObject())
	{
		InstallerResult result = InstallerResult::failure(
			"Resposta inesperada do instalador remoto (" + parseError + ").");
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

	// Dois formatos de falha: { "error_code": 0x... } da API e
	// { "error": "texto" } dos erros de pedido malformado.
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
		result.message = "O instalador remoto recusou o pedido.";
	logError("Instalador remoto: " + path + " -> " + result.message);
	return result;
}

InstallerResult RpiClient::installDirect(const std::vector<std::string> &packageUrls,
	InstallTaskHandle *handle)
{
	if(packageUrls.empty())
		return InstallerResult::failure("Nenhum pacote indicado para instalação.");

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
		// "exists" vem como string ("true"/"false") no instalador do flatz.
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
