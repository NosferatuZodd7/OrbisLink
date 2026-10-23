// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QMetaObject>
#include <QObject>

#include <memory>
#include <mutex>
#include <utility>

namespace orbislink {

// The way back to a QObject for the threads it starts and leaves running.
//
// A thread that outlives its object and then posts to it reaches freed
// memory: Qt asks the dead object which thread it lives on, and the
// application crashes on the way out. The object cuts the line when it goes
// away; whatever is posted after that is dropped. What was already queued is
// dropped by Qt itself when the object is destroyed.
class Lifeline
{
public:
	explicit Lifeline(QObject *owner) : owner_(owner) {}

	// Runs `work` later on the owner's thread, while the owner lives.
	template<typename Work>
	void post(Work &&work)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(owner_)
			QMetaObject::invokeMethod(owner_, std::forward<Work>(work), Qt::QueuedConnection);
	}

	bool alive() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return owner_ != nullptr;
	}

	// The owner is going away: called first thing in its destructor.
	void cut()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		owner_ = nullptr;
	}

private:
	mutable std::mutex mutex_;
	QObject *owner_;
};

using LifelinePtr = std::shared_ptr<Lifeline>;

} // namespace orbislink
