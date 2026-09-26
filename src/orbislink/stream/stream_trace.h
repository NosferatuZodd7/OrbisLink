// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

// Trace of a Remote Play attempt.
//
// When a connection fails, what is needed is not "it didn't work", but
// which step it stopped at, how long it took to get there and what the console answered.
//
// Each step is recorded with the moment it started and ended. At the end,
// summary() gives a text that can be pasted into a message.
class StreamTrace
{
public:
	enum class Result { Running, Ok, Failed, Skipped };

	struct Step
	{
		std::string name;
		std::string detail;
		Result result = Result::Running;
		int64_t startedMs = 0;
		int64_t endedMs = 0;
		int64_t durationMs() const { return endedMs > startedMs ? endedMs - startedMs : 0; }
	};

	static StreamTrace &instance();

	// Starts a new attempt. Clears the previous one.
	void begin(const std::string &address);
	// Stores the address if there is none yet. Periodic discovery uses
	// this: it does not open an attempt, but the report still learns who
	// was being asked.
	void addressIfUnset(const std::string &address);
	// Opens a step. The previous one is closed automatically, as a success.
	void step(const std::string &name, const std::string &detail = std::string());
	// Fecha o passo aberto.
	void ok(const std::string &detail = std::string());
	void fail(const std::string &detail);
	void skip(const std::string &name, const std::string &reason);
	// A loose note, without opening a step (e.g. first frame, state change).
	void note(const std::string &text);
	void end();

	std::vector<Step> steps() const;
	std::vector<std::string> notes() const;
	std::string summary() const;
	bool active() const;

private:
	StreamTrace() = default;
};

// Closes the step as a success when leaving scope, unless it was already
// closed — so no steps are left open when leaving through a return.
class StreamStep
{
public:
	StreamStep(const std::string &name, const std::string &detail = std::string());
	~StreamStep();
	void ok(const std::string &detail = std::string());
	void fail(const std::string &detail);

private:
	bool closed_ = false;
};

} // namespace orbislink
