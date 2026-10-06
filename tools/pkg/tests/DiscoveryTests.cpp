#include "gameDiscovery.h"
#include "gameContent.h"
#include "common/archive.h"
#include "common/archiveReader.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <array>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    Check(argc == 2, "pass a test directory containing fixture.staged");
    QDir root(QString::fromLocal8Bit(argv[1]));
    const auto staged = root.filePath("fixture.staged");
    const auto pkg = root.filePath(QString::fromUtf8("new game Ω.PKG"));
    Check(QFileInfo::exists(staged), "missing sparse fixture");
    Check(DiscoverGames({root.absolutePath()}).isEmpty(), "initial scan is not empty");
    { QFile partial(pkg); Check(partial.open(QIODevice::WriteOnly), "create incomplete package"); partial.write("partial"); }
    auto games = DiscoverGames({root.absolutePath()});
    Check(games.size() == 1 && games[0].archive, "new uppercase PKG not found in same process");
    Check(!Common::OpenArchive(GameContent::ToPath(pkg)), "partial package unexpectedly opened");
    Check(QFile::remove(pkg) && QFile::rename(staged, pkg), "complete package copy");
    Common::InvalidateArchiveCache();
    games = DiscoverGames({root.absolutePath(), root.absolutePath()});
    Check(games.size() == 1, "refresh failed or overlapping roots duplicated package");
    auto before = Common::OpenArchive(GameContent::ToPath(pkg));
    Check(before && before->Find("eboot.bin").has_value(), "completed package cannot be retried");
    Common::InvalidateArchiveCache();
    auto after = Common::OpenArchive(GameContent::ToPath(pkg));
    Check(after && after != before, "refresh reused retained metadata reader");
    std::array<unsigned char,4> bytes{};
    const auto boot = before->Find("eboot.bin");
    Check(before->Read(boot->id, 0, bytes.size(), bytes.data()) == 4 && bytes[0] == 0x7f,
          "invalidating metadata broke an existing open reader");
    before.reset(); after.reset(); Common::InvalidateArchiveCache();
    auto preview = GameContent::ReadArchivePreview(pkg, true);
    Check(preview.has_value() && preview->metadata.contains("PPSA00001"), "preview metadata not read");
    Common::InvalidateArchiveCache();
    const auto helper = qgetenv("KYTY_PKG_HELPER");
    const bool had_helper = qEnvironmentVariableIsSet("KYTY_PKG_HELPER");
    qputenv("KYTY_PKG_HELPER", root.filePath("missing-helper.dll").toUtf8());
    Check(GameContent::ReadArchivePreview(pkg).has_value(), "warm preview started the missing helper");
    Check(!GameContent::ReadArchivePreview(pkg, true).has_value(), "forced refresh reused cached preview");
    Check(!GameContent::ReadArchivePreview(pkg).has_value(), "failed refresh preserved stale cache");
    if (had_helper) qputenv("KYTY_PKG_HELPER", helper); else qunsetenv("KYTY_PKG_HELPER");
    Common::InvalidateArchiveCache();
    Check(GameContent::ReadArchivePreview(pkg, true).has_value(), "preview retry failed");
    Common::InvalidateArchiveCache();
    qputenv("KYTY_PKG_HELPER", root.filePath("missing-helper.dll").toUtf8());
    { QFile changed(pkg); Check(changed.open(QIODevice::Append), "open package for size change"); changed.write("x"); }
    Check(!GameContent::ReadArchivePreview(pkg).has_value(), "changed package reused stale preview");
    // Remove the old preview and restore the helper for the remaining discovery tests.
    (void)GameContent::ReadArchivePreview(pkg, true);
    if (had_helper) qputenv("KYTY_PKG_HELPER", helper); else qunsetenv("KYTY_PKG_HELPER");
    Common::InvalidateArchiveCache();
    const auto fpkg = root.filePath("renamed.fpkg");
    Check(QFile::rename(pkg, fpkg), "rename package");
    games = DiscoverGames({root.absolutePath()});
    Check(games.size() == 1 && games[0].path == QFileInfo(fpkg).absoluteFilePath(), "renamed fpkg not refreshed");
    Check(QFile::remove(fpkg), "remove package");
    Check(DiscoverGames({root.absolutePath()}).isEmpty(), "removed package remains listed");
    Check(root.mkpath("nested/extracted"), "make extracted game");
    { QFile bootfile(root.filePath("nested/extracted/eboot.bin")); Check(bootfile.open(QIODevice::WriteOnly), "create eboot"); }
    games = DiscoverGames({root.absolutePath()});
    Check(games.size() == 1 && !games[0].archive, "extracted game regression");
    std::cout << "Discovery tests passed: live add, incomplete-copy retry, duplicates, retained readers, preview cache, forced refresh, size invalidation, rename, removal, extracted games\n";
}
