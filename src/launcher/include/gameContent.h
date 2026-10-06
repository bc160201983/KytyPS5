#ifndef GAME_CONTENT_H
#define GAME_CONTENT_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace GameContent {

inline constexpr uint64_t MaxMetadataSize      = uint64_t {1} << 20u;
inline constexpr uint64_t MaxImageSize         = uint64_t {32} << 20u;
inline constexpr uint64_t MaxTrophyPackageSize = uint64_t {128} << 20u;

struct ArchivePreview {
    QByteArray metadata;
    QByteArray icon;
};
// Cached launcher-only data. Game reads always use the live archive reader.
[[nodiscard]] std::optional<ArchivePreview> ReadArchivePreview(const QString& base, bool refresh = false);

[[nodiscard]] std::filesystem::path ToPath(const QString& path);
[[nodiscard]] QString               FromPath(const std::filesystem::path& path);
[[nodiscard]] bool                  IsArchive(const QString& base);
[[nodiscard]] std::filesystem::path Resolve(const QString& base, const QString& relative);
[[nodiscard]] bool                  FileExists(const QString& base, const QString& relative);
[[nodiscard]] QByteArray            ReadPath(const std::filesystem::path& path, uint64_t max_size);
[[nodiscard]] QByteArray  ReadFile(const QString& base, const QString& relative, uint64_t max_size);
[[nodiscard]] QStringList ListFiles(const QString& base, const QString& relative);

} // namespace GameContent

#endif // GAME_CONTENT_H
