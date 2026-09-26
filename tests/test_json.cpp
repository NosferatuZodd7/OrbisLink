// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/json.h"
#include "test_support.h"

#include <sstream>

using orbislink::Json;

ORBISLINK_TEST(parse_simple_object)
{
	std::string error;
	const Json json = Json::parse(R"({"status":"success","task_id":7,"title":"Game"})", &error);
	CHECK(error.empty());
	CHECK(json.isObject());
	CHECK_EQ(json["status"].toString(), std::string("success"));
	CHECK_EQ(json["task_id"].toInt(), 7);
	CHECK_EQ(json["title"].toString(), std::string("Game"));
}

ORBISLINK_TEST(accepts_installer_hexadecimal)
{
	// Real /api/get_task_progress reply: hexadecimal numbers without quotes.
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

ORBISLINK_TEST(accepts_hexadecimal_error_code)
{
	const Json json = Json::parse(R"({ "status": "fail", "error_code": 0x8002001C })");
	CHECK_EQ(json["status"].toString(), std::string("fail"));
	CHECK_EQ(static_cast<uint32_t>(json["error_code"].toInt()), 0x8002001Cu);
}

ORBISLINK_TEST(exists_comes_as_a_string)
{
	const Json json = Json::parse(R"({ "status": "success", "exists": "true", "size": 0x1A2B })");
	CHECK(json["exists"].toLooseBool(false));
	CHECK_EQ(json["size"].toInt(), 0x1A2B);
	const Json negative = Json::parse(R"({ "exists": "false" })");
	CHECK(!negative["exists"].toLooseBool(true));
}

ORBISLINK_TEST(escapes_and_unicode)
{
	const Json json = Json::parse(R"({"title":"Game \"rare\"ç\n"})");
	CHECK_EQ(json["title"].toString(), std::string("Game \"rare\"\xc3\xa7\n"));
	const std::string dumped = Json::fromString("a\"b\\c\n").dump();
	CHECK_EQ(dumped, std::string("\"a\\\"b\\\\c\\n\""));
}

ORBISLINK_TEST(arrays_and_round_trip)
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

ORBISLINK_TEST(invalid_input_does_not_crash)
{
	std::string error;
	const Json json = Json::parse("{ this is not json", &error);
	CHECK(json.isNull());
	CHECK(!error.empty());
	// Accessing missing keys returns default values.
	CHECK_EQ(json["whatever"].toInt(-1), -1);
	CHECK_EQ(json.at(3).toString("empty"), std::string("empty"));
}

TEST_MAIN()
