// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The Files tab's list: folders first, then the order chosen, narrowed by
// the filter, and the dates in one form.
#include "orbislink/qt/ftp_model.h"
#include "test_support.h"

#include <vector>

using namespace orbislink;

namespace {

FtpEntry entry(const std::string &name, bool folder, int64_t size, const std::string &modified)
{
	FtpEntry item;
	item.name = name;
	item.path = "/data/test/" + name;
	item.isDirectory = folder;
	item.size = size;
	item.modified = modified;
	item.permissions = folder ? "drwxr-xr-x" : "-rw-r--r--";
	return item;
}

std::vector<FtpEntry> folder()
{
	return {
		entry("zeta.txt", false, 4, "Mar 05 2024"),
		entry("Beta.bin", false, 300000, "Jan 10 2025"),
		entry("sub", true, 0, "Feb 01 2025"),
		entry("alpha.ini", false, 22, "Jan 10 2025"),
		entry("Apps", true, 0, "Dec 24 2023"),
	};
}

QString nameAt(const FtpModel &model, int row)
{
	return model.get(row).value(QStringLiteral("name")).toString();
}

} // namespace

ORBISLINK_TEST(by_name_folders_first_in_any_case)
{
	FtpModel model;
	model.setEntries(folder());
	CHECK_EQ(model.rowCount(), 5);
	CHECK(nameAt(model, 0) == QStringLiteral("Apps"));
	CHECK(nameAt(model, 1) == QStringLiteral("sub"));
	CHECK(nameAt(model, 2) == QStringLiteral("alpha.ini"));
	CHECK(nameAt(model, 3) == QStringLiteral("Beta.bin"));
	CHECK(nameAt(model, 4) == QStringLiteral("zeta.txt"));
	// The same key again turns it round; the folders stay first.
	model.sortBy(QStringLiteral("name"));
	CHECK(!model.sortAscending());
	CHECK(nameAt(model, 0) == QStringLiteral("sub"));
	CHECK(nameAt(model, 2) == QStringLiteral("zeta.txt"));
}

ORBISLINK_TEST(by_size_and_date_the_biggest_and_newest_first_ties_by_name)
{
	FtpModel model;
	model.setEntries(folder());
	model.sortBy(QStringLiteral("size"));
	CHECK(model.sortKey() == QStringLiteral("size"));
	CHECK(!model.sortAscending());
	CHECK(nameAt(model, 2) == QStringLiteral("Beta.bin"));
	CHECK(nameAt(model, 3) == QStringLiteral("alpha.ini"));
	CHECK(nameAt(model, 4) == QStringLiteral("zeta.txt"));

	model.sortBy(QStringLiteral("date"));
	CHECK(nameAt(model, 0) == QStringLiteral("sub"));
	CHECK(nameAt(model, 1) == QStringLiteral("Apps"));
	// alpha.ini and Beta.bin changed the same day: A to Z between them.
	CHECK(nameAt(model, 2) == QStringLiteral("alpha.ini"));
	CHECK(nameAt(model, 3) == QStringLiteral("Beta.bin"));
	CHECK(nameAt(model, 4) == QStringLiteral("zeta.txt"));

	model.setSort(QStringLiteral("date"), true);
	CHECK(nameAt(model, 2) == QStringLiteral("zeta.txt"));
	CHECK(nameAt(model, 3) == QStringLiteral("alpha.ini"));
	// Anything else is by name.
	model.setSort(QStringLiteral("colour"), true);
	CHECK(model.sortKey() == QStringLiteral("name"));
}

ORBISLINK_TEST(the_filter_narrows_what_is_shown_not_what_the_folder_has)
{
	FtpModel model;
	model.setEntries(folder());
	model.setFilter(QStringLiteral("  A "));
	CHECK(model.filter() == QStringLiteral("A"));
	CHECK_EQ(model.rowCount(), 4); // Apps, alpha.ini, Beta.bin, zeta.txt
	CHECK_EQ(model.total(), 5);
	CHECK_EQ(model.indexOfPath(QStringLiteral("/data/test/sub")), -1);
	CHECK(model.containsPath(QStringLiteral("/data/test/sub")));
	model.setFilter(QStringLiteral("nothing"));
	CHECK_EQ(model.rowCount(), 0);
	model.setFilter(QString());
	CHECK_EQ(model.rowCount(), 5);
	// A new listing keeps the order and the filter.
	model.setFilter(QStringLiteral("bin"));
	model.setEntries(folder());
	CHECK_EQ(model.rowCount(), 1);
	CHECK(nameAt(model, 0) == QStringLiteral("Beta.bin"));
}

ORBISLINK_TEST(a_row_says_its_date_in_one_form_and_its_permissions)
{
	FtpModel model;
	model.setEntries({ entry("old.txt", false, 1, "Mar 05 2024"), entry("odd.txt", false, 1, "yesterday") });
	const QVariantMap first = model.get(model.indexOfPath(QStringLiteral("/data/test/old.txt")));
	CHECK(first.value(QStringLiteral("modified")).toString() == QStringLiteral("2024-03-05 00:00"));
	CHECK(first.value(QStringLiteral("permissions")).toString() == QStringLiteral("-rw-r--r--"));
	// A date it cannot read is shown as it came.
	const QVariantMap second = model.get(model.indexOfPath(QStringLiteral("/data/test/odd.txt")));
	CHECK(second.value(QStringLiteral("modified")).toString() == QStringLiteral("yesterday"));
	CHECK(model.get(99).isEmpty());
}

TEST_MAIN()
