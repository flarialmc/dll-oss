#include "PlatformUtils.hpp"
#include <Utils/Logger/Logger.hpp>
#include <windows.h>
#include <filesystem>
#include <shlobj.h>
#include <knownfolders.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.ApplicationModel.h>
#include <appmodel.h>
#include <shellapi.h>
#include <fmt/format.h>

#include "WinrtUtils.hpp"

// OutputDebugStringA wrapper for use inside detectPlatform() — we can't use
// Logger there because Logger depends on Utils::getClientPath() which calls
// PlatformUtils::isGDK(), creating an infinite recursion during initialization.
namespace {
    void debugTrace(const std::string& msg) {
        OutputDebugStringA(msg.c_str());
        OutputDebugStringA("\n");
    }
}

// Static member initialization
PlatformType PlatformUtils::s_detectedPlatform = PlatformType::Unknown;
bool PlatformUtils::s_initialized = false;

void PlatformUtils::initialize() {
    if (s_initialized) return;
    s_detectedPlatform = detectPlatform();
    s_initialized = true;

    // Unknown-path policy: when classification is Unknown or Win32, the client
    // falls through to GDK-style code paths (SHGetKnownFolderPath, Win32 dialogs,
    // SetWindowText, etc.) which work on any desktop environment. UWP-only features
    // (WinRT file pickers, toast notifications, protocol activation) are simply
    // skipped. This is safe — the worst case is degraded UX, not broken state.
    Logger::info("Platform detected: {}", getPlatformString());

    if (isWine()) {
        Logger::info("Wine/Proton environment detected: \"Better Frames\" (DX11 fallback) "
                     "will be enabled by default for new installs");
    }
}


PlatformType PlatformUtils::detectPlatform() {
    // Step 1: AUMID check — the original, battle-tested UWP detection.
    // AppPolicyGetWindowingModel is more elegant but this has been reliable in production.
    UINT32 aumidLength = 0;
    GetCurrentApplicationUserModelId(&aumidLength, nullptr);
    if (aumidLength > 0) {
        std::wstring aumid(aumidLength, L'\0');
        if (GetCurrentApplicationUserModelId(&aumidLength, aumid.data()) == ERROR_SUCCESS) {
            if (!aumid.empty() && aumid.back() == L'\0') aumid.pop_back();
            if (aumid == L"Microsoft.MinecraftUWP_8wekyb3d8bbwe!App") {
                debugTrace("[PlatformDetect] AUMID matched UWP package");
                return PlatformType::UWP;
            }
            debugTrace(fmt::format("[PlatformDetect] AUMID present but not UWP: length={}", aumid.size()));
        }
    } else {
        debugTrace("[PlatformDetect] No AUMID available");
    }

    // Step 2: Confirm non-UWP via AppPolicyGetWindowingModel as a secondary check.
    AppPolicyWindowingModel windowingModel{};
    LONG wmResult = AppPolicyGetWindowingModel(GetCurrentProcessToken(), &windowingModel);
    debugTrace(fmt::format("[PlatformDetect] AppPolicyGetWindowingModel rc={}, model={}",
                           wmResult, static_cast<int>(windowingModel)));

    if (wmResult == ERROR_SUCCESS && windowingModel == AppPolicyWindowingModel_Universal) {
        // AUMID didn't match but windowing model says Universal — treat as UWP.
        debugTrace("[PlatformDetect] Windowing model is Universal, classifying as UWP");
        return PlatformType::UWP;
    }

    // Step 3: Not UWP. Try to distinguish GDK Secure vs Insecure via package origin.
    // If anything fails along the way, default to GDKInsecure (matches old behavior
    // where non-UWP always meant GDK — critical for config path resolution).
    UINT32 packageNameLength = 0;
    LONG pkgResult = GetCurrentPackageFullName(&packageNameLength, nullptr);
    if (pkgResult != ERROR_INSUFFICIENT_BUFFER || packageNameLength == 0) {
        // No package identity. Still default to GDKInsecure rather than Win32,
        // because Flarial is always injected into Minecraft and the GDK code paths
        // (config directories, etc.) are the safe default for non-UWP.
        debugTrace(fmt::format("[PlatformDetect] No package identity (rc={}), defaulting to GDKInsecure",
                               pkgResult));
        return PlatformType::GDKInsecure;
    }

    std::wstring packageFullName(packageNameLength, L'\0');
    pkgResult = GetCurrentPackageFullName(&packageNameLength, packageFullName.data());
    if (pkgResult != ERROR_SUCCESS) {
        debugTrace(fmt::format("[PlatformDetect] GetCurrentPackageFullName second call failed (rc={})", pkgResult));
        return PlatformType::GDKInsecure;
    }
    if (!packageFullName.empty() && packageFullName.back() == L'\0') {
        packageFullName.pop_back();
    }

    // Dynamically load GetStagedPackageOrigin — it lives in kernelbase.dll but
    // isn't exported from kernel32.lib, only from onecoreuap.lib which we don't link.
    using GetStagedPackageOriginFn = LONG(WINAPI*)(PCWSTR, PackageOrigin*);
    static auto pfn = reinterpret_cast<GetStagedPackageOriginFn>(
        GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetStagedPackageOrigin"));

    if (!pfn) {
        debugTrace("[PlatformDetect] GetStagedPackageOrigin not found in kernelbase.dll");
        return PlatformType::GDKInsecure;
    }

    PackageOrigin origin{};
    LONG originResult = pfn(packageFullName.c_str(), &origin);
    if (originResult != ERROR_SUCCESS) {
        debugTrace(fmt::format("[PlatformDetect] GetStagedPackageOrigin failed (rc={})", originResult));
        return PlatformType::GDKInsecure;
    }

    debugTrace(fmt::format("[PlatformDetect] Package origin={} (Store={})",
                           static_cast<int>(origin), static_cast<int>(PackageOrigin_Store)));

    return (origin == PackageOrigin_Store) ? PlatformType::GDKSecure : PlatformType::GDKInsecure;
}

std::string PlatformUtils::getPlatformString() {
    if (!s_initialized) initialize();

    switch (s_detectedPlatform) {
        case PlatformType::UWP:
            return "UWP (Windows Store)";
        case PlatformType::GDKSecure:
            return "GDK Secure (Store)";
        case PlatformType::GDKInsecure:
            return "GDK Insecure (Sideloaded)";
        case PlatformType::Win32:
            return "Win32 (Desktop)";
        default:
            return "Unknown";
    }
}

bool PlatformUtils::isUWP() {
    if (!s_initialized) initialize();
    return s_detectedPlatform == PlatformType::UWP;
}

bool PlatformUtils::isGDK() {
    if (!s_initialized) initialize();
    return s_detectedPlatform == PlatformType::GDKSecure
        || s_detectedPlatform == PlatformType::GDKInsecure;
}

bool PlatformUtils::isGDKSecure() {
    if (!s_initialized) initialize();
    return s_detectedPlatform == PlatformType::GDKSecure;
}

bool PlatformUtils::isWine() {
    // Wine (and Proton, which is built on Wine) exports wine_get_version from
    // ntdll.dll; native Windows ntdll never exports it. This is the standard,
    // reliable way to detect a Wine-based environment from inside the process.
    static bool cached = []() {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
    }();
    return cached;
}

std::string PlatformUtils::getRoamingPath() {
    if (isUWP())
    {
        using namespace winrt::Windows::Storage;
        std::filesystem::path path(ApplicationData::Current().RoamingFolder().Path().c_str());
        return path.string();
    }
    else
    {
        wchar_t* appDataPath = nullptr;
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appDataPath);
        if (SUCCEEDED(hr) && appDataPath != nullptr) {
            std::filesystem::path path(appDataPath);
            CoTaskMemFree(appDataPath);
            return path.string();
        }
    }

    return "";
}

std::string PlatformUtils::getLocalPath() {

    /*
    * Due to GDK installs and custom launchers, the path "AppData/MinecraftBedrock/.../ is not guaranteed to exist.
    * Launchers such as Levi simply bind this path to the folder corresponding to a
    * specific version. This means that if a user installs Minecraft using a custom
    * launcher, the AppData directory may either not exist (if levi was used before mc was ever installed from MS Store)
     * or be irrelevant to the instance that Flarial is injected into.
    *
    * Previously, if this path did not exist, the application would crash. We now
    * handle this case by assuming the user is on Levi, then checking the executable
    * path and retrieving the MCBE user folders from there (Levi-specific behavior).
    *
    * If the path does exist but the user is using a custom launcher, the data found
    * there (e.g., options.txt) may be irrelevant to the current instance of Minecraft. This can
    * cause issues with OptionParser (which relies on options.txt updating) and any modules that depend on it.
    */

    wchar_t* appDataPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath);
    
    auto iteratePath = [](std::string path) -> std::string {
        if(std::filesystem::exists(path)) {
            for (const auto &entry: std::filesystem::directory_iterator(path)) {
                if (entry.is_directory()) {
                    return entry.path().string();
                }
            }
        }

        return "";
    };

    if (SUCCEEDED(hr) && appDataPath != nullptr) {
        std::filesystem::path path(appDataPath);
        CoTaskMemFree(appDataPath);

        auto usersPath = path.string() + R"(\Minecraft Bedrock\Users\)";
        auto resPath = iteratePath(usersPath);

        if(!resPath.empty()) {
            return resPath;
        }
        else {
            auto currentPath = std::filesystem::current_path();
            return iteratePath(currentPath.string() + R"(\Minecraft Bedrock\Users\)");
        }
    }

    return "";
}

void PlatformUtils::openFolder(const std::string& folderPath) {
    ShellExecuteW(nullptr, L"open", std::wstring(folderPath.begin(), folderPath.end()).c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
}

void PlatformUtils::launchURI(const std::string& uri) {
    if (isUWP()) WinrtUtils::launchURI(uri);
    else ShellExecute(0, "open", uri.c_str(), 0, 0, SW_SHOWNORMAL);
}

void PlatformUtils::setClipboard(const std::string& text) {
    if (isUWP()) WinrtUtils::setClipboard(text);
    else if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hGlob) {
            memcpy(GlobalLock(hGlob), text.c_str(), text.size() + 1);
            GlobalUnlock(hGlob);
            SetClipboardData(CF_TEXT, hGlob);
        }
        CloseClipboard();
    }
}

std::string PlatformUtils::getClipboard() {
    if (isUWP()) return WinrtUtils::getClipboard();
    else {
        std::string result;
        if (OpenClipboard(NULL)) {
            HANDLE hData = GetClipboardData(CF_TEXT);
            if (hData) {
                char* pszText = static_cast<char*>(GlobalLock(hData));
                if (pszText) {
                    result = pszText;
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }
        return result;
    }
}
