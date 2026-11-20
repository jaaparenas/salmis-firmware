#include "media_library.h"

#include <dirent.h>
#include <esp_log.h>
#include <strings.h>

#include <algorithm>

#define TAG "MediaLibrary"

namespace {

constexpr const char* kRoot = "/sdcard/mp3";
constexpr int kMaxDepth = 1;

bool HasMp3Extension(const std::string& name) {
    return name.size() > 4 && strcasecmp(name.c_str() + name.size() - 4, ".mp3") == 0;
}

char BaseLetterForLatin1(unsigned char c) {
    switch (c) {
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: return 'a';
        case 0xA7: return 'c';
        case 0xA8: case 0xA9: case 0xAA: case 0xAB: return 'e';
        case 0xAC: case 0xAD: case 0xAE: case 0xAF: return 'i';
        case 0xB1: return 'n';
        case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: return 'o';
        case 0xB9: case 0xBA: case 0xBB: case 0xBC: return 'u';
        default: return ' ';
    }
}

std::string TitleFromPath(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    name.resize(name.size() - 4);
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

}  // namespace

MediaLibrary& MediaLibrary::GetInstance() {
    static MediaLibrary instance;
    return instance;
}

std::string MediaLibrary::Normalize(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    auto push = [&out](char c) {
        if (c == ' ') {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
        } else {
            out.push_back(c);
        }
    };
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z') {
                push(static_cast<char>(c - 'A' + 'a'));
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                push(static_cast<char>(c));
            } else {
                push(' ');
            }
        } else if (c == 0xC3 && i + 1 < text.size()) {
            unsigned char next = static_cast<unsigned char>(text[++i]);
            if (next >= 0x80 && next <= 0x9F) {
                next += 0x20;
            }
            push(BaseLetterForLatin1(next));
        } else {
            push(' ');
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

void MediaLibrary::ScanDirectory(const std::string& dir, int depth, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
        return;
    }
    while (auto* entry = readdir(d)) {
        if (out.size() >= kMaxTracks) {
            break;
        }
        std::string name = entry->d_name;
        if (name.empty() || name[0] == '.') {
            continue;
        }
        std::string full = dir + "/" + name;
        if (entry->d_type == DT_DIR) {
            if (depth < kMaxDepth) {
                ScanDirectory(full, depth + 1, out);
            }
        } else if (HasMp3Extension(name)) {
            out.push_back(full);
        }
    }
    closedir(d);
}

void MediaLibrary::Rescan() {
    std::vector<std::string> paths;
    ScanDirectory(kRoot, 0, paths);
    std::sort(paths.begin(), paths.end());

    std::lock_guard<std::mutex> lock(mutex_);
    tracks_.clear();
    for (const auto& path : paths) {
        MediaTrack track;
        track.id = static_cast<int>(tracks_.size());
        track.path = path;
        track.title = TitleFromPath(path);
        track.normalized = Normalize(track.title);
        tracks_.push_back(std::move(track));
    }
    scanned_ = true;
    ESP_LOGI(TAG, "Catalog: %u tracks", static_cast<unsigned>(tracks_.size()));
}

void MediaLibrary::EnsureScanned() {
    bool scanned;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scanned = scanned_;
    }
    if (!scanned) {
        Rescan();
    }
}

size_t MediaLibrary::Count() {
    EnsureScanned();
    std::lock_guard<std::mutex> lock(mutex_);
    return tracks_.size();
}

std::vector<MediaTrack> MediaLibrary::Page(size_t page, size_t page_size) {
    EnsureScanned();
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MediaTrack> result;
    size_t start = page * page_size;
    for (size_t i = start; i < tracks_.size() && result.size() < page_size; i++) {
        result.push_back(tracks_[i]);
    }
    return result;
}

bool MediaLibrary::Get(int id, MediaTrack& out) {
    EnsureScanned();
    std::lock_guard<std::mutex> lock(mutex_);
    if (id < 0 || static_cast<size_t>(id) >= tracks_.size()) {
        return false;
    }
    out = tracks_[id];
    return true;
}

MediaLibrary::SearchResult MediaLibrary::Search(const std::string& query) {
    EnsureScanned();
    SearchResult result;

    std::string normalized_query = Normalize(query);
    std::vector<std::string> tokens;
    size_t pos = 0;
    while (pos < normalized_query.size()) {
        size_t end = normalized_query.find(' ', pos);
        if (end == std::string::npos) {
            end = normalized_query.size();
        }
        tokens.push_back(normalized_query.substr(pos, end - pos));
        pos = end + 1;
    }
    if (tokens.empty()) {
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    size_t best = 0;
    std::vector<std::pair<size_t, const MediaTrack*>> scored;
    for (const auto& track : tracks_) {
        if (track.normalized == normalized_query) {
            result.kind = MatchKind::kUnique;
            result.tracks = {track};
            return result;
        }
        size_t matched = 0;
        for (const auto& token : tokens) {
            if (track.normalized.find(token) != std::string::npos) {
                matched++;
            }
        }
        if (matched > 0) {
            scored.emplace_back(matched, &track);
            best = std::max(best, matched);
        }
    }
    if (best == 0 || best * 2 < tokens.size()) {
        return result;
    }

    for (const auto& [matched, track] : scored) {
        if (matched == best) {
            result.tracks.push_back(*track);
        }
    }
    result.kind = result.tracks.size() == 1 ? MatchKind::kUnique : MatchKind::kAmbiguous;
    if (result.tracks.size() > kMaxCandidates) {
        result.tracks.resize(kMaxCandidates);
    }
    return result;
}
