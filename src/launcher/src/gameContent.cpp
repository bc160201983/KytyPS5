#include "gameContent.h"

#include "common/archive.h"
#include "common/archiveReader.h"
#include "common/file.h"
#include "common/stringUtils.h"

#include <QFileInfo>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <limits>

namespace GameContent {

std::filesystem::path ToPath(const QString& path) {
#if defined(_WIN32)
	return std::filesystem::path(path.toStdWString());
#else
	return std::filesystem::path(path.toStdString());
#endif
}

QString FromPath(const std::filesystem::path& path) {
#if defined(_WIN32)
	return QString::fromStdWString(path.wstring());
#else
	return QString::fromStdString(Common::PathToString(path));
#endif
}

bool IsArchive(const QString& base) {
	const QFileInfo info(base);
	return info.isFile() && Common::IsSupportedArchive(ToPath(base));
}

std::filesystem::path Resolve(const QString& base, const QString& relative) {
	const auto root = ToPath(base);
	return IsArchive(base) ? Common::MakeArchivePath(root, ToPath(relative))
	                       : root / ToPath(relative);
}

bool FileExists(const QString& base, const QString& relative) {
	return Common::File::IsFileExisting(Resolve(base, relative));
}

std::optional<ArchivePreview> ReadArchivePreview(const QString& base, bool refresh) {
	const QFileInfo before(base);
	if (!before.isFile() || !IsArchive(base)) return std::nullopt;
	const auto canonical = before.canonicalFilePath();
	if (canonical.isEmpty()) return std::nullopt;
	const auto size = QString::number(before.size());
	const auto modified = QString::number(before.lastModified().toMSecsSinceEpoch());
	const auto key = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex();
	const auto cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	const auto cache_dir = cache_root.isEmpty() ? QString() : cache_root + QStringLiteral("/archive-previews-v1");
	const auto cache_path = cache_dir + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".json");
	if (!cache_dir.isEmpty() && refresh) QFile::remove(cache_path);
	if (!cache_dir.isEmpty() && !refresh) {
		QFile cache(cache_path);
		// Bound the JSON/base64 input before allocating or parsing it.
		if (cache.open(QIODevice::ReadOnly) && cache.size() <= 48 * 1024 * 1024) {
			const auto doc = QJsonDocument::fromJson(cache.readAll());
			const auto object = doc.object();
			if (object.value("path").toString() == canonical &&
				object.value("size").toString() == size &&
				object.value("modified").toString() == modified &&
				object.value("metadata").isString() && object.value("icon").isString()) {
				const auto metadata = QByteArray::fromBase64Encoding(object.value("metadata").toString().toLatin1(),
				    QByteArray::AbortOnBase64DecodingErrors);
				const auto icon = QByteArray::fromBase64Encoding(object.value("icon").toString().toLatin1(),
				    QByteArray::AbortOnBase64DecodingErrors);
				if (metadata && icon && uint64_t(metadata.decoded.size()) <= MaxMetadataSize &&
					uint64_t(icon.decoded.size()) <= MaxImageSize)
					return ArchivePreview{metadata.decoded, icon.decoded};
			}
		}
	}
	// Keep one reader alive for the whole preview; never persist failed mounts.
	const auto reader = Common::OpenArchive(ToPath(base));
	if (!reader) return std::nullopt;
	const auto boot = reader->Find("eboot.bin");
	if (!boot || !boot->is_file) return std::nullopt;
	const auto read = [&](std::string_view name, uint64_t limit) -> std::optional<QByteArray> {
		const auto entry = reader->Find(name);
		if (!entry) return QByteArray{};
		if (!entry->is_file || entry->size > limit) return std::nullopt;
		QByteArray bytes(static_cast<int>(entry->size), Qt::Uninitialized);
		for (uint64_t offset = 0; offset < entry->size;) {
			const auto count = static_cast<uint32_t>(std::min<uint64_t>(entry->size - offset, 1024u * 1024u));
			if (reader->Read(entry->id, offset, count, bytes.data() + offset) != count) return std::nullopt;
			offset += count;
		}
		return bytes;
	};
	const auto metadata = read("sce_sys/param.json", MaxMetadataSize);
	const auto icon = read("sce_sys/icon0.png", MaxImageSize);
	if (!metadata || !icon) return std::nullopt;
	ArchivePreview preview{*metadata, *icon};
	const QFileInfo after(base);
	if (after.size() == before.size() && after.lastModified() == before.lastModified() &&
		after.canonicalFilePath() == canonical && !cache_dir.isEmpty() && QDir().mkpath(cache_dir)) {
		QJsonObject object{{"path", canonical}, {"size", size}, {"modified", modified},
						   {"metadata", QString::fromLatin1(preview.metadata.toBase64())},
						   {"icon", QString::fromLatin1(preview.icon.toBase64())}};
		QSaveFile cache(cache_path);
		if (cache.open(QIODevice::WriteOnly)) {
			const auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
			if (cache.write(data) == data.size()) cache.commit();
		}
	}
	return preview;
}

QByteArray ReadFile(const QString& base, const QString& relative, uint64_t max_size) {
	if (relative == QStringLiteral("sce_sys/icon0.png") && IsArchive(base)) {
		const auto preview = ReadArchivePreview(base);
		return preview && uint64_t(preview->icon.size()) <= max_size ? preview->icon : QByteArray{};
	}
	return ReadPath(Resolve(base, relative), max_size);
}

QByteArray ReadPath(const std::filesystem::path& path, uint64_t max_size) {
	Common::File file(path, Common::File::Mode::Read);
	if (file.IsInvalid()) {
		return {};
	}

	const auto size = file.Size();
	if (size > max_size || size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
		return {};
	}

	QByteArray data(static_cast<int>(size), Qt::Uninitialized);
	uint32_t   bytes_read = 0;
	file.Read(data.data(), static_cast<uint32_t>(size), &bytes_read);
	return bytes_read == size ? data : QByteArray {};
}

QStringList ListFiles(const QString& base, const QString& relative) {
	const auto  directory = Resolve(base, relative);
	const bool  archive   = Common::IsArchivePath(directory);
	QStringList result;
	for (const auto& entry: Common::File::GetDirEntries(directory)) {
		if (entry.is_file) {
			const auto path = FromPath(directory / Common::PathFromUtf8(entry.name));
			if (archive || !QFileInfo(path).isSymLink()) {
				result.append(path);
			}
		}
	}
	return result;
}

} // namespace GameContent
