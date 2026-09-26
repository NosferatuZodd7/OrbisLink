// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/stream/stream_trace.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"

#include <mutex>
#include <sstream>

namespace orbislink {

namespace {

struct State
{
	std::mutex mutex;
	std::string address;
	int64_t startedMs = 0;
	bool active = false;
	std::vector<StreamTrace::Step> steps;
	std::vector<std::string> notes;
};

State &state()
{
	static State s;
	return s;
}

const char *resultName(StreamTrace::Result result)
{
	switch(result)
	{
		case StreamTrace::Result::Ok: return "ok";
		case StreamTrace::Result::Failed: return "FAILED";
		case StreamTrace::Result::Skipped: return "skipped";
		case StreamTrace::Result::Running: return "running";
	}
	return "?";
}

} // namespace

StreamTrace &StreamTrace::instance()
{
	static StreamTrace trace;
	return trace;
}

void StreamTrace::begin(const std::string &address)
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	s.address = address;
	s.startedMs = monotonicMillis();
	s.active = true;
	s.steps.clear();
	s.notes.clear();
	logInfo("Remote Play: attempt started for " + address);
}

void StreamTrace::addressIfUnset(const std::string &address)
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if(s.address.empty())
		s.address = address;
	if(s.startedMs == 0)
		s.startedMs = monotonicMillis();
}

void StreamTrace::step(const std::string &name, const std::string &detail)
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	// Fecha o anterior, se ficou aberto.
	if(!s.steps.empty() && s.steps.back().result == Result::Running)
	{
		s.steps.back().result = Result::Ok;
		s.steps.back().endedMs = monotonicMillis();
	}
	Step step;
	step.name = name;
	step.detail = detail;
	step.startedMs = monotonicMillis();
	s.steps.push_back(step);
	logInfo("Remote Play [" + name + "]" + (detail.empty() ? "" : ": " + detail));
}

void StreamTrace::ok(const std::string &detail)
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if(s.steps.empty() || s.steps.back().result != Result::Running)
		return;
	Step &step = s.steps.back();
	step.result = Result::Ok;
	step.endedMs = monotonicMillis();
	if(!detail.empty())
		step.detail = detail;
	logInfo("Remote Play [" + step.name + "] ok in " + std::to_string(step.durationMs()) + " ms"
		+ (detail.empty() ? "" : " — " + detail));
}

void StreamTrace::fail(const std::string &detail)
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if(s.steps.empty() || s.steps.back().result != Result::Running)
	{
		s.notes.push_back("failure outside a step: " + detail);
		logError("Remote Play: " + detail);
		return;
	}
	Step &step = s.steps.back();
	step.result = Result::Failed;
	step.endedMs = monotonicMillis();
	step.detail = detail;
	logError("Remote Play [" + step.name + "] FAILED after "
		+ std::to_string(step.durationMs()) + " ms — " + detail);
}

void StreamTrace::skip(const std::string &name, const std::string &reason)
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	Step step;
	step.name = name;
	step.detail = reason;
	step.result = Result::Skipped;
	step.startedMs = step.endedMs = monotonicMillis();
	s.steps.push_back(step);
	logInfo("Remote Play [" + name + "] saltado — " + reason);
}

void StreamTrace::note(const std::string &text)
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	const int64_t desde = s.startedMs > 0 ? monotonicMillis() - s.startedMs : 0;
	s.notes.push_back("+" + std::to_string(desde) + " ms  " + text);
	logInfo("Remote Play: " + text);
}

void StreamTrace::end()
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if(!s.steps.empty() && s.steps.back().result == Result::Running)
	{
		s.steps.back().result = Result::Ok;
		s.steps.back().endedMs = monotonicMillis();
	}
	s.active = false;
}

bool StreamTrace::active() const
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.active;
}

std::vector<StreamTrace::Step> StreamTrace::steps() const
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.steps;
}

std::vector<std::string> StreamTrace::notes() const
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	return s.notes;
}

std::string StreamTrace::summary() const
{
	State &s = state();
	std::lock_guard<std::mutex> lock(s.mutex);
	if(s.steps.empty() && s.notes.empty())
		return "No Remote Play attempt yet in this session.";

	std::ostringstream out;
	out << "Remote Play attempt for " << (s.address.empty() ? "(no address)" : s.address)
		<< "\n";
	for(const Step &step : s.steps)
	{
		out << "  " << step.name;
		for(size_t i = step.name.size(); i < 28; ++i)
			out << ' ';
		out << resultName(step.result);
		if(step.result != Result::Skipped)
			out << "  (" << step.durationMs() << " ms)";
		if(!step.detail.empty())
			out << "  — " << step.detail;
		out << "\n";
	}
	if(!s.notes.empty())
	{
		out << "  notes:\n";
		for(const std::string &note : s.notes)
			out << "    " << note << "\n";
	}
	return out.str();
}

StreamStep::StreamStep(const std::string &name, const std::string &detail)
{
	StreamTrace::instance().step(name, detail);
}

StreamStep::~StreamStep()
{
	if(!closed_)
		StreamTrace::instance().ok();
}

void StreamStep::ok(const std::string &detail)
{
	closed_ = true;
	StreamTrace::instance().ok(detail);
}

void StreamStep::fail(const std::string &detail)
{
	closed_ = true;
	StreamTrace::instance().fail(detail);
}

} // namespace orbislink
