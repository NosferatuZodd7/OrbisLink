// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Builds a fake PKG from the command line. Mostly for the tests, which
// compare its output with other tools byte for byte.
//
//   orbislink-fpkg OUT.pkg CONTENT_ID VOLUME_TIME C_DATE TARGET=SOURCE...
//
// VOLUME_TIME is in seconds since 1970, C_DATE is yyyyMMdd.
#include "orbislink/fpkg/pkg_builder.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char **argv)
{
	if(argc < 6)
	{
		std::fprintf(stderr, "usage: %s OUT.pkg CONTENT_ID VOLUME_TIME C_DATE TARGET=SOURCE...\n",
			argv[0]);
		return 2;
	}
	orbislink::fpkg::PkgRequest request;
	request.contentId = argv[2];
	request.volumeTime = std::strtoll(argv[3], nullptr, 10);
	request.creationDate = argv[4];
	for(int i = 5; i < argc; ++i)
	{
		const std::string arg = argv[i];
		const size_t eq = arg.find('=');
		if(eq == std::string::npos)
		{
			std::fprintf(stderr, "expected TARGET=SOURCE, got %s\n", argv[i]);
			return 2;
		}
		orbislink::fpkg::PkgSource source;
		source.targetPath = arg.substr(0, eq);
		source.sourcePath = arg.substr(eq + 1);
		request.files.push_back(source);
	}
	std::string error;
	int lastPercent = -1;
	const bool ok = orbislink::fpkg::buildFakePkg(request, argv[1],
		[&](const std::string &stage, uint64_t done, uint64_t total) {
			const int percent = total ? static_cast<int>(done * 100 / total) : 0;
			if(stage == "image" && percent / 10 != lastPercent / 10)
			{
				lastPercent = percent;
				std::fprintf(stderr, "image %d%%\n", percent);
			}
			return true;
		},
		&error);
	if(!ok)
	{
		std::fprintf(stderr, "error: %s\n", error.c_str());
		return 1;
	}
	return 0;
}
