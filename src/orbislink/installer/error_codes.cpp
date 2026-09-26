// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/installer/error_codes.h"

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
		{ 0x80020001u, "Operação não permitida na consola (EPERM)." },
		{ 0x80020002u, "A consola não encontrou o ficheiro ou o caminho (ENOENT)." },
		{ 0x80020005u, "Erro de entrada/saída na consola (EIO)." },
		{ 0x8002000Cu, "A consola ficou sem memória (ENOMEM)." },
		{ 0x8002000Du, "Acesso negado na consola (EACCES)." },
		{ 0x80020010u, "O recurso está ocupado na consola (EBUSY)." },
		{ 0x80020011u, "Já existe (EEXIST)." },
		{ 0x80020016u, "Pedido inválido para a consola (EINVAL)." },
		{ 0x8002001Bu, "Ficheiro demasiado grande para a consola (EFBIG)." },
		{ 0x8002001Cu, "Espaço insuficiente na consola (ENOSPC)." },
		{ 0x8002001Eu, "Sistema de ficheiros só de leitura (EROFS)." },
		{ 0x80020023u, "A consola pediu para tentar de novo (EAGAIN)." },
		{ 0x80020033u, "A consola não conseguiu alcançar a rede (ENETUNREACH)." },
		{ 0x80020035u, "Ligação abortada (ECONNABORTED)." },
		{ 0x80020036u, "A ligação foi reposta pela outra ponta (ECONNRESET)." },
		{ 0x8002003Cu, "A consola excedeu o tempo de espera (ETIMEDOUT)." },
		{ 0x8002003Du, "A consola não conseguiu ligar-se ao PC (ECONNREFUSED). "
					   "Verifica a firewall do Windows e se estão na mesma rede." },
		{ 0x80020041u, "A consola não alcança o PC (EHOSTUNREACH)." },
		{ 0x80020055u, "A operação foi cancelada (ECANCELED)." },
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
	return "A consola devolveu o erro " + formatErrorCode(code) + ".";
}

bool isOutOfSpaceError(uint32_t code) { return code == 0x8002001Cu; }

} // namespace orbislink
