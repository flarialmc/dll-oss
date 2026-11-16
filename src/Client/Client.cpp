#include "Client.hpp"
#include "Managers/Managers.hpp"
#include <lib/json/json.hpp>

#include <filesystem>
#include <thread>

#include <Utils/VersionUtils.hpp>
#include <Utils/WinrtUtils.hpp>
#include <Utils/PlatformUtils.hpp>
#include <Utils/ShellMessageUtil.hpp>

#include "Command/CommandManager.hpp"

#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

namespace winrt
{
    using namespace Windows::UI::Core;
    using namespace Windows::Foundation;
    using namespace Windows::ApplicationModel::Activation;
    using namespace Windows::ApplicationModel::Core;
}

#include <Scripting/ScriptManager.hpp>

#include "Utils/APIUtils.hpp"

#include "../Assets/Assets.hpp"
#include "Events/Misc/ProtocolEvent.hpp"

// Static member definitions - these synchronize with managers for backward compatibility
Settings Client::settings = Settings();
Settings Client::legacySettings = Settings();

#include "Hook/Manager.hpp"
#include "Module/Manager.hpp"
#include "Config/Settings.hpp"
#include "Utils/Utils.hpp"
#include "GUI/Engine/ExpressionFormat.hpp"
#include "Utils/Telemetry.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

bool notifiedOfConnectionIssue = false;

std::thread::id Client::g_mainThreadId;
std::string Client::current_commit = COMMIT_HASH;
std::vector<std::string> Client::availableConfigs;
HWND Client::window = nullptr;

std::vector<std::string> Client::getPlayersVector(const nlohmann::json& data)
{
    std::vector<std::string> allPlayers;

    try
    {
        if (data.is_array())
        {
            for (const auto& player : data)
            {
                if (player.is_string())
                {
                    allPlayers.push_back(player.get<std::string>());
                }
            }
        }
        else
        {
            LOG_ERROR("Invalid JSON format: expected an array of players.");
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_ERROR("Error parsing players: {}", e.what());
    }

    if (SDK::clientInstance&& SDK::clientInstance
    ->
    getLocalPlayer()
    )
    {
        try
        {
            std::string name = SDK::clientInstance->getLocalPlayer()->getPlayerName();
            std::string clearedName = String::removeNonAlphanumeric(String::removeColorCodes(name));

            if (clearedName.empty())
            {
                clearedName = String::removeColorCodes(name);
            }
            allPlayers.push_back(clearedName);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Error processing local player name: {}", e.what());
        }
    }

    return allPlayers;
}

bool Client::disable = false;
bool Client::init = false;

winrt::event_token activationToken;

void RegisterActivationHandler()
{
    if (PlatformUtils::isUWP())
    {
        try
        {
            try
            {
                auto mainView = winrt::CoreApplication::MainView();
                if (!mainView)
                {
                    Logger::warn(
                        "CoreApplication MainView is null, skipping protocol handler registration");
                    return;
                }
            }
            catch (const winrt::hresult_error& e)
            {
                LOG_ERROR("Cannot access CoreApplication MainView: {} (0x{:08X})",
                          winrt::to_string(e.message()),
                          static_cast<uint32_t>(e.code()));
                return;
            }

            activationToken = winrt::CoreApplication::MainView().Activated(
                [](const auto&, const winrt::IActivatedEventArgs& context)
                {
                    if (context.Kind() != winrt::ActivationKind::Protocol)
                        return;

                    auto uri = winrt::unbox_value<winrt::ProtocolActivatedEventArgs>(context).Uri();

                    std::vector<std::pair<std::wstring, std::wstring>> dataList;

                    for (const auto& dataContext : uri.QueryParsed())
                    {
                        std::wstring name = dataContext.Name().c_str();
                        std::wstring value = dataContext.Value().c_str();

                        dataList.emplace_back(name, value);
                    }

                    auto event = nes::make_holder<ProtocolEvent>(uri.Host().c_str(), dataList);
                    eventMgr.trigger(event);
                });
            Logger::success("Protocol activation handler registered successfully");
        }
        catch (const winrt::hresult_error& e)
        {
            LOG_ERROR("Failed to register protocol handler: {} (0x{:08X})",
                      winrt::to_string(e.message()),
                      static_cast<uint32_t>(e.code()));
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to register protocol handler: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("Unknown exception while registering protocol handler");
        }
    }
    else
    {
        Logger::warn("Protocol activation handler not available in GDK/Win32");
    }

    WinrtUtils::cacheUIHandles();
    WinrtUtils::installPointerHook();
}

void Client::UnregisterActivationHandler()
{
    if (PlatformUtils::isUWP() && activationToken)
    {
        try
        {
            winrt::CoreApplication::MainView().Activated(activationToken);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to unregister protocol handler: {}", e.what());
        }
    }
}

// Forward to ConfigManager
void Client::createConfig(std::string name)
{
    ConfigManager::instance().createConfig(name);
}

void Client::switchConfig(std::string name, bool deleting)
{
    ConfigManager::instance().switchConfig(name, deleting);
    // Sync static variable
    activeConfig = ConfigManager::instance().getActiveConfig();
}

void Client::deleteConfig(std::string name)
{
    ConfigManager::instance().deleteConfig(name);
}

void Client::loadAvailableConfigs()
{
    ConfigManager::instance().loadAvailableConfigs();
    // Sync static variable
    availableConfigs = ConfigManager::instance().getAvailableConfigsMut();
}

// Forward to WindowManager
void Client::setWindowTitle(std::string title)
{
    WindowManager::instance().setWindowTitle(title);
}

void Client::centerCursor()
{
    if (Client::disable) return;
    WindowManager::instance().centerCursor();
}

void Client::initialize()
{
    auto& windowMgr = WindowManager::instance();
    auto& settingsMgr = ClientSettingsManager::instance();
    auto& configMgr = ConfigManager::instance();
    auto& initMgr = InitializationManager::instance();

    windowMgr.findGameWindow();
    window = windowMgr.getWindow();
    g_mainThreadId = windowMgr.getMainThreadId();

    // Initialize platform detection first
    PlatformUtils::initialize();

    // Always initialize WinRT apartment (required for single binary)
    winrt::init_apartment();

    if (PlatformUtils::isUWP())
    {
        Logger::success("Initialized WinRT apartment for UWP");
    }
    else if (PlatformUtils::isGDK())
    {
        Logger::success("Running in GDK mode with WinRT support");
    }
    else
    {
        Logger::success("Running in Win32 mode with WinRT support");
    }

    std::string windowTitle = fmt::format("Flarial v{} {} [{}]", FLARIAL_VERSION,
                                          FLARIAL_BUILD_DATE, COMMIT_HASH);

    if (PlatformUtils::isUWP())
        WinrtUtils::setWindowTitle(windowTitle);

    VersionUtils::initialize();
    Version currentVersion = WinrtUtils::impl::getGameVersion();
    version = WinrtUtils::impl::toRawString(currentVersion);
    initMgr.setVersion(version);
    Telemetry::sendStartupVersionPing(version);

#if !defined(__DEBUG__)
    if (!VersionUtils::isSupported(version))
    {
        LOG_FATAL("{} Unsupported Minecraft version", version);
        ShellMessageUtil::showA(nullptr,
            "This Minecraft version is not supported by Flarial Client.\n\n"
            "To fix this, try the following:\n"
            "1. Make sure your launcher is using the release version of the client (not a custom DLL).\n"
            "2. Restart the launcher and try again.\n"
            "3. If you've done both of the above, then the client may have not updated :(\n"
            "   Join our Discord for help and updates: https://flarial.xyz/discord",
            "Flarial - Unsupported Version", MB_OK | MB_ICONWARNING);
        ModuleManager::terminate();
        disable = true;
        initMgr.setDisabled(true);
        return;
    }
#endif

    VersionUtils::addData();

    std::vector<std::filesystem::path> directories = {
        Utils::getRoamingPath() + "\\Flarial",
        Utils::getRoamingPath() + "\\Flarial\\assets",
        Utils::getRoamingPath() + "\\Flarial\\logs",
        Utils::getRoamingPath() + "\\Flarial\\Config",
        Utils::getRoamingPath() + "\\Flarial\\Scripts",
        Utils::getRoamingPath() + "\\Flarial\\Crosshairs",
        Utils::getRoamingPath() + "\\Flarial\\MessageLogger"
    };

    if (PlatformUtils::isGDK())
    {
        directories = {
            Utils::getRoamingPath() + "\\Flarial\\Client",
            Utils::getRoamingPath() + "\\Flarial\\Client\\assets",
            Utils::getRoamingPath() + "\\Flarial\\Client\\logs",
            Utils::getRoamingPath() + "\\Flarial\\Client\\Config",
            Utils::getRoamingPath() + "\\Flarial\\Client\\Scripts",
            Utils::getRoamingPath() + "\\Flarial\\Client\\Crosshairs",
            Utils::getRoamingPath() + "\\Flarial\\Client\\MessageLogger"
        };
    }

    if (PlatformUtils::isGDK())
    {
        fs::path source =
            getenv("AppData") +
            (std::string)
            "\\..\\Local\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\RoamingState\\Flarial";
        fs::path destination = directories[0];
        fs::path migrationMarker = destination / ".migrated";

        try
        {
            if (std::filesystem::exists(source) && !std::filesystem::is_empty(source) &&
                !std::filesystem::exists(migrationMarker))
            {
                Logger::debug("Migrating Flarial Data");
                fs::create_directories(destination);

                for (const auto& entry : fs::directory_iterator(source))
                {
                    fs::path destPath = destination / entry.path().filename();
                    if (std::filesystem::exists(entry.path()))
                    {
                        fs::copy(entry.path(), destPath,
                                 fs::copy_options::recursive |
                                 fs::copy_options::overwrite_existing);
                    }
                }

                std::ofstream marker(migrationMarker);
                marker.close();

                Logger::success("Successfully migrated Flarial Data to new directory");
            }
        }
        catch (const fs::filesystem_error& e)
        {
            Logger::error("An Error occurred while migrating data: {}", e.what());
        }
    }

    std::thread updateThread([directories]()
    {
        std::string playersList;
        std::string filePath = directories[0].string() + "\\playerscache.txt";
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            std::ofstream createFile(filePath);
            if (!createFile.is_open())
            {
                LOG_ERROR("Could not create file: ");
            }
            else
            {
                createFile.close();
                file.open(filePath);
                if (!file.is_open())
                {
                    LOG_ERROR("Could not open file for reading after creation: ");
                }
            }
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        playersList = buffer.str();
        file.close();

        APIUtils::onlineUsers = APIUtils::ListToVector(playersList);
        APIUtils::onlineUsersSet = APIUtils::onlineUsers | std::ranges::to<decltype(
            APIUtils::onlineUsersSet)>();
    });

    updateThread.detach();

    for (const auto& path : directories)
    {
        if (!std::filesystem::exists(path))
        {
            std::filesystem::create_directory(path);
        }
    }

    // Use managers for settings operations
    settingsMgr.loadLegacySettings();
    hasLegacySettings = settingsMgr.hasLegacySettings();
    softLoadLegacy = settingsMgr.isSoftLoadLegacy();

    settingsMgr.checkSettingsFile();
    privateInit = settingsMgr.isPrivateInitialized();

    if (privateInit)
    {
        LoadPrivate();
        LoadSettings();

        if (PlatformUtils::isGDK())
        {
            // Note: Better Frames (killdx) and VSync Disabler now work on GDK
            settings.getSettingByName<bool>("recreateAtStart")->value = false;
        }
    }

    Logger::success("4");

    ADD_SETTING("fontname", std::string("Space Grotesk"));
    ADD_SETTING("mod_fontname", std::string("Space Grotesk"));
    ADD_SETTING("blurintensity", 2.0f);
    // "killdx" (Better Frames) defaults to ON under Wine/Proton: the D2D overlay
    // Flarial draws on top of the game's DX12 backbuffer (via D3D11on12) is not
    // rendered by Wine's translation layers, so the mod GUI would be invisible.
    // Defaulting to ON forces the game to recreate its swapchain with DX11, which
    // works correctly. Explicitly saved values in PRIVATE always win over this
    // default, and native Windows keeps the previous default of false.
    ADD_SETTING("killdx", PlatformUtils::isWine());
    ADD_SETTING("disable_alias", false);
    ADD_SETTING("vsync", false);
    ADD_SETTING("recreateAtStart", false);
    ADD_SETTING("promotions", true);
    ADD_SETTING("hideHudAndMods", true);
    ADD_SETTING("saveScrollPos", true);
    ADD_SETTING("pageScrollMultiplier", 5.0f);
    ADD_SETTING("snappinglines", true);
    ADD_SETTING("apiusage", true);
    ADD_SETTING("donotwait", true);
    ADD_SETTING("bufferingmode", std::string("Double Buffering"));
    ADD_SETTING("swapeffect", std::string("FLIP_SEQUENTIAL"));
    ADD_SETTING("disableanims", false);
    ADD_SETTING("anonymousApi", false);
    ADD_SETTING("dlassets", true);
    ADD_SETTING("noicons", false);
    ADD_SETTING("noshadows", false);
    ADD_SETTING("watermark", true);
    ADD_SETTING("centreCursor", false);
    ADD_SETTING("aliasingMode", std::string("Default"));
    ADD_SETTING("ejectKeybind", std::string(""));
    ADD_SETTING("enabledModulesOnTop", true);
    ADD_SETTING("rgb_speed", 1.0f);
    ADD_SETTING("rgb_saturation", 1.0f);
    ADD_SETTING("rgb_value", 1.0f);
    ADD_SETTING("pixelateFonts", false);
    ADD_SETTING("modules_font_scale", 1.0f);
    ADD_SETTING("gui_font_scale", 1.0f);
    ADD_SETTING("overrideFontWeight", false);
    ADD_SETTING("fontWeight", std::string("Normal"));
    ADD_SETTING("nologoicon", false);
    ADD_SETTING("nochaticon", false);
    ADD_SETTING("singlewatermark", false);
    ADD_SETTING("watermarkduplicates", true);
    ADD_SETTING("currentConfig", std::string("default.json"));
    ADD_SETTING("resettableSettings", true);
    ADD_SETTING("clearTextBoxWhenClicked", true);
    ADD_SETTING("dotcmdprefix", std::string("."));
    ADD_SETTING("autosearch", false);

    if (hasLegacySettings && !softLoadLegacy)
        settings.getSettingByName<std::string>("currentConfig")->value =
            legacySettings.getSettingByName<std::string>("currentConfig")->value + ".json";

    loadAvailableConfigs();
    SavePrivate();

    if (!privateInit)
    {
        LoadSettings();
    }

    if (hasLegacySettings)
    {
        LoadPrivate();
    }

    Logger::success("5");

    FlarialGUI::ExtractImageResource(IDR_RED_LOGO_PNG, "red-logo.png", "PNG");
    FlarialGUI::ExtractImageResource(IDR_CYAN_LOGO_PNG, "dev-logo.png", "PNG");
    FlarialGUI::ExtractImageResource(IDR_GAMER_LOGO_PNG, "gamer-logo.png", "PNG");
    FlarialGUI::ExtractImageResource(IDR_BOOSTER_LOGO_PNG, "booster-logo.png", "PNG");
    FlarialGUI::ExtractImageResource(IDR_WHITE_LOGO_PNG, "white-logo.png", "PNG");
    FlarialGUI::ExtractImageResource(IDR_SUPPORTER_LOGO_PNG, "supporter-logo.png", "PNG");
    FlarialGUI::ExtractImageResource(IDR_MEDIA_LOGO_PNG, "media-logo.png", "PNG");

    FlarialGUI::LoadFont(IDR_FONT_TTF);
    FlarialGUI::LoadFont(IDR_FONT_BOLD_TTF);
    FlarialGUI::LoadFont(IDR_MINECRAFTIA_TTF);

    Logger::success("6");

    RegisterActivationHandler();
    HookManager::initialize();
    MH_ApplyQueued();

    // Initialize expression formatting system (for color functions in HUD labels)
    ExpressionFormat::initialize();

    ModuleManager::initialize();

    CommandManager::initialize();

    ScriptManager::initialize();

    PerformPostLegacySetup();
    init = true;
    initMgr.setInitialized(true);
}

// Forward to InitializationManager
void Client::PerformPostLegacySetup()
{
    InitializationManager::instance().performPostLegacySetup();
}

// Forward to ClientSettingsManager
void Client::LoadLegacySettings()
{
    ClientSettingsManager::instance().loadLegacySettings();
    // Sync static variables
    hasLegacySettings = ClientSettingsManager::instance().hasLegacySettings();
    softLoadLegacy = ClientSettingsManager::instance().isSoftLoadLegacy();
}

void Client::SavePrivate()
{
    ClientSettingsManager::instance().savePrivate();
}

void Client::LoadPrivate()
{
    ClientSettingsManager::instance().loadPrivate();
    // Sync static variables
    path = ConfigManager::instance().getConfigPath();
}

void Client::SaveSettings()
{
    ClientSettingsManager::instance().saveSettings();
}

void Client::LoadSettings()
{
    ClientSettingsManager::instance().loadSettings();
    // globalSettings is populated directly by loadSettings()
}

void Client::CheckSettingsFile()
{
    ClientSettingsManager::instance().checkSettingsFile();
    // Sync static variable
    privateInit = ClientSettingsManager::instance().isPrivateInitialized();
}

// Static member definitions for backward compatibility
std::string Client::activeConfig;
bool Client::hasLegacySettings = false;
bool Client::softLoadLegacy = false;
bool Client::privateInit = false;
bool Client::savingSettings = false;
bool Client::savingPrivate = false;
nlohmann::json Client::globalSettings;
std::string Client::version;
HMODULE Client::currentModule = nullptr;
std::string Client::path = Utils::getConfigsPath() + "\\default.json";
std::string Client::legacyPath = Utils::getConfigsPath() + "\\main.flarial";
std::string Client::legacyDir = Utils::getConfigsPath() + "\\Legacy";

Client::LocalPlayerState& Client::getPlayerState() {
    static LocalPlayerState inst{};
    return inst;
}

void Client::LocalPlayerState::update(ClientInstance *client) {
    std::unique_lock lock(playerStateMutex);

    _isInWorld = client->getLocalPlayer() != nullptr;

    if(_isInWorld) {
        lastKnownServerIP = SDK::getServerIP();
        lastKnownUserName = client->getLocalPlayer()->getPlayerName();
    }
}

const std::string Client::LocalPlayerState::getServerIP() {
    std::shared_lock lock(playerStateMutex);
    return lastKnownServerIP;
}

const std::string Client::LocalPlayerState::getUserName() {
    std::shared_lock lock(playerStateMutex);
    return lastKnownUserName;
}

const bool Client::LocalPlayerState::isInWorld() {
    std::shared_lock lock(playerStateMutex);
    return _isInWorld;
}

// Note: Client::elapsed and Client::start are defined in main.cpp
