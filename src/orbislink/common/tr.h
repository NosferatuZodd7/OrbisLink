// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Texts the user reads that are born outside Qt (Remote Play, registration,
// credentials, the queue, FTP) are marked with QT_TRANSLATE_NOOP("Messages", ...).
// lupdate finds them through the macro and puts them in the .ts files; here it
// does nothing, and the interface translates the text when it shows it (see
// translateMessage()).
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif
