/**
 * @file llvvmquery.cpp
 * @brief Query the Viewer Version Manager (VVM) for update information
 *
 * $LicenseInfo:firstyear=2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "llvvmquery.h"

#include "llcorehttputil.h"
#include "llcoros.h"
#include "llevents.h"
#include "llviewernetwork.h"
#include "llversioninfo.h"
#include "llviewercontrol.h"
#include "llhasheduniqueid.h"
#include "lluri.h"
#include "llsys.h"
#include "llstring.h"
#include "llnotificationsutil.h"
#include "llweb.h"

#include <simdjson.h>

#if LL_VELOPACK
#include "llvelopack.h"
#endif

namespace
{
    // Extracts numeric version components (major, minor, patch, build) from a text string.
    // Handles formats like "26.4.0.63807", "v26.4.1", "26.4", etc.
    static bool extract_version(std::string_view str, int& out_major, int& out_minor, int& out_patch, unsigned long long& out_build)
    {
        out_major = 0;
        out_minor = 0;
        out_patch = 0;
        out_build = 0;

        for (size_t i = 0; i < str.size(); ++i)
        {
            if (isdigit(static_cast<unsigned char>(str[i])))
            {
                std::string sub(str.substr(i));
                int maj = 0, min = 0, pat = 0;
                unsigned long long bld = 0;
                int matched = sscanf(sub.c_str(), "%d.%d.%d.%llu", &maj, &min, &pat, &bld);
                if (matched >= 3)
                {
                    out_major = maj;
                    out_minor = min;
                    out_patch = pat;
                    out_build = (matched >= 4) ? bld : 0;
                    return true;
                }
                else if (matched == 2)
                {
                    out_major = maj;
                    out_minor = min;
                    out_patch = 0;
                    out_build = 0;
                    return true;
                }
            }
        }
        return false;
    }

    // Compares release version against currently running viewer version.
    // Returns true if the candidate release is strictly newer.
    static bool is_newer_than_running(std::string_view release_name, std::string_view tag_name)
    {
        int rel_major = 0, rel_minor = 0, rel_patch = 0;
        unsigned long long rel_build = 0;

        // Try extracting from release_name first (e.g. "Vayu Beta 26.4.0.63807")
        bool has_ver = extract_version(release_name, rel_major, rel_minor, rel_patch, rel_build);
        if (!has_ver)
        {
            // Fall back to extracting from tag_name (e.g. "v26.4.1" or "26.4.0.63807")
            has_ver = extract_version(tag_name, rel_major, rel_minor, rel_patch, rel_build);
        }

        const LLVersionInfo& vi = LLVersionInfo::instance();
        int cur_major = vi.getMajor();
        int cur_minor = vi.getMinor();
        int cur_patch = vi.getPatch();
        unsigned long long cur_build = vi.getBuild();

        if (has_ver)
        {
            if (rel_major != cur_major) return rel_major > cur_major;
            if (rel_minor != cur_minor) return rel_minor > cur_minor;
            if (rel_patch != cur_patch) return rel_patch > cur_patch;
            if (rel_build > 0 && cur_build > 0 && rel_build != cur_build)
            {
                return rel_build > cur_build;
            }
            return false;
        }

        // If no dotted versions were found, don't trigger unsolicited update prompts
        return false;
    }

    void query_vvm_coro()
    {
        U32 updater_service = gSavedSettings.getU32("UpdaterServiceSetting");
        if (updater_service == 0)
        {
            LL_INFOS("Velopack") << "Update checks disabled by user (UpdaterServiceSetting=0)" << LL_ENDL;
            return;
        }

        std::string velopack_override = gSavedSettings.controlExists("UpdaterServiceURL") ?
                                        gSavedSettings.getString("UpdaterServiceURL") : "";
#if LL_VELOPACK
        if (!velopack_override.empty())
        {
            LL_INFOS("Velopack") << "Using custom update URL override: " << velopack_override << LL_ENDL;
            velopack_set_update_url(velopack_override);
            velopack_check_for_updates("", "");
            return;
        }
#endif

        // Determine target asset feed and installer extension for this platform
#if LL_WINDOWS
        const std::string target_feed = "releases.win.json";
        const std::string installer_ext = ".exe";
#elif LL_DARWIN
        const std::string target_feed = "releases.osx.json";
        const std::string installer_ext = ".dmg";
#elif LL_LINUX
        const std::string target_feed = "releases.linux.json";
        const std::string installer_ext = ".tar.xz";
#else
        const std::string target_feed = "";
        const std::string installer_ext = "";
#endif

        // Query GitHub Releases API directly for Shadowolf7/Vayu-Viewer
        std::string api_url = "https://api.github.com/repos/Shadowolf7/Vayu-Viewer/releases";
        LL_INFOS("Velopack") << "Querying GitHub Releases for Vayu update feed: " << api_url << LL_ENDL;

        LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
        auto httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("VayuUpdateCheck", httpPolicy);
        auto httpRequest = std::make_shared<LLCore::HttpRequest>();
        auto httpOpts = std::make_shared<LLCore::HttpOptions>();
        auto httpHeaders = std::make_shared<LLCore::HttpHeaders>();

        httpOpts->setFollowRedirects(true);
        httpHeaders->append("User-Agent", "Vayu-Viewer");
        httpHeaders->append("Accept", "application/vnd.github+json");

        LLSD result = httpAdapter->getRawAndSuspend(httpRequest, api_url, httpOpts, httpHeaders);
        LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);
        if (!status)
        {
            LL_WARNS("Velopack") << "Failed to fetch GitHub releases: " << status.toString() << LL_ENDL;
            return;
        }

        const LLSD::Binary& rawBody = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
        std::string json_str(rawBody.begin(), rawBody.end());

        simdjson::dom::parser parser;
        simdjson::dom::element releases;
        if (parser.parse(json_str).get(releases) != simdjson::SUCCESS || !releases.is_array())
        {
            LL_WARNS("Velopack") << "Failed to parse GitHub releases JSON" << LL_ENDL;
            return;
        }

        std::string current_channel = LLVersionInfo::instance().getChannel();
        bool willing_to_test = gSavedSettings.getBOOL("UpdaterWillingToTest");

        std::string channel_lower = current_channel;
        LLStringUtil::toLower(channel_lower);
        bool is_alpha_channel = (channel_lower.find("alpha") != std::string::npos);
        bool is_beta_channel = (channel_lower.find("beta") != std::string::npos);

        std::string matched_base_url;
        std::string matched_download_url;
        std::string matched_relnotes;
        std::string matched_tag;
        bool found_candidate = false;

        for (simdjson::dom::element release : releases)
        {
            std::string_view tag;
            if (release["tag_name"].get(tag) != simdjson::SUCCESS)
                continue;

            std::string tag_lower(tag);
            LLStringUtil::toLower(tag_lower);

            bool is_prerelease = false;
            if (release["prerelease"].get(is_prerelease) != simdjson::SUCCESS)
            {
                is_prerelease = false;
            }

            // Channel filter matching:
            // 1. If running Alpha, accept any release matching Alpha or newer
            // 2. If running Beta, accept Beta or stable Release
            // 3. If running Release (stable), accept only non-prereleases unless willing_to_test is true
            if (!willing_to_test)
            {
                if (!is_alpha_channel && !is_beta_channel && is_prerelease)
                {
                    continue; // Stable user doesn't want prereleases
                }
                if (is_beta_channel && is_prerelease && tag_lower.find("alpha") != std::string::npos)
                {
                    continue; // Beta user doesn't want Alpha
                }
            }

            std::string_view release_name;
            if (release["name"].get(release_name) != simdjson::SUCCESS)
            {
                release_name = "";
            }

            // Check if this release is newer than our currently running version
            if (!is_newer_than_running(release_name, tag))
            {
                LL_INFOS("Velopack") << "Candidate release " << tag << " (" << release_name
                                     << ") is not newer than running version ("
                                     << LLVersionInfo::instance().getVersion() << ")" << LL_ENDL;
                break; // Since releases are ordered newest first, no subsequent release will be newer
            }

            found_candidate = true;
            matched_tag = std::string(tag);

            std::string_view html_url;
            if (release["html_url"].get(html_url) == simdjson::SUCCESS)
            {
                matched_relnotes = std::string(html_url);
            }

            // Inspect assets for Velopack feed and/or platform installer
            simdjson::dom::array assets;
            if (release["assets"].get(assets) == simdjson::SUCCESS)
            {
                for (simdjson::dom::element asset : assets)
                {
                    std::string_view name;
                    std::string_view download_url;
                    if (asset["name"].get(name) == simdjson::SUCCESS &&
                        asset["browser_download_url"].get(download_url) == simdjson::SUCCESS)
                    {
                        if (!target_feed.empty() && name == target_feed)
                        {
                            auto last_slash = download_url.rfind('/');
                            if (last_slash != std::string_view::npos)
                            {
                                matched_base_url = std::string(download_url.substr(0, last_slash));
                            }
                        }

                        if (!installer_ext.empty() && name.ends_with(installer_ext))
                        {
                            matched_download_url = std::string(download_url);
                        }
                    }
                }
            }

            break; // Found the best matching candidate release
        }

        if (!found_candidate)
        {
            LL_INFOS("Velopack") << "No newer release available for channel " << current_channel << LL_ENDL;
            return;
        }

#if LL_VELOPACK
        if (!matched_base_url.empty())
        {
            LL_INFOS("Velopack") << "Configured Velopack update feed for " << matched_tag << ": " << matched_base_url << LL_ENDL;
            velopack_set_update_url(matched_base_url);

            if (!matched_relnotes.empty())
            {
                LL_INFOS("Velopack") << "Release notes URL: " << matched_relnotes << LL_ENDL;
                LLEventPumps::instance().obtain("relnotes").post(matched_relnotes);
            }

            velopack_check_for_updates("", matched_relnotes);
            return;
        }
#endif

        // Fallback for platforms or releases without a Velopack feed (e.g. Linux or macOS without feed assets)
        LL_INFOS("Velopack") << "Update available without Velopack feed (" << matched_tag
                             << "), presenting download notification" << LL_ENDL;
        if (!matched_relnotes.empty())
        {
            LL_INFOS("Velopack") << "Release notes URL: " << matched_relnotes << LL_ENDL;
            LLEventPumps::instance().obtain("relnotes").post(matched_relnotes);
        }

        std::string final_download_url = matched_download_url.empty() ? matched_relnotes : matched_download_url;

        LLSD args;
        args["VERSION"] = matched_tag;
        args["URL"] = matched_relnotes.empty() ? final_download_url : matched_relnotes;

        LLSD payload;
        payload["url"] = final_download_url;

        LLNotificationsUtil::add("VayuUpdateAvailable", args, payload,
            [](const LLSD& notification, const LLSD& response)
            {
                S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
                if (option == 0) // "Download"
                {
                    std::string url = notification["payload"]["url"].asString();
                    if (!url.empty())
                    {
                        LLWeb::loadURLExternal(url);
                    }
                }
            });
    }
}

void initVVMUpdateCheck()
{
    LL_INFOS("VVM") << "Initializing VVM update check" << LL_ENDL;
    LLCoros::instance().launch("VVMUpdateCheck", &query_vvm_coro);
}
