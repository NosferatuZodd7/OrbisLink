// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/registration.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/stream/chiaki_log_bridge.h"
#include "orbislink/stream/credentials.h"
#include "orbislink/stream/stream_trace.h"

#include <chiaki/regist.h>
#include <chiaki/session.h>

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
	credentials.ps5 = chiaki_target_is_ps5(host->target);
	credentials.hostId = bytesToHex(host->server_mac, sizeof(host->server_mac));
	credentials.valid = !credentials.registKey.empty();
	return credentials;
}

// What the console said when refusing, in two versions: short for the
// diagnostics, full for whoever is registering.
struct Recusa
{
	std::string diagnostico;
	std::string mensagem;
};

Recusa explicarRecusa(uint32_t motivo)
{
	switch(motivo)
	{
		case CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID:
			return { "the console did not recognise the Account ID (0x80108b02)",
				QT_TRANSLATE_NOOP("Messages", "The console did not recognise the Account ID. It must belong "
					"to the PSN account you used to request the PIN on the "
					"console (each user has their own), not the PSN user name.") };
		case CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED:
			return { "the console rejected the PIN (0x80108b09)",
				QT_TRANSLATE_NOOP("Messages", "The console rejected the PIN. Request a new one on the "
					"console — it is only valid for a few minutes — and type it "
					"again.") };
		case CHIAKI_RP_APPLICATION_REASON_IN_USE:
			return { "the console's Remote Play is already in use (0x80108b10)",
				QT_TRANSLATE_NOOP("Messages", "The console's Remote Play is already in use by another "
					"device. Close that session and try again.") };
		case CHIAKI_RP_APPLICATION_REASON_CRASH:
			return { "the console's Remote Play crashed (0x80108b15)",
				QT_TRANSLATE_NOOP("Messages", "The console's Remote Play crashed. Restart the console and "
					"try again.") };
		case CHIAKI_RP_APPLICATION_REASON_RP_VERSION:
			return { "incompatible Remote Play version (0x80108b11)",
				QT_TRANSLATE_NOOP("Messages", "The console did not accept the Remote Play version. Update "
					"the console system software and try again.") };
		default:
			return { "the console rejected the registration (expired or wrong PIN, wrong "
					 "Account ID, or Remote Play turned off on the console)",
				QT_TRANSLATE_NOOP("Messages", "The console rejected the registration. Check the PIN (it is "
					"valid for a few minutes), the PSN Account ID, and that the "
					"console is on the same network.") };
	}
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
			StreamTrace::instance().ok("console \"" + credentials.nickname + "\" registered, "
				"host-id " + credentials.hostId);
			if(finished)
				finished(true, credentials, std::string());
			break;
		}
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
			impl->running.store(false);
			StreamTrace::instance().fail("registration cancelled");
			if(finished)
				finished(false, {}, QT_TRANSLATE_NOOP("Messages", "Registration cancelled."));
			break;
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
		default:
		{
			impl->running.store(false);
			const Recusa recusa = explicarRecusa(takeApplicationReason());
			StreamTrace::instance().fail(recusa.diagnostico);
			if(finished)
				finished(false, {}, recusa.mensagem);
			break;
		}
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
			*error = QT_TRANSLATE_NOOP("Messages", "A registration is already in progress.");
		return false;
	}
	if(request.address.empty())
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The console's address is missing.");
		return false;
	}
	// The console PIN has 8 digits.
	if(request.pin == 0)
	{
		if(error)
			*error = QT_TRANSLATE_NOOP("Messages", "The PIN shown by the console under Add Device is "
				"missing.");
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

	StreamTrace::instance().step("registration",
		"target " + std::to_string(request.target) + ", " + std::to_string(request.pin > 0 ? 8 : 0)
			+ "-digit PIN, Account ID with "
			+ std::to_string(request.accountIdBase64.size()) + " characters");

	impl_->running.store(true);
	takeApplicationReason(); // an old reason does not apply to this registration
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
	logInfo("Remote Play: registration started on " + request.address);
	return true;
}

void StreamRegistration::cancel()
{
	if(!impl_->running.load())
		return;
	chiaki_regist_stop(&impl_->regist);
}

} // namespace orbislink
