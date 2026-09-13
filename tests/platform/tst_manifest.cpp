// tst_manifest -- rawpage.exe carries the application manifest that opts it into the segment heap.
// Stage 2 issue I-35, guide case A-93.
//
// HERE RATHER THAN IN A GATE because reading an executable's resources is a Win32 question
// (`LoadLibraryEx` as a data file, `FindResource`), and this is the suite that is Windows-only by rule.
// The subject is `app/`'s build output, not `platform/`'s code, which is why the target depends on
// `rawpage` explicitly.
//
// WHY IT EXISTS AT ALL. Without `app/rawpage.manifest` the product's allocations come from the classic
// NT heap, which fragmented on the tile workload: 60 -> 167 MB of committed-but-free memory over 30
// rounds of ten documents, with live memory flat. The fix is a resource in a build file, which is
// exactly the kind of thing that disappears in a refactor without any test going red -- every behaviour
// still works, the process just keeps more memory than it should, and only a forty-round measurement
// would notice.
//
// WHAT THIS DOES NOT COVER (convention 19): that Windows HONOURS the element. It proves the resource is
// embedded and says what it should; that the heap then stays flat is measured by
// `rawpage_bench_openclose_ui` against its `_classicheap` control (guide case A-92), and Windows only
// honours it from 10 2004 (build 19041).

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <string>

namespace {

// The RT_MANIFEST resource with id 1 of the module at `path`, or empty when there is none.
std::string manifestOf(const wchar_t* path)
{
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr) return {};
    std::string text;
    if (HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24))) {
        if (HGLOBAL loaded = LoadResource(module, resource)) {
            const DWORD size = SizeofResource(module, resource);
            if (const auto* bytes = static_cast<const char*>(LockResource(loaded)))
                text.assign(bytes, bytes + size);
        }
    }
    FreeLibrary(module);
    return text;
}

std::wstring widen(const char* utf8)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length > 0 ? length - 1 : 0), L'\0');
    if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
    return wide;
}

} // namespace

TEST_CASE("rawpage.exe embeds a manifest that opts it into the segment heap", "[manifest]")
{
    const std::string manifest = manifestOf(widen(RAWPAGE_EXE_PATH).c_str());
    INFO("rawpage.exe at " << RAWPAGE_EXE_PATH);
    REQUIRE_FALSE(manifest.empty());
    CHECK(manifest.find("<heapType") != std::string::npos);
    CHECK(manifest.find(">SegmentHeap</heapType>") != std::string::npos);
    CHECK(manifest.find("http://schemas.microsoft.com/SMI/2020/WindowsSettings") != std::string::npos);

    SECTION("AND WINDOWS ACCEPTS IT: the product still starts")
    {
        // The half the text checks cannot see, and the one that bit. The first version of the
        // manifest carried a double hyphen inside its XML comment; XML forbids that, Windows rejected
        // the whole manifest, and every executable embedding it failed to START ("side-by-side
        // configuration is incorrect") -- while the three CHECKs above passed, because the text was
        // all there. A manifest is only proven by a process that loads under it.
        std::wstring command = L"\"" + widen(RAWPAGE_EXE_PATH) + L"\" --version";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        INFO("CreateProcess error " << (started ? 0 : GetLastError())
             << " (14001 is ERROR_SXS_CANT_GEN_ACTCTX: the manifest was rejected)");
        REQUIRE(started);
        const DWORD waited = WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode = 0xFFFFFFFF;
        GetExitCodeProcess(process.hProcess, &exitCode);
        if (waited != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CHECK(waited == WAIT_OBJECT_0);
        CHECK(exitCode == 0);
    }

    SECTION("THE CONTROL: a binary built without one reads as having none")
    {
        // This test binary carries no manifest. Without the control, a lookup that returned SOMETHING
        // for every module -- a default manifest the toolchain injected, say -- would pass above.
        wchar_t self[MAX_PATH] = L"";
        REQUIRE(GetModuleFileNameW(nullptr, self, MAX_PATH) > 0);
        CHECK(manifestOf(self).find("SegmentHeap") == std::string::npos);
    }
}
