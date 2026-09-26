// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Os textos que o utilizador lê e que nascem fora do Qt (o Remote Play, o
// registo, as credenciais) marcam-se com QT_TRANSLATE_NOOP("Messages", ...).
// O lupdate encontra-os pela macro e põe-nos nos .ts; aqui ela não faz nada,
// e a interface traduz o texto quando o mostra (ver translateMessage()).
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif
