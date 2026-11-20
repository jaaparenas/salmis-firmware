#ifndef MEDIA_LIBRARY_H_
#define MEDIA_LIBRARY_H_

#include <mutex>
#include <string>
#include <vector>

struct MediaTrack {
    int id = 0;
    std::string path;
    std::string title;
    std::string normalized;
};

class MediaLibrary {
public:
    enum class MatchKind { kNone, kUnique, kAmbiguous };

    struct SearchResult {
        MatchKind kind = MatchKind::kNone;
        std::vector<MediaTrack> tracks;
    };

    static constexpr size_t kMaxTracks = 500;
    static constexpr size_t kMaxCandidates = 5;

    static MediaLibrary& GetInstance();

    void Rescan();
    size_t Count();
    // Number of tracks found by the last scan. Never triggers a scan, so it
    // is safe to call from a latency sensitive path.
    size_t CachedCount();
    // True once a scan has completed, even if it found nothing.
    bool IsScanned();
    std::vector<MediaTrack> Page(size_t page, size_t page_size);
    bool Get(int id, MediaTrack& out);
    SearchResult Search(const std::string& query);

    // Lowercase ASCII, accents removed, anything else collapsed into single spaces.
    static std::string Normalize(const std::string& text);

private:
    MediaLibrary() = default;

    void EnsureScanned();
    void ScanDirectory(const std::string& dir, int depth, std::vector<std::string>& out);

    std::mutex mutex_;
    // Serializes scans so two callers never walk the card at the same time.
    std::mutex scan_mutex_;
    std::vector<MediaTrack> tracks_;
    bool scanned_ = false;
};

#endif  // MEDIA_LIBRARY_H_
