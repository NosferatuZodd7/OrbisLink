// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Cheats and game patches: the repository's lists (and the oddly named
// files in them), what a cheat file says in each format, a patch XML, the
// switch GoldHEN keeps for each patch, and etaHEN's lists of files.

#include "orbislink/cheats/cheats.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace orbislink::cheats;

namespace {

const char *kJsonList =
	"CUSA00207_01.09.json=Bloodborne\xe2\x84\xa2\r\n"
	"CUSA02882_01.00_bh1hd.elf_2.json=Resident Evil Origins Collection\n"
	"CUSA00207_01.03.json=Bloodborne\n"
	"SLUS20873_01.00.json=Silent Hill-4-The Room\n"
	"not a line\n"
	"../evil_01.00.json=Out of the folder\n"
	"CUSA0020_01.00.json=Short title ID\n"
	"\n";

const char *kMc4List =
	"CUSA27973_01.03.mc4.xml=Hades\n"
	"CUSA29735_01_03.mc4.xml=Miraculous - Rise of the Sphinx\n";

const char *kShnList = "CUSA15436_1.05.shn=Kingdom Come Deliverance Royal Edition\n"
					   "CUSA00207_01.09.shn=Bloodborne\n";

const char *kCheatJson = R"x({
  "name": "Bloodborne",
  "id": "CUSA00207",
  "version": "01.09",
  "process": "eboot.bin",
  "master": { "memory": [ { "offset": "50DB810", "on": "48A3" } ] },
  "mods": [
    { "name": "Infinite  BloodEcho", "type": "checkbox", "memory": [ { "offset": "50DB800", "on": "B9", "off": "00" } ] },
    { "name": "Infinite Health", "type": "checkbox", "memory": [] }
  ],
  "credits": [ "Shiningami", "someone else" ]
})x";

const char *kCheatShn = R"x(<?xml version="1.0" encoding="utf-8"?>
<Trainer xmlns:xsd="http://www.w3.org/2001/XMLSchema" Game="Darksiders III" Moder="Saso" Cusa="CUSA08798" Version="01.11" Process="eboot.bin">
  <Genres Name="Adventure" />
  <Items />
  <Cheat Text="Infinite Health">
    <Cheatline>
      <Offset>48B9500</Offset>
      <ValueOn>80-3B-00</ValueOn>
    </Cheatline>
  </Cheat>
  <Cheat Text="Rock &amp; Roll &#x2764;" Description="Max &quot;souls&quot;"/>
</Trainer>)x";

const char *kPatchXml = R"x(<?xml version="1.0" encoding="utf-8"?>
<!-- File generated from: Bloodborne-Orbis.xml -->
<Patch>
    <TitleID>
        <ID>CUSA00900</ID>
        <ID>CUSA00207</ID>
    </TitleID>
    <Metadata Title="Bloodborne"
              Name="Skip Intro"
              Author="illusion"
              PatchVer="1.0"
              AppVer="01.09"
              AppElf="eboot.bin">
        <PatchList>
            <Line Type="bytes32" Address="0x04d99138" Value="0x00000000"/>
            <Line Type="bytes32" Address="0x04d99154" Value="0x00000000"/>
        </PatchList>
    </Metadata>
    <Metadata Title="Bloodborne"
              Name="30 FPS Fix (Proper Frame Pacing)"
              Note="Caps framerate to 30 &amp; paces frames.\nNo softlock."
              Author="illusion, Lance McDonald"
              PatchVer="1.0"
              AppVer="all"
              AppElf="eboot.bin">
        <PatchList>
            <Line Type="float32" Address="0x02434883" Value="0.016666667"/>
        </PatchList>
    </Metadata>
</Patch>)x";

} // namespace

ORBISLINK_TEST(reads_the_repository_lists)
{
	const std::vector<CheatListing> json = parseCheatList(kJsonList, Format::Json);
	CHECK_EQ(json.size(), size_t(4)); // the bad lines are left out
	CHECK_EQ(json[0].titleId, std::string("CUSA00207"));
	CHECK_EQ(json[0].version, std::string("01.09"));
	CHECK_EQ(json[0].variant, std::string());
	CHECK_EQ(json[0].game, std::string("Bloodborne\xe2\x84\xa2"));
	CHECK_EQ(json[1].variant, std::string("bh1hd.elf_2"));
	CHECK_EQ(json[1].consoleName(), std::string("CUSA02882_01.00_bh1hd.elf_2.json"));
	CHECK_EQ(json[3].titleId, std::string("SLUS20873"));

	// A version written another way is the version the console matches.
	const std::vector<CheatListing> mc4 = parseCheatList(kMc4List, Format::Mc4);
	CHECK_EQ(mc4.size(), size_t(2));
	CHECK_EQ(mc4[1].version, std::string("01.03"));
	CHECK_EQ(mc4[1].consoleName(), std::string("CUSA29735_01.03.mc4"));
	// The names come from the readable .mc4.xml; the console gets the .mc4.
	CHECK_EQ(mc4[0].readablePath(), std::string("mc4/CUSA27973_01.03.mc4.xml"));
	CHECK_EQ(mc4[0].loadablePath(), std::string("mc4/CUSA27973_01.03.mc4"));
	// A PS5 game's version, with one digit too few in front.
	const std::vector<CheatListing> ps5 = parseCheatList("PPSA01628_1.026.000.json=Call of Duty\n", Format::Json);
	CHECK_EQ(ps5.size(), size_t(1));
	CHECK_EQ(ps5[0].version, std::string("01.026.000"));
	CHECK_EQ(ps5[0].consoleName(), std::string("PPSA01628_01.026.000.json"));
	const std::vector<CheatListing> shn = parseCheatList(kShnList, Format::Shn);
	CHECK_EQ(shn[0].version, std::string("01.05"));
	CHECK_EQ(shn[0].consoleName(), std::string("CUSA15436_01.05.shn"));
	CHECK_EQ(shn[0].loadablePath(), std::string("shn/CUSA15436_1.05.shn"));
}

ORBISLINK_TEST(a_titles_cheats_for_its_version_first)
{
	std::vector<CheatListing> all = parseCheatList(kJsonList, Format::Json);
	for(const CheatListing &listing : parseCheatList(kShnList, Format::Shn))
		all.push_back(listing);
	size_t exact = 99;
	const std::vector<CheatListing> mine = cheatsFor(all, "cusa00207", "01.09", &exact);
	CHECK_EQ(mine.size(), size_t(3));
	CHECK_EQ(exact, size_t(2));
	CHECK(mine[0].format == Format::Json);
	CHECK(mine[1].format == Format::Shn);
	CHECK_EQ(mine[2].version, std::string("01.03"));
	// Another version installed: none of them fits it.
	cheatsFor(all, "CUSA00207", "1.10", &exact);
	CHECK_EQ(exact, size_t(0));
	CHECK_EQ(normalizeVersion("1.10"), std::string("01.10"));
	CHECK_EQ(normalizeVersion("01.000.000"), std::string("01.000.000"));
	CHECK_EQ(normalizeVersion("1.026.000"), std::string("01.026.000"));
	CHECK_EQ(normalizeVersion("v1"), std::string());
}

ORBISLINK_TEST(reads_what_a_cheat_file_has)
{
	const CheatFile json = parseCheatFile(kCheatJson, Format::Json);
	CHECK(json.ok);
	CHECK_EQ(json.game, std::string("Bloodborne"));
	CHECK_EQ(json.version, std::string("01.09"));
	CHECK_EQ(json.author, std::string("Shiningami, someone else"));
	CHECK_EQ(json.cheats.size(), size_t(2));
	CHECK_EQ(json.cheats[0].name, std::string("Infinite BloodEcho"));

	const CheatFile shn = parseCheatFile(kCheatShn, Format::Shn);
	CHECK(shn.ok);
	CHECK_EQ(shn.game, std::string("Darksiders III"));
	CHECK_EQ(shn.author, std::string("Saso"));
	CHECK_EQ(shn.process, std::string("eboot.bin"));
	CHECK_EQ(shn.cheats.size(), size_t(2));
	CHECK_EQ(shn.cheats[1].name, std::string("Rock & Roll \xe2\x9d\xa4"));
	CHECK_EQ(shn.cheats[1].description, std::string("Max \"souls\""));

	CHECK(!parseCheatFile("<html>404</html>", Format::Json).ok);
	CHECK(!parseCheatFile("404: Not Found", Format::Shn).ok);
}

ORBISLINK_TEST(reads_a_patch_file)
{
	const PatchFile file = parsePatchFile(kPatchXml);
	CHECK(file.ok);
	CHECK_EQ(file.titleIds.size(), size_t(2));
	CHECK_EQ(file.titleIds[1], std::string("CUSA00207"));
	CHECK_EQ(file.patches.size(), size_t(2));
	CHECK_EQ(file.patches[0].name, std::string("Skip Intro"));
	CHECK_EQ(file.patches[0].lines, 2);
	CHECK_EQ(file.patches[1].lines, 1);
	CHECK_EQ(file.patches[1].note, std::string("Caps framerate to 30 & paces frames.\nNo softlock."));
	CHECK(file.patches[0].appliesTo("01.09"));
	CHECK(!file.patches[0].appliesTo("01.08"));
	CHECK(file.patches[1].appliesTo("01.08")); // "all"
}

ORBISLINK_TEST(the_switch_goldhen_keeps_for_each_patch)
{
	// Worked out with game_patch's own djb2_hash and patch_hash_calc,
	// built for x86-64 as the console's is.
	const PatchFile file = parsePatchFile(kPatchXml);
	CHECK_EQ(patchHash(file.patches[0], "CUSA00207"), uint64_t(0x5584d6061e774ef1ULL));
	CHECK_EQ(patchSettingsPath(patchHash(file.patches[0], "CUSA00207")),
		std::string("/data/GoldHEN/patches/settings/0x5584d6061e774ef1.txt"));
	// Bytes past 0x7F count as negative chars.
	CHECK_EQ(patchHash("Marvel\xe2\x80\x99s Spider-Man", "60 FPS", "01.19", patchXmlPath("CUSA02299"), "eboot.bin"),
		uint64_t(0x554cd0f2af41dab8ULL));
	// Only the first 255 bytes count.
	std::string longName;
	for(int i = 0; i < 299; ++i)
		longName += static_cast<char>('a' + i % 26);
	CHECK_EQ(patchHash("Long", longName, "01.00", patchXmlPath("CUSA00001"), "eboot.bin"),
		uint64_t(0xf25987c2556c1435ULL));
	CHECK(patchSwitchOn("1\n"));
	CHECK(!patchSwitchOn("0\n"));
	CHECK(!patchSwitchOn(""));
}

ORBISLINK_TEST(game_patch_loaded_by_plugins_ini)
{
	CHECK(patchPluginLoads("[default]\n/data/GoldHEN/plugins/game_patch.prx\n", "CUSA00207"));
	CHECK(patchPluginLoads("[default]\r\n/data/GoldHEN/plugins/game_patch.prx=true\r\n", "CUSA00207"));
	CHECK(!patchPluginLoads("[default]\n/data/GoldHEN/plugins/game_patch.prx=false\n", "CUSA00207"));
	CHECK(!patchPluginLoads("[default]\n;/data/GoldHEN/plugins/game_patch.prx\n", "CUSA00207"));
	CHECK(patchPluginLoads("[CUSA00207]\n/data/GoldHEN/plugins/game_patch.prx\n", "CUSA00207"));
	CHECK(!patchPluginLoads("[CUSA00900]\n/data/GoldHEN/plugins/game_patch.prx\n", "CUSA00207"));
	CHECK(!patchPluginLoads("", "CUSA00207"));
}

ORBISLINK_TEST(the_consoles_folders_and_etahen_lists)
{
	CHECK_EQ(cheatFolder(Console::GoldHen, Format::Json), std::string("/data/GoldHEN/cheats/json"));
	CHECK_EQ(cheatFolder(Console::EtaHen, Format::Mc4), std::string("/data/etaHEN/cheats/mc4"));
	CHECK_EQ(etaHenList(Format::Shn), std::string("/data/etaHEN/cheats/shn.txt"));

	std::string list = "CUSA00001_01.00.json=Other";
	list = etaHenListAdd(list, "CUSA00207_01.09.json", "Bloodborne");
	CHECK_EQ(list, std::string("CUSA00001_01.00.json=Other\nCUSA00207_01.09.json=Bloodborne\n"));
	// Once.
	CHECK_EQ(etaHenListAdd(list, "CUSA00207_01.09.json", "Bloodborne"), list);
	CHECK_EQ(etaHenListRemove(list, "CUSA00207_01.09.json"), std::string("CUSA00001_01.00.json=Other\n"));
	CHECK_EQ(etaHenListRemove("CUSA00207_01.09.json=Bloodborne\n", "CUSA00207_01.09.json"), std::string());
	CHECK_EQ(etaHenListRemove(list, "missing.json"), list);
	CHECK_EQ(etaHenListAdd("", "CUSA00207_01.09.json", ""), std::string("CUSA00207_01.09.json=CUSA00207_01\n"));

	CHECK(cheatFileIsFor("CUSA00207_01.09.json", "CUSA00207", "01.09"));
	CHECK(cheatFileIsFor("CUSA00207_01.09_2.shn", "cusa00207", ""));
	CHECK(!cheatFileIsFor("CUSA00207_01.09.json", "CUSA00207", "01.03"));
	CHECK(!cheatFileIsFor("CUSA00900_01.09.mc4", "CUSA00207", ""));
	CHECK(!cheatFileIsFor("readme.txt", "CUSA00207", ""));
}

ORBISLINK_TEST(a_little_xml)
{
	const std::vector<XmlElement> elements = xmlElements("<a x='1' y = \"two &lt;2&gt;\"><b/>text<c z=3>inner</c></a>");
	CHECK_EQ(elements.size(), size_t(3));
	CHECK_EQ(elements[0].attribute("y"), std::string("two <2>"));
	CHECK_EQ(elements[1].name, std::string("b"));
	CHECK_EQ(elements[1].depth, 1);
	CHECK_EQ(elements[1].text, std::string("text"));
	CHECK_EQ(elements[2].attribute("z"), std::string("3"));
	CHECK_EQ(elements[2].text, std::string("inner"));
	CHECK_EQ(xmlDecode("&#233;&#xE9;&bogus;&"), std::string("\xc3\xa9\xc3\xa9&bogus;&"));
	// Cut short: no element is made up.
	CHECK(xmlElements("<a x=\"unterminated").size() <= 1);
}

TEST_MAIN()
