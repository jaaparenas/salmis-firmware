"""Exercise the real MediaLibrary search on the host.

Compiles main/media/media_library.cc with a stub esp_log.h and a temporary
library root, then checks the matching rules against it.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "main/media/media_library.cc"
LIBRARY_HEADER = ROOT / "main/media/media_library.h"

# The library root is a constant in the source. It is replaced so the test
# can build a library of its own.
LIBRARY_ROOT = "/sdcard/mp3"
TEST_ROOT = "/tmp/opencode-media-library-test/mp3"

STUB_LOG = """#pragma once
#define ESP_LOGI(tag, ...)
#define ESP_LOGW(tag, ...)
#define ESP_LOGE(tag, ...)
"""

HARNESS = r"""
#include <cstdio>
#include <string>
#include <vector>

#include "media_library.h"

namespace {

int g_failures = 0;
int g_checks = 0;

const char* KindName(MediaLibrary::MatchKind kind) {
    switch (kind) {
        case MediaLibrary::MatchKind::kNone: return "none";
        case MediaLibrary::MatchKind::kUnique: return "unique";
        case MediaLibrary::MatchKind::kAmbiguous: return "ambiguous";
    }
    return "?";
}

void Check(const std::string& query, MediaLibrary::MatchKind expected,
           const std::string& expected_title = "") {
    g_checks++;
    auto& library = MediaLibrary::GetInstance();
    auto result = library.Search(query);

    std::string titles;
    for (size_t i = 0; i < result.tracks.size(); i++) {
        if (i) titles += " | ";
        titles += result.tracks[i].title;
    }

    bool ok = result.kind == expected;
    if (ok && expected == MediaLibrary::MatchKind::kUnique && !expected_title.empty()) {
        ok = result.tracks.size() == 1 && result.tracks[0].title == expected_title;
    }

    printf("%s  query=%-40s -> %-9s %s\n", ok ? "PASS" : "FAIL",
           ("\"" + query + "\"").c_str(), KindName(result.kind), titles.c_str());
    if (!ok) {
        g_failures++;
        printf("      expected: %s %s\n", KindName(expected), expected_title.c_str());
    }
}

}  // namespace

int main() {
    auto& library = MediaLibrary::GetInstance();
    library.Rescan();
    printf("library: %u tracks\n\n", (unsigned)library.Count());

    // A title must match every word of the query. Matching only some of
    // them once made "caperucita roja" answer with "caperucita azul".
    Check("caperucita roja", MediaLibrary::MatchKind::kUnique, "Caperucita Roja");
    Check("caperucita azul", MediaLibrary::MatchKind::kUnique, "Caperucita Azul");
    Check("caperucita verde", MediaLibrary::MatchKind::kNone);
    Check("caperucita", MediaLibrary::MatchKind::kAmbiguous);

    // An exact title wins over a longer one that merely contains it.
    Check("caperucita roja y el lobo", MediaLibrary::MatchKind::kUnique,
          "Caperucita Roja y el Lobo");
    Check("caperucita roja", MediaLibrary::MatchKind::kUnique, "Caperucita Roja");

    // Filler words must not get in the way of a match.
    Check("la caperucita roja", MediaLibrary::MatchKind::kUnique, "Caperucita Roja");
    Check("pon la cancion de la caperucita roja", MediaLibrary::MatchKind::kUnique,
          "Caperucita Roja");
    Check("el ratoncito perez", MediaLibrary::MatchKind::kUnique, "El Ratoncito Perez");
    Check("la cancion del oso", MediaLibrary::MatchKind::kUnique, "La Cancion del Oso");
    Check("play we wish you a merry christmas", MediaLibrary::MatchKind::kUnique,
          "We Wish You a Merry Christmas");

    // A query made only of filler words carries no information.
    Check("la", MediaLibrary::MatchKind::kNone);
    Check("la cancion", MediaLibrary::MatchKind::kNone);

    // One folder level below the root is indexed too.
    Check("subfolder song", MediaLibrary::MatchKind::kUnique, "Subfolder Song");
    Check("subfolder", MediaLibrary::MatchKind::kUnique, "Subfolder Song");

    // Edge cases.
    Check("", MediaLibrary::MatchKind::kNone);
    Check("   ", MediaLibrary::MatchKind::kNone);
    Check("perro", MediaLibrary::MatchKind::kNone);
    Check("CAPERUCITA ROJA", MediaLibrary::MatchKind::kUnique, "Caperucita Roja");
    Check("capérucita", MediaLibrary::MatchKind::kAmbiguous);
    Check("merry christmas", MediaLibrary::MatchKind::kUnique, "We Wish You a Merry Christmas");
    Check("rock music", MediaLibrary::MatchKind::kUnique, "Rock Song");

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
"""

TRACKS = [
    "Caperucita Roja.mp3",
    "Caperucita Roja y el Lobo.mp3",
    "Caperucita Azul.mp3",
    "El Ratoncito Perez.mp3",
    "La Cancion del Oso.mp3",
    "Rock Song.mp3",
    "We Wish You a Merry Christmas.mp3",
]


@unittest.skipUnless(shutil.which("c++"), "requires a host C++ compiler")
class MediaLibrarySearchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        work = pathlib.Path(cls.tmp.name)

        (work / "esp_log.h").write_text(STUB_LOG)

        source = LIBRARY.read_text()
        self_check = f'"{LIBRARY_ROOT}"'
        if self_check not in source:
            raise AssertionError(f"library root {LIBRARY_ROOT} not found in {LIBRARY}")
        (work / "media_library.cc").write_text(source.replace(self_check, f'"{TEST_ROOT}"'))

        # The library the test searches into.
        library_dir = pathlib.Path(TEST_ROOT)
        shutil.rmtree(library_dir, ignore_errors=True)
        library_dir.mkdir(parents=True)
        for name in TRACKS:
            (library_dir / name).write_bytes(b"x")
        (library_dir / "sub").mkdir()
        (library_dir / "sub" / "Subfolder Song.mp3").write_bytes(b"x")

        (work / "harness.cc").write_text(HARNESS)

        cls.binary = str(work / "media-library-test")
        subprocess.run(
            ["c++", "-std=c++17", "-Wall", "-Werror",
             "-I", str(work), "-I", str(LIBRARY_HEADER.parent),
             str(work / "media_library.cc"), str(work / "harness.cc"),
             "-o", cls.binary],
            check=True,
        )

    def test_search_rules(self):
        result = subprocess.run([self.binary], capture_output=True, text=True)
        print(result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
