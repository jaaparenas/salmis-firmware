#include "media_mcp_tools.h"

#include <esp_log.h>
#include <esp_random.h>

#include <atomic>

#include "application.h"
#include "mcp_server.h"
#include "media_library.h"

#define TAG "MediaMcp"

namespace {

// The robot must not read the whole library aloud. Every answer the device
// returns is bounded to this many titles, so the assistant has a short list
// it can offer instead of an endless one.
constexpr size_t kListPageSize = 5;
constexpr size_t kSampleSize = 5;

std::atomic<int> g_current_id{-1};

cJSON* TrackJson(const MediaTrack& track) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", track.id);
    cJSON_AddStringToObject(item, "title", track.title.c_str());
    return item;
}

cJSON* StatusJson(const char* status) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", status);
    return root;
}

cJSON* StartTrack(const MediaTrack& track) {
    if (!Application::GetInstance().PlayLocalAudio(track.path)) {
        return StatusJson("storage_unavailable");
    }
    g_current_id = track.id;
    ESP_LOGI(TAG, "Play id=%d title=%s", track.id, track.title.c_str());
    cJSON* root = StatusJson("playing");
    cJSON_AddNumberToObject(root, "id", track.id);
    cJSON_AddStringToObject(root, "title", track.title.c_str());
    return root;
}

ReturnValue Play(const PropertyList& properties) {
    auto& library = MediaLibrary::GetInstance();
    if (library.Count() == 0) {
        return StatusJson("storage_unavailable");
    }

    int id = properties["id"].value<int>();
    if (id >= 0) {
        MediaTrack track;
        if (!library.Get(id, track)) {
            return StatusJson("not_found");
        }
        return StartTrack(track);
    }

    auto result = library.Search(properties["query"].value<std::string>());
    if (result.kind == MediaLibrary::MatchKind::kNone) {
        // The song is not in the library. Return a short sample so the
        // assistant can offer a few real titles instead of dumping the whole
        // library, and let it ask the user before reading any of them.
        cJSON* root = StatusJson("not_found");
        cJSON_AddNumberToObject(root, "total", static_cast<double>(library.Count()));
        cJSON* sample = cJSON_AddArrayToObject(root, "sample");
        for (const auto& track : library.Page(0, kSampleSize)) {
            cJSON_AddItemToArray(sample, TrackJson(track));
        }
        return root;
    }
    if (result.kind == MediaLibrary::MatchKind::kUnique) {
        return StartTrack(result.tracks.front());
    }
    cJSON* root = StatusJson("ambiguous");
    cJSON* candidates = cJSON_AddArrayToObject(root, "candidates");
    for (const auto& track : result.tracks) {
        cJSON_AddItemToArray(candidates, TrackJson(track));
    }
    return root;
}

ReturnValue Random(const PropertyList&) {
    auto& library = MediaLibrary::GetInstance();
    size_t count = library.Count();
    if (count == 0) {
        return StatusJson("storage_unavailable");
    }
    size_t index = esp_random() % count;
    MediaTrack track;
    if (!library.Get(static_cast<int>(index), track)) {
        return StatusJson("not_found");
    }
    ESP_LOGI(TAG, "Random pick id=%d title=%s", track.id, track.title.c_str());
    return StartTrack(track);
}

ReturnValue List(const PropertyList& properties) {
    auto& library = MediaLibrary::GetInstance();
    int page = properties["page"].value<int>();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total", static_cast<double>(library.Count()));
    cJSON_AddNumberToObject(root, "page", page);
    cJSON_AddNumberToObject(root, "page_size", kListPageSize);
    cJSON* tracks = cJSON_AddArrayToObject(root, "tracks");
    for (const auto& track : library.Page(page, kListPageSize)) {
        cJSON_AddItemToArray(tracks, TrackJson(track));
    }
    return root;
}

ReturnValue Status(const PropertyList&) {
    cJSON* root = cJSON_CreateObject();
    bool playing = Application::GetInstance().IsLocalAudioPlaying();
    cJSON_AddBoolToObject(root, "playing", playing);
    MediaTrack track;
    if (playing && MediaLibrary::GetInstance().Get(g_current_id, track)) {
        cJSON_AddNumberToObject(root, "id", track.id);
        cJSON_AddStringToObject(root, "title", track.title.c_str());
    }
    return root;
}

}  // namespace

void RegisterMediaMcpTools() {
    auto& server = McpServer::GetInstance();

    server.AddTool(
        "self.media.play",
        "Play a song or audio file stored on this device's own memory card. Use this whenever "
        "the user asks to play, put on, or listen to a song, story, or audio by name. This "
        "device has no internet music: it never streams songs from the web and this tool is "
        "the ONLY way to play music or audio on it. ALWAYS use it, and NEVER use a music "
        "search, a web search, or any other tool to find or play a song; if the song is not "
        "in the library, say so instead. Pass the name the user said in `query` (just the "
        "title, without words like 'play'). The result is one of: `playing` (confirm briefly "
        "in one short sentence; playback starts right after you finish speaking), "
        "`not_found` (the song is not in the library; `sample` holds up to five titles that "
        "ARE in it. Say in one short sentence that the song is not there, then ask the user "
        "ONE short question: whether they want to hear a few of those titles, or to have a "
        "random one played. Do NOT read the titles out until the user says yes, and never "
        "read more than five), "
        "`ambiguous` (read the candidate titles and ask which one, then call this tool again "
        "with that candidate's `id`), or `storage_unavailable` (the memory card is missing or "
        "empty). Do not call this tool to pause or stop; use self.media.stop.",
        PropertyList({Property("query", kPropertyTypeString, std::string("")),
                      Property("id", kPropertyTypeInteger, -1, -1, 100000)}),
        Play);

    server.AddTool(
        "self.media.random",
        "Play a random song from the device's own memory card. Use when the user asks for a "
        "surprise, says 'pick one at random', or accepts the offer to have a random song "
        "played after a song was not found in the library. The result is one of: `playing` "
        "(confirm briefly in one short sentence; playback starts right after you finish "
        "speaking) or `storage_unavailable` (the memory card is missing or empty).",
        PropertyList(), Random);

    server.AddTool(
        "self.media.stop",
        "Stop the song that is currently playing from the device's memory card. Use when the "
        "user says stop, enough, be quiet, or cancel the music.",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            Application::GetInstance().StopLocalAudio();
            return true;
        });

    server.AddTool(
        "self.media.status",
        "Tell whether a song from the device's memory card is playing now and which one. Use "
        "when the user asks what is playing.",
        PropertyList(), Status);

    server.AddTool(
        "self.media.list",
        "List the songs stored on the device's memory card, 5 per page, with their ids. Use "
        "only when the user asks what songs or audio are available, or when a search was not "
        "found AND the user said they want to hear some titles. Read at most ONE page, that "
        "is at most five titles, and then stop: never read the whole library aloud and never "
        "go through page after page.",
        PropertyList({Property("page", kPropertyTypeInteger, 0, 0, 1000)}), List);

    server.AddUserOnlyTool("self.media.rescan",
                           "Rescan the memory card after files were added or removed.",
                           PropertyList(), [](const PropertyList&) -> ReturnValue {
                               MediaLibrary::GetInstance().Rescan();
                               return true;
                           });
}
