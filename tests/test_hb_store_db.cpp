// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A Homebrew Store's database (store.db) as the store page reads it: one
// row per app in "homebrews", with the columns PKG-Zone's has. Here it is
// a made-up one, built the same way.

#include "orbislink/qt/hb_store_db.h"
#include "test_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

using namespace orbislink;

namespace {

int counter = 0;

QString makeStore(const QStringList &rows)
{
	const QString path = QDir::temp().filePath(
		QStringLiteral("orbislink-test-store-%1-%2.db").arg(QCoreApplication::applicationPid()).arg(++counter));
	QFile::remove(path);
	{
		QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("make"));
		db.setDatabaseName(path);
		db.open();
		QSqlQuery q(db);
		q.exec(QStringLiteral(
			"CREATE TABLE `homebrews` (`pid`, `id`, `name`, `desc`, `image`, `package`, `version`, `picpath`, "
			"`desc_1`, `desc_2`, `ReviewStars`, `Size`, `Author`, `apptype`, `pv`, `main_icon_path`, "
			"`main_menu_pic`, `releaseddate`, `number_downloads`, `github`, `video`, `twitter`, \"md5\", "
			"\"ps5_md5\", \"ps5_Size\", \"ps5_version\", \"ps5_pv\", \"ps5_number_downloads\")"));
		for(const QString &row : rows)
			q.exec(row);
		db.close();
	}
	QSqlDatabase::removeDatabase(QStringLiteral("make"));
	return path;
}

const char *kInsert = "INSERT INTO homebrews (pid, id, name, `desc`, image, package, version, desc_1, desc_2, Size, "
					  "Author, apptype, pv, releaseddate, md5, ps5_md5, ps5_Size, ps5_version, ps5_pv) VALUES ";

} // namespace

ORBISLINK_TEST(reads_the_apps_of_a_store_database)
{
	const QString path = makeStore({
		QString::fromLatin1(kInsert) + QStringLiteral(
			"(1, 'TEST00001', 'Pixel Painter', 'Draws pixels\r', 'https://store.example.org/images/TEST00001/cover.png', "
			"'https://store.example.org/download.php?tid=TEST00001', '1.08', 'With a palette', '', '25.88 MB', "
			"'Someone', 'Utility', '5.05+ != 6.72', '2025-06-22', 'F67547C372E28F2FCF8A0E913E5E9C9F', 'abc', "
			"'59.94 MB', '1.14', 'PS5 SUPPORTED')"),
		QString::fromLatin1(kInsert) + QStringLiteral(
			"(2, 'test00002', 'Tiny Racer', 'A small game', '', 'https://store.example.org/download.php?tid=TEST00002', "
			"'1.00', NULL, NULL, '12.00 MB', 'Another', 'hb game', '4.55+, ps5_supported', '2024-01-02', NULL, NULL, "
			"NULL, NULL, NULL)"),
		// Not an app: no package, a title ID that is not one, or one already listed.
		QString::fromLatin1(kInsert) + QStringLiteral("(3, 'TEST00003', 'No package', '', '', '', '1.0', "
			"'', '', '', '', '', '', '', NULL, NULL, NULL, NULL, NULL)"),
		QString::fromLatin1(kInsert) + QStringLiteral("(4, '../etc', 'Bad id', '', '', 'https://x', '1.0', "
			"'', '', '', '', '', '', '', NULL, NULL, NULL, NULL, NULL)"),
		QString::fromLatin1(kInsert) + QStringLiteral("(5, 'TEST00001', 'Twice', '', '', 'https://x', '1.0', "
			"'', '', '', '', '', '', '', NULL, NULL, NULL, NULL, NULL)"),
	});
	std::vector<store::HbStoreApp> apps;
	QString error;
	CHECK(readHbStoreDatabase(path, &apps, &error));
	CHECK_EQ(apps.size(), static_cast<size_t>(2));
	const store::HbStoreApp &painter = apps[0];
	CHECK_EQ(painter.titleId, std::string("TEST00001"));
	CHECK_EQ(painter.name, std::string("Pixel Painter"));
	CHECK_EQ(painter.description, std::string("Draws pixels\nWith a palette"));
	CHECK_EQ(painter.version, std::string("1.08"));
	CHECK_EQ(painter.sizeText, std::string("25.88 MB"));
	CHECK_EQ(painter.md5, std::string("f67547c372e28f2fcf8a0e913e5e9c9f"));
	CHECK_EQ(painter.kind(), std::string("tool"));
	CHECK(painter.forPs4());
	CHECK(painter.forPs5());
	// Its PS5 build is another package: not the one at its address.
	CHECK(!painter.ps5PackageIsThisOne());
	CHECK_EQ(painter.ps4Firmware(), std::string("5.05+ != 6.72"));

	const store::HbStoreApp &racer = apps[1];
	CHECK_EQ(racer.titleId, std::string("TEST00002"));
	CHECK_EQ(racer.kind(), std::string("game"));
	CHECK(racer.md5.empty());
	CHECK(racer.ps5PackageIsThisOne());
	CHECK_EQ(racer.ps4Firmware(), std::string("4.55+"));
	QFile::remove(path);
}

ORBISLINK_TEST(a_file_that_is_not_a_store_database_is_refused)
{
	const QString path = QDir::temp().filePath(QStringLiteral("orbislink-test-store-not.db"));
	{
		QFile file(path);
		file.open(QIODevice::WriteOnly);
		file.write("<html>500 Server Error</html>");
	}
	std::vector<store::HbStoreApp> apps;
	QString error;
	CHECK(!readHbStoreDatabase(path, &apps, &error));
	CHECK(!error.isEmpty());
	CHECK(apps.empty());
	QFile::remove(path);

	// A database, but not a store's.
	const QString other = QDir::temp().filePath(QStringLiteral("orbislink-test-store-other.db"));
	QFile::remove(other);
	{
		QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("other"));
		db.setDatabaseName(other);
		db.open();
		QSqlQuery(db).exec(QStringLiteral("CREATE TABLE savedata (id INTEGER)"));
		db.close();
	}
	QSqlDatabase::removeDatabase(QStringLiteral("other"));
	CHECK(!readHbStoreDatabase(other, &apps, &error));
	QFile::remove(other);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	return orbislink_test::runAll();
}
