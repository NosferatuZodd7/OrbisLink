// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/registration.h"

#include "orbislink/common/log.h"
#include "orbislink/stream/chiaki_log_bridge.h"
#include "orbislink/stream/credentials.h"
#include "orbislink/stream/stream_trace.h"

#include <chiaki/regist.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace orbislink {

struct StreamRegistration::Impl
{
	std::mutex mutex;
	ChiakiRegist regist {};
	std::atomic<bool> running { false };
	bool started = false;
	std::string host;
	Finished finished;

	~Impl()
	{
		if(started)
		{
			chiaki_regist_stop(&regist);
			chiaki_regist_fini(&regist);
		}
	}
};

namespace {

StreamCredentials fromChiaki(const ChiakiRegisteredHost *host)
{
	StreamCredentials credentials;
	if(!host)
		return credentials;
	credentials.nickname = std::string(host->server_nickname,
		strnlen(host->server_nickname, sizeof(host->server_nickname)));
	credentials.registKey = std::string(host->rp_regist_key,
		strnlen(host->rp_regist_key, sizeof(host->rp_regist_key)));
	credentials.rpKeyHex = bytesToHex(host->rp_key, sizeof(host->rp_key));
	credentials.rpKeyType = host->rp_key_type;
	credentials.target = static_cast<int>(host->target);
	credentials.hostId = bytesToHex(host->server_mac, sizeof(host->server_mac));
	credentials.valid = !credentials.registKey.empty();
	return credentials;
}

void registCallback(ChiakiRegistEvent *event, void *user)
{
	auto *impl = static_cast<StreamRegistration::Impl *>(user);
	StreamRegistration::Finished finished;
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		finished = impl->finished;
	}

	switch(event->type)
	{
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
		{
			StreamCredentials credentials = fromChiaki(event->registered_host);
			impl->running.store(false);
			StreamTrace::instance().ok("consola \"" + credentials.nickname + "\" registada, "
				"host-id " + credentials.hostId);
			if(finished)
				finished(true, credentials, std::string());
			break;
		}
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
			impl->running.store(false);
			StreamTrace::instance().fail("registo cancelado");
			if(finished)
				finished(false, {}, "Registo cancelado.");
			break;
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
		default:
			impl->running.store(false);
			StreamTrace::instance().fail("a consola recusou o registo (PIN expirado ou errado, "
				"Account ID errado, ou Remote Play desligado na consola)");
			if(finished)
				finished(false, {},
					"A consola recusou o registo. Confirma o PIN (é válido poucos minutos), "
					"o Account ID da PSN e que a consola está ligada na mesma rede.");
			break;
	}
}

} // namespace

StreamRegistration::StreamRegistration() : impl_(std::make_unique<Impl>()) {}
StreamRegistration::~StreamRegistration() = default;

bool StreamRegistration::running() const { return impl_->running.load(); }

bool StreamRegistration::start(const Request &request, Finished finished, std::string *error)
{
	if(impl_->running.load())
	{
		if(error)
			*error = "Já há um registo a decorrer.";
		return false;
	}
	if(request.address.empty())
	{
		if(error)
			*error = "Falta o endereço da consola.";
		return false;
	}
	// O PIN da consola tem 8 dígitos.
	if(request.pin == 0)
	{
		if(error)
			*error = "Falta o PIN que a consola mostra em Adicionar Dispositivo.";
		return false;
	}

	unsigned char accountId[CHIAKI_PSN_ACCOUNT_ID_SIZE] {};
	std::string decodeError;
	if(!decodeAccountId(request.accountIdBase64, accountId, &decodeError))
	{
		if(error)
			*error = decodeError;
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		impl_->finished = std::move(finished);
		impl_->host = request.address;
	}

	ChiakiRegistInfo info {};
	info.target = static_cast<ChiakiTarget>(request.target);
	info.host = impl_->host.c_str();
	info.broadcast = false;
	info.psn_online_id = nullptr; // PS4 >= 7.0 usa o Account ID
	std::memcpy(info.psn_account_id, accountId, sizeof(accountId));
	info.pin = request.pin;
	info.console_pin = 0;
	info.holepunch_info = nullptr;
	info.rudp = nullptr;

	StreamTrace::instance().step("registo",
		"alvo " + std::to_string(request.target) + ", PIN de " + std::to_string(request.pin > 0 ? 8 : 0)
			+ " dígitos, Account ID com "
			+ std::to_string(request.accountIdBase64.size()) + " caracteres");

	impl_->running.store(true);
	const ChiakiErrorCode result =
		chiaki_regist_start(&impl_->regist, chiakiLog(), &info, registCallback, impl_.get());
	if(result != CHIAKI_ERR_SUCCESS)
	{
		impl_->running.store(false);
		StreamTrace::instance().fail(std::string("chiaki_regist_start: ")
			+ chiaki_error_string(result));
		if(error)
			*error = chiaki_error_string(result);
		return false;
	}
	impl_->started = true;
	logInfo("Remote Play: registo iniciado em " + request.address);
	return true;
}

void StreamRegistration::cancel()
{
	if(!impl_->running.load())
		return;
	chiaki_regist_stop(&impl_->regist);
}

} // namespace orbislink
