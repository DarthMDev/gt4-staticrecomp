// Written from the TASK-026 specification (docs/hw-renderer-spec), chapters 11.2, 11.3.5, 11.5.
// Clean room (decision 0069). Copyright (c) 2026 GT4Recomp contributors, MIT.
#include "hw_gs_replace/packs.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace hw_gs_replace {
namespace {

namespace fs = std::filesystem;

std::string Lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return text;
}

bool HasZipExtension(const fs::path& path) {
  return Lower(path.extension().string()) == ".zip";
}

// Regular files under `directory`, recursively, hidden files included, sorted by path text.
std::vector<fs::path> SortedFilesRecursive(const fs::path& directory, std::vector<std::string>& messages) {
  std::vector<fs::path> files;
  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    return files;
  }
  for (fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied,
                                          error),
       end;
       it != end; it.increment(error)) {
    if (error) {
      messages.push_back("pack scan stopped under " + directory.string() + ": " + error.message());
      break;
    }
    if (it->is_regular_file(error)) {
      files.push_back(it->path());
    }
  }
  std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
    return a.generic_string() < b.generic_string();
  });
  return files;
}

// Zip archives directly inside `directory` (the chapter 11.3.5 folder rule uses recursion for
// loose files and lists archives where they are found).
std::vector<fs::path> SortedZipsRecursive(const fs::path& directory, std::vector<std::string>& messages) {
  std::vector<fs::path> zips;
  for (const fs::path& file : SortedFilesRecursive(directory, messages)) {
    if (HasZipExtension(file)) {
      zips.push_back(file);
    }
  }
  return zips;
}

}  // namespace

std::vector<std::string> UnsupportedArchiveReader::EntryBasenames(const fs::path& archive,
                                                                  std::string& error) const {
  error = "zip support is not built into this HWR core: " + archive.string();
  return {};
}

bool UnsupportedArchiveReader::ReadEntry(const fs::path& archive, std::string_view entryPath,
                                         std::vector<uint8_t>& bytes, std::string& error) const {
  (void)entryPath;
  bytes.clear();
  error = "zip support is not built into this HWR core: " + archive.string();
  return false;
}

ScanReport ReplacementIndex::ScanFormatG(const fs::path& root, std::string_view serial,
                                         const ArchiveReader& archives) {
  ScanReport report;
  const fs::path folder = root / std::string(serial) / "gt4";
  const std::vector<fs::path> files = SortedFilesRecursive(folder, report.messages);

  const auto registerEntry = [&](const fs::path& file, const std::string& member,
                                 const std::string& basename) {
    const std::optional<ParsedFormatGName> parsed = ParseFormatGFileName(basename);
    if (!parsed.has_value()) {
      ++report.unparsedNames;
      return;
    }
    const FormatGMapKey key{parsed->key.textureHash, parsed->key.paletteHash, parsed->key.width,
                            parsed->key.height};
    if (formatG_.contains(key)) {
      ++report.duplicatesIgnored;
      return;
    }
    PackEntry entry;
    entry.basename = basename;
    entry.extension = parsed->extension;
    entry.file = file;
    entry.member = member;
    formatG_.emplace(key, entry);
    ++report.filesRegistered;
  };

  for (const fs::path& file : files) {
    if (HasZipExtension(file)) {
      continue;
    }
    registerEntry(file, {}, file.filename().string());
  }
  for (const fs::path& zip : SortedZipsRecursive(folder, report.messages)) {
    std::string error;
    const std::vector<std::string> entries = archives.EntryBasenames(zip, error);
    if (entries.empty() && !error.empty()) {
      ++report.archivesSkipped;
      report.messages.push_back(error);
      continue;
    }
    for (const std::string& basename : entries) {
      registerEntry(zip, basename, basename);
    }
  }
  return report;
}

ScanReport ReplacementIndex::ScanFormatP(const fs::path& root, std::string_view serial,
                                         const ArchiveReader& archives) {
  ScanReport report;
  const fs::path folder = root / std::string(serial) / "replacements";

  const auto registerEntry = [&](const fs::path& file, const std::string& member,
                                 const std::string& basename) {
    const std::optional<ParsedFormatPName> parsed = ParseFormatPFileName(basename);
    if (!parsed.has_value()) {
      ++report.unparsedNames;
      return;
    }
    const FormatPMapKey key{parsed->textureHash, parsed->clutPresent, parsed->clutHash,
                            parsed->regionWidth, parsed->regionHeight, parsed->bits};
    if (formatP_.contains(key)) {
      ++report.duplicatesIgnored;
      return;
    }
    PackEntry entry;
    entry.basename = basename;
    entry.extension = parsed->extension;
    entry.file = file;
    entry.member = member;
    formatP_.emplace(key, entry);
    ++report.filesRegistered;
  };

  for (const fs::path& file : SortedFilesRecursive(folder, report.messages)) {
    if (!HasZipExtension(file)) {
      registerEntry(file, {}, file.filename().string());
    }
  }

  std::vector<fs::path> archivePaths = SortedZipsRecursive(folder, report.messages);
  const fs::path rootArchive = root / (std::string(serial) + ".zip");
  std::error_code error;
  if (fs::is_regular_file(rootArchive, error)) {
    archivePaths.push_back(rootArchive);
  }
  for (const fs::path& zip : archivePaths) {
    std::string readError;
    const std::vector<std::string> entries = archives.EntryBasenames(zip, readError);
    if (entries.empty() && !readError.empty()) {
      ++report.archivesSkipped;
      report.messages.push_back(readError);
      continue;
    }
    for (const std::string& basename : entries) {
      registerEntry(zip, basename, basename);
    }
  }
  return report;
}

std::optional<PackEntry> ReplacementIndex::FindFormatG(const FormatGKey& key) const {
  const auto found = formatG_.find(
      FormatGMapKey{key.textureHash, key.paletteHash, key.width, key.height});
  if (found == formatG_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<PackEntry> ReplacementIndex::FindFormatP(const FormatPRecord& record) const {
  const FormatPMapKey key{record.textureHash, record.indexed, record.indexed ? record.clutHash : 0,
                          record.regionWidth, record.regionHeight, record.bits};
  const auto found = formatP_.find(key);
  if (found == formatP_.end()) {
    return std::nullopt;
  }
  return found->second;
}

bool ReadPackEntryBytes(const PackEntry& entry, const ArchiveReader& archives,
                        std::vector<uint8_t>& bytes, std::string& error) {
  if (!entry.member.empty()) {
    return archives.ReadEntry(entry.file, entry.member, bytes, error);
  }
  std::ifstream stream(entry.file, std::ios::binary);
  if (!stream) {
    error = "cannot open " + entry.file.string();
    return false;
  }
  bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace hw_gs_replace
