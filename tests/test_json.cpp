// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/json.h"
#include "test_support.h"

#include <sstream>

using orbislink::Json;

ORBISLINK_TEST(parse_objeto_simples)
{
	std::string error;
	const Json json = Json::parse(R"({"status":"success","task_id":7,"title":"Jogo"})", &error);
	CHECK(error.empty());
	CHECK(json.isObject());
	CHECK_EQ(json["status"].toString(), std::string("success"));
	CHECK_EQ(json["task_id"].toInt(), 7);
	CHECK_EQ(json["title"].toString(), std::string("Jogo"));
}

ORBISLINK_TEST(aceita_hexadecimal_do_instalador)
{
	// Resposta real de /api/get_task_progress: números em hexadecimal sem aspas.
	const std::string body =
		R"({ "status": "success", "bits": 0x1, "error": 0, "length": 0x40000000, )"
		R"("transferred": 0x200, "length_total": 0x40000000, "transferred_total": 0x200, )"
		R"("num_index": 0, "num_total": 1, "rest_sec": 12, "rest_sec_total": 12, )"
		R"("preparing_percent": 100, "local_copy_percent": 0 })";
	std::string error;
	const Json json = Json::parse(body, &error);
	CHECK(error.empty());
	CHECK_EQ(json["length_total"].toInt(), 0x40000000);
	CHECK_EQ(json["transferred_total"].toInt(), 512);
	CHECK_EQ(json["bits"].toInt(), 1);
	CHECK_EQ(json["rest_sec"].toInt(), 12);
}

ORBISLINK_TEST(aceita_codigo_de_erro_hexadecimal)
{
	const Json json = Json::parse(R"({ "status": "fail", "error_code": 0x8002001C })");
	CHECK_EQ(json["status"].toString(), std::string("fail"));
	CHECK_EQ(static_cast<uint32_t>(json["error_code"].toInt()), 0x8002001Cu);
}

ORBISLINK_TEST(exists_vem_como_string)
{
	const Json json = Json::parse(R"({ "status": "success", "exists": "true", "size": 0x1A2B })");
	CHECK(json["exists"].toLooseBool(false));
	CHECK_EQ(json["size"].toInt(), 0x1A2B);
	const Json negative = Json::parse(R"({ "exists": "false" })");
	CHECK(!negative["exists"].toLooseBool(true));
}

ORBISLINK_TEST(escapes_e_unicode)
{
	const Json json = Json::parse(R"({"title":"Jogo \"raro\"ç\n"})");
	CHECK_EQ(json["title"].toString(), std::string("Jogo \"raro\"\xc3\xa7\n"));
	const std::string dumped = Json::fromString("a\"b\\c\n").dump();
	CHECK_EQ(dumped, std::string("\"a\\\"b\\\\c\\n\""));
}

ORBISLINK_TEST(arrays_e_ida_e_volta)
{
	Json root = Json::makeObject();
	root.set("type", Json::fromString("direct"));
	Json packages = Json::makeArray();
	packages.push(Json::fromString("http://192.168.1.10:8765/f/abc/x.pkg"));
	root.set("packages", packages);

	const Json reparsed = Json::parse(root.dump());
	CHECK_EQ(reparsed["packages"].size(), static_cast<size_t>(1));
	CHECK_EQ(reparsed["packages"].at(0).toString(),
		std::string("http://192.168.1.10:8765/f/abc/x.pkg"));
}

ORBISLINK_TEST(entrada_invalida_nao_rebenta)
{
	std::string error;
	const Json json = Json::parse("{ isto nao e json", &error);
	CHECK(json.isNull());
	CHECK(!error.empty());
	// Acessos a chaves inexistentes devolvem valores por omissão.
	CHECK_EQ(json["seja_o_que_for"].toInt(-1), -1);
	CHECK_EQ(json.at(3).toString("vazio"), std::string("vazio"));
}

TEST_MAIN()
