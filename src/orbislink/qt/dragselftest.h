// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <functional>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace orbislink::dragselftest {

// Drags a fake file over the window with synthetic events and checks
// that the overlay does not flicker.
//
// Guards against a concrete bug: with a DropArea covering the window and
// one more per zone ("install" and "upload via FTP"), entering a zone made
// the window's one say "left", the overlay closed, the cursor was over the
// window's one again, which said "entered" — and so on in a loop, and it
// took two drops to hit the option.
//
// The test goes: enter the window → enter the left zone → move inside it
// → cross over to the right one → move → drop. Throughout that whole path
// the overlay must stay open without interruption.
// Calls `finished` with the number of times it closed midway: 0 passes.
void run(QQuickWindow *window, std::function<void(int fechouAMeio, const QString &relato)> finished);

} // namespace orbislink::dragselftest
