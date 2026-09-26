// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/installer/error_codes.h"

#include "orbislink/common/tr.h"

#include <cstdio>
#include <map>

namespace orbislink {

namespace {

// Valores confirmados em
// OpenOrbis-PS4-Toolchain/include/orbis/_types/errors.h (ORBIS_KERNEL_ERROR_*).
// TODO: os códigos específicos do BGFT/AppInstUtil (famílias 0x8099xxxx e
// 0x8024xxxx) não estão publicados em nenhuma fonte que se possa citar; até
// haver confirmação, esses códigos aparecem em hexadecimal na UI.
const std::map<uint32_t, const char *> &knownErrors()
{
	static const std::map<uint32_t, const char *> table = {
		{ 0x80020001u, QT_TRANSLATE_NOOP("Mensagens", "Operação não permitida na consola (EPERM).") },
		{ 0x80020002u, QT_TRANSLATE_NOOP("Mensagens", "A consola não encontrou o ficheiro ou o caminho (ENOENT).") },
		{ 0x80020005u, QT_TRANSLATE_NOOP("Mensagens", "Erro de entrada/saída na consola (EIO).") },
		{ 0x8002000Cu, QT_TRANSLATE_NOOP("Mensagens", "A consola ficou sem memória (ENOMEM).") },
		{ 0x8002000Du, QT_TRANSLATE_NOOP("Mensagens", "Acesso negado na consola (EACCES).") },
		{ 0x80020010u, QT_TRANSLATE_NOOP("Mensagens", "O recurso está ocupado na consola (EBUSY).") },
		{ 0x80020011u, QT_TRANSLATE_NOOP("Mensagens", "Já existe (EEXIST).") },
		{ 0x80020016u, QT_TRANSLATE_NOOP("Mensagens", "Pedido inválido para a consola (EINVAL).") },
		{ 0x8002001Bu, QT_TRANSLATE_NOOP("Mensagens", "Ficheiro demasiado grande para a consola (EFBIG).") },
		{ 0x8002001Cu, QT_TRANSLATE_NOOP("Mensagens", "Espaço insuficiente na consola (ENOSPC).") },
		{ 0x8002001Eu, QT_TRANSLATE_NOOP("Mensagens", "Sistema de ficheiros só de leitura (EROFS).") },
		{ 0x80020023u, QT_TRANSLATE_NOOP("Mensagens", "A consola pediu para tentar de novo (EAGAIN).") },
		{ 0x80020033u, QT_TRANSLATE_NOOP("Mensagens", "A consola não conseguiu alcançar a rede (ENETUNREACH).") },
		{ 0x80020035u, QT_TRANSLATE_NOOP("Mensagens", "Ligação abortada (ECONNABORTED).") },
		{ 0x80020036u, QT_TRANSLATE_NOOP("Mensagens", "A ligação foi reposta pela outra ponta (ECONNRESET).") },
		{ 0x8002003Cu, QT_TRANSLATE_NOOP("Mensagens", "A consola excedeu o tempo de espera (ETIMEDOUT).") },
		{ 0x8002003Du, QT_TRANSLATE_NOOP("Mensagens", "A consola não conseguiu ligar-se ao PC (ECONNREFUSED). "
					   "Verifica a firewall do Windows e se estão na mesma rede.") },
		{ 0x80020041u, QT_TRANSLATE_NOOP("Mensagens", "A consola não alcança o PC (EHOSTUNREACH).") },
		{ 0x80020055u, QT_TRANSLATE_NOOP("Mensagens", "A operação foi cancelada (ECANCELED).") },
	};
	return table;
}

} // namespace

std::string formatErrorCode(uint32_t code)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "0x%08X", code);
	return buffer;
}

std::string describeConsoleError(uint32_t code)
{
	if(code == 0)
		return std::string();
	const auto &table = knownErrors();
	auto it = table.find(code);
	if(it != table.end())
		return std::string(it->second) + " (" + formatErrorCode(code) + ")";
	return std::string(QT_TRANSLATE_NOOP("Mensagens", "A consola devolveu um erro")) + ": " + formatErrorCode(code);
}

bool isOutOfSpaceError(uint32_t code) { return code == 0x8002001Cu; }

} // namespace orbislink
