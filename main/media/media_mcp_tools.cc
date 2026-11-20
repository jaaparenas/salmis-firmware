#include "media_mcp_tools.h"

#include <esp_log.h>

#include <atomic>

#include "application.h"
#include "mcp_server.h"
#include "media_library.h"

#define TAG "MediaMcp"

namespace {

constexpr size_t kListPageSize = 10;

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
        return StatusJson("not_found");
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
        "the user asks to play, put on, or listen to a song, story, or audio by name, and ALWAYS "
        "prefer it over any other music or web search. Pass the name the user said in `query` "
        "(just the title, without words like 'play'). The result is one of: `playing` (confirm "
        "briefly in one short sentence; playback starts right after you finish speaking), "
        "`not_found` (tell the user it is not in the library and offer self.media.list), "
        "`ambiguous` (read the candidate titles and ask which one, then call this tool again "
        "with that candidate's `id`), or `storage_unavailable` (the memory card is missing or "
        "empty). Do not call this tool to pause or stop; use self.media.stop.",
        PropertyList({Property("query", kPropertyTypeString, std::string("")),
                      Property("id", kPropertyTypeInteger, -1, -1, 100000)}),
        Play);

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
        "List the songs stored on the device's memory card, 10 per page, with their ids. Use "
        "when the user asks what songs or audio are available, or when a search was not found. "
        "Read at most a few titles aloud and offer the next page instead of reading everything.",
        PropertyList({Property("page", kPropertyTypeInteger, 0, 0, 1000)}), List);

    server.AddUserOnlyTool("self.media.rescan",
                           "Rescan the memory card after files were added or removed.",
                           PropertyList(), [](const PropertyList&) -> ReturnValue {
                               MediaLibrary::GetInstance().Rescan();
                               return true;
                           });
}
