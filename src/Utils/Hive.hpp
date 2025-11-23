#pragma once

#include <string>
#include <windows.h>
#include <unknwn.h>
#include <wininet.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <ImGui/imgui.h>
#include <lib/json/json.hpp>
#include <Utils/Logger/Logger.hpp>

#pragma comment(lib, "wininet.lib")



// Refer to https://support.playhive.com/in-game-unlocks/
static const std::vector<int> xpTable = {
    0,        // Level 0
    0,        // Level 1
    150,      // Level 2
    450,      // Level 3
    900,      // Level 4
    1500,     // Level 5
    2250,     // Level 6
    3150,     // Level 7
    4200,     // Level 8
    5400,     // Level 9
    6750,     // Level 10
    8250,     // Level 11
    9900,     // Level 12
    11700,    // Level 13
    13650,    // Level 14
    15750,    // Level 15
    18000,    // Level 16
    20400,    // Level 17
    22950,    // Level 18
    25650,    // Level 19
    28500,    // Level 20
    31500,    // Level 21
    34650,    // Level 22
    37950,    // Level 23
    41400,    // Level 24
    45000,    // Level 25
    48750,    // Level 26
    52650,    // Level 27
    56700,    // Level 28
    60900,    // Level 29
    65250,    // Level 30
    69750,    // Level 31
    74400,    // Level 32
    79200,    // Level 33
    84150,    // Level 34
    89250,    // Level 35
    94500,    // Level 36
    99900,    // Level 37
    105450,   // Level 38
    111150,   // Level 39
    117000,   // Level 40
    123000,   // Level 41
    129150,   // Level 42
    135450,   // Level 43
    141900,   // Level 44
    148500,   // Level 45
    155250,   // Level 46
    162150,   // Level 47
    169200,   // Level 48
    176400,   // Level 49
    183750,   // Level 50
    191250,   // Level 51
    198900,   // Level 52
    206550,   // Level 53
    214200,   // Level 54
    221850,   // Level 55
    229500,   // Level 56
    237150,   // Level 57
    244800,   // Level 58
    252450,   // Level 59
    260100,   // Level 60
    267750,   // Level 61
    275400,   // Level 62
    283050,   // Level 63
    290700,   // Level 64
    298350,   // Level 65
    306000,   // Level 66
    313650,   // Level 67
    321300,   // Level 68
    328950,   // Level 69
    336600,   // Level 70
    344250,   // Level 71
    351900,   // Level 72
    359550,   // Level 73
    367200,   // Level 74
    374850,   // Level 75
    382500,   // Level 76
    390150,   // Level 77
    397800,   // Level 78
    405450,   // Level 79
    413100,   // Level 80
    420750,   // Level 81
    428400,   // Level 82
    436050,   // Level 83
    443700,   // Level 84
    451350,   // Level 85
    459000,   // Level 86
    466650,   // Level 87
    474300,   // Level 88
    481950,   // Level 89
    489600,   // Level 90
    497250,   // Level 91
    504900,   // Level 92
    512550,   // Level 93
    520200,   // Level 94
    527850,   // Level 95
    535500,   // Level 96
    543150,   // Level 97
    550800,   // Level 98
    558450,   // Level 99
    566100    // Level 100
};



namespace Hive {

    struct httpResponse {
        std::string response;
        int httpCode;
    };

    class PlayerStats {
    private:
        float fkdr;
        float kd;
        float winRate;
        int level;
        int victories;
        int losses;
        int kills;
        int deaths;
        int code = 0;
        int prestige = 0;

    public:
        explicit PlayerStats(float fkdr = 0.0f, float kd = 0.0f, float winRate = 0.0f)
            : fkdr(fkdr), kd(kd), winRate(winRate), level(0), victories(0), losses(0), kills(0), deaths(0)
        {
        }

        void setFKDR(double fkdrValue) { fkdr = fkdrValue; }
        void setKD(double kdValue) { kd = kdValue; }
        void setWinRate(double winRateValue) { winRate = winRateValue; }
        void setCode(int codeValue){ code = codeValue; }
        void setLevel(int levelValue){ level = levelValue; }
        void setVictories(int victoriesValue){ victories = victoriesValue; }
        void setLosses(int lossesValue){ losses = lossesValue; }
        void setKills(int killsValue){ kills = killsValue; }
        void setDeaths(int deathsValue){ deaths = deathsValue; }
        void setPrestige(int pres){ prestige = pres; }

        float getFKDR() const { return fkdr; }
        float getKD() const { return kd; }
        float getWinRate() const { return winRate; }
        int getLevel() const { return level; }
        int getVictories() const { return victories; }
        int getLosses() const { return losses; }
        int getKills() const { return kills; }
        int getDeaths() const { return deaths; }
        int getCode() const { return code; };
        int getIntPrestige() const { return prestige; }
        std::string getPrestige() const {
            std::string p = "";
            if(prestige != 0) p = "[";
            if(prestige == 1) p += "I";
            if(prestige == 2) p += "II";
            if(prestige == 3) p += "III";
            if(prestige == 4) p += "IV";
            if(prestige == 5) p += "V";
            if(prestige != 0) p += "] ";
            return p;
        };
    };

    inline double roundToSecond(float value) {
        return std::round(value * 100.0f) / 100.0f;
    }

    inline httpResponse GetString(const std::string &URL) {
        HINTERNET interwebs = InternetOpenA(fmt::format("FlarialClient/{}", COMMIT_HASH).c_str(), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, NULL);
        HINTERNET urlFile;
        std::string rtn;
        DWORD statusCode = 0;
        DWORD bufferSize = sizeof(statusCode);
        DWORD randomShit = 0;

        if (interwebs) {
            urlFile = InternetOpenUrlA(interwebs, URL.c_str(), NULL, NULL, NULL, NULL);
            HttpQueryInfoA(urlFile, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &bufferSize, &randomShit);
            if (urlFile) {
                char buffer[2000];
                DWORD bytesRead;
                do {
                    InternetReadFile(urlFile, buffer, 2000, &bytesRead);
                    rtn.append(buffer, bytesRead);
                    memset(buffer, 0, 2000);
                } while (bytesRead);
                InternetCloseHandle(urlFile);
            }
            InternetCloseHandle(interwebs);
        }

        return {rtn, (int)statusCode};
    }

    inline std::string replaceAll(std::string subject, const std::string &search, const std::string &replace) {
        size_t pos = 0;
        while ((pos = subject.find(search, pos)) != std::string::npos) {
            subject.replace(pos, search.length(), replace);
            pos += replace.length();
        }
        return subject;
    }

    inline int getLevelFromXP(int xp, int max = 100) {
        if (xp <= 0) return 1;

        int maxXP = xpTable[max];

        if (xp >= maxXP) {
            return max;
        }
        else {
            auto it = std::upper_bound(xpTable.begin(), xpTable.end(), xp);
            int level = static_cast<int>(it - xpTable.begin()) - 1;
            return level;
        }
    }

    inline Hive::PlayerStats GetStats(const std::string &gameId, const std::string &username) {

        PlayerStats stats;

        try {
            std::string url = "https://api.playhive.com/v0/game/all/" + gameId + "/" + username;

            httpResponse response = GetString(url);
            std::string jsonResponse = response.response;

            // Logger::info("{}", response.httpCode);
            if (response.httpCode != 200) {
                stats.setCode(response.httpCode == 404 ? 1:3);
                return stats;
            }

            nlohmann::json jsonData = nlohmann::json::parse(jsonResponse);

            // Logger::info("{}", jsonResponse);
            if (!jsonData.contains("first_played")) {
                stats.setCode(2);
                return stats;
            }

            if (gameId == "bed") {
                if (
                    !jsonData.contains("final_kills") ||
                    !jsonData.contains("kills")
                    || !jsonData.contains("deaths")
                    || !jsonData.contains("victories")
                    || !jsonData.contains("played")
                    || !jsonData.contains("xp")
                    || !jsonData.contains("beds_destroyed")
                ) {
                    stats.setCode(2);
                    return stats;
                }

                int finalKills = jsonData["final_kills"].get<int>();
                int xp = jsonData["xp"].get<int>();
                int kills = jsonData["kills"].get<int>();
                int deaths = jsonData["deaths"].get<int>();
                int victories = jsonData["victories"].get<int>();
                int played = jsonData["played"].get<int>();

                int finalDeaths = played - victories;

                stats.setFKDR(roundToSecond((float) finalKills / (float) finalDeaths));
                stats.setKD(roundToSecond((float) kills / (float) deaths));
                stats.setWinRate(std::round(((float) victories / (float) played) * 100.0f));
                stats.setLevel(getLevelFromXP(xp));
                stats.setVictories(victories);
                stats.setLosses(played - victories);
                stats.setKills(kills);
                stats.setDeaths(deaths);

                // for future:
                // if (jsonData.contains("prestige")) stats.setPrestige(jsonData["prestige"].get<int>());
            }

            if (gameId == "sky") {
                if (
                    !jsonData.contains("kills")
                    || !jsonData.contains("deaths")
                    || !jsonData.contains("victories")
                    || !jsonData.contains("played")
                    || !jsonData.contains("xp")
                ) {
                    stats.setCode(2);
                    return stats;
                }

                int kills = jsonData["kills"].get<int>();
                int deaths = jsonData["deaths"].get<int>();
                int victories = jsonData["victories"].get<int>();
                int played = jsonData["played"].get<int>();
                int xp = jsonData["xp"].get<int>();

                stats.setFKDR(roundToSecond((float) -1.f));
                stats.setKD(roundToSecond((float) kills / (float) deaths));
                stats.setWinRate(std::round(((float) victories / (float) played) * 100.0f));
                stats.setLevel(getLevelFromXP(xp));
                stats.setVictories(victories);
                stats.setLosses(played - victories);
                stats.setKills(kills);
                stats.setDeaths(deaths);

                if (jsonData.contains("prestige")) stats.setPrestige(jsonData["prestige"].get<int>());
            }

            if (gameId == "murder") {
                if (
                    !jsonData.contains("murders") ||
                    !jsonData.contains("murderer_eliminations") ||
                    !jsonData.contains("deaths") ||
                    !jsonData.contains("victories") ||
                    !jsonData.contains("played") ||
                    !jsonData.contains("xp")
                ) {
                    stats.setCode(2);
                    return stats;
                }

                int murders = jsonData["murders"].get<int>();
                int eliminations = jsonData["murderer_eliminations"].get<int>();
                int deaths = jsonData["deaths"].get<int>();
                int victories = jsonData["victories"].get<int>();
                int played = jsonData["played"].get<int>();
                int xp = jsonData["xp"].get<int>();

                stats.setFKDR(roundToSecond(-1.0f));
                stats.setKD(roundToSecond((float) murders / (float) deaths));
                stats.setWinRate(std::round(((float) victories / (float) played) * 100.0f));
                stats.setLevel(getLevelFromXP(xp));
                stats.setVictories(victories);
                stats.setLosses(played - victories);
                stats.setKills(eliminations);
                stats.setDeaths(deaths);

                if (jsonData.contains("prestige")) stats.setPrestige(jsonData["prestige"].get<int>());
            }

            if (gameId == "ctf") {
                if (
                    !jsonData.contains("kills") ||
                    !jsonData.contains("deaths") ||
                    !jsonData.contains("victories") ||
                    !jsonData.contains("played") ||
                    !jsonData.contains("xp")
                ) {
                    stats.setCode(2);
                    return stats;
                }

                int kills = jsonData["kills"].get<int>();
                int deaths = jsonData["deaths"].get<int>();
                int victories = jsonData["victories"].get<int>();
                int played = jsonData["played"].get<int>();
                int xp = jsonData["xp"].get<int>();
                // int flagsCaptured = jsonData["flags_captured"].get<int>();
                // int flagsReturned = jsonData["flags_returned"].get<int>();
                // int assists = jsonData["assists"].get<int>();

                stats.setKD(roundToSecond((float) kills / (float) deaths));
                stats.setFKDR(roundToSecond(-1.0f));
                stats.setWinRate(std::round(((float) victories / (float) played) * 100.0f));
                stats.setLevel(getLevelFromXP(xp, 50));
                stats.setVictories(victories);
                stats.setLosses(played - victories);
                stats.setKills(kills);
                stats.setDeaths(deaths);
            }


            return stats;
        } catch (...) {
            return stats;
        }
    }

}