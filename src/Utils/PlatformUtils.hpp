#pragma once

#include <string>

enum class PlatformType {
    Unknown,
    UWP,          // Universal Windows Platform (Store version)
    GDKSecure,    // GDK with Store package origin (Game Pass / MS Store)
    GDKInsecure,  // GDK with non-Store or unknown package origin (sideloaded / custom launcher)
    Win32         // Traditional Win32 application (no package identity)
};

class PlatformUtils {
public:
    // Detects the current platform environment
    static PlatformType detectPlatform();

    // Returns the detected platform as a string
    static std::string getPlatformString();

    // Check if current platform is UWP
    static bool isUWP();

    // Check if current platform is GDK (either Secure or Insecure)
    static bool isGDK();

    // Check if current platform is GDK with Store origin
    static bool isGDKSecure();

    // Check if the process is running under Wine/Proton (Linux/macOS compat layers)
    static bool isWine();

    // Initialize platform detection (called once during startup)
    static void initialize();

    // Get the platform-specific roaming path
    static std::string getRoamingPath();

    // Get the platform-specific local path
    static std::string getLocalPath();

    static void openFolder(const std::string& folderPath);
    static void launchURI(const std::string& uri);
    static void setClipboard(const std::string& text);
    static std::string getClipboard();

private:
    static PlatformType s_detectedPlatform;
    static bool s_initialized;
};
