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

#if LL_VELOPACK
#include "llvelopack.h"
#include <simdjson.h>
#endif

namespace
{
    void query_vvm_coro()
    {
#if LL_VELOPACK
        U32 updater_service = gSavedSettings.getU32("UpdaterServiceSetting");
        if (updater_service == 0)
        {
            LL_INFOS("Velopack") << "Update checks disabled by user (UpdaterServiceSetting=0)" << LL_ENDL;
            return;
        }

        std::string velopack_override = gSavedSettings.controlExists("UpdaterServiceURL") ?
                                        gSavedSettings.getString("UpdaterServiceURL") : "";
        if (!velopack_override.empty())
        {
            LL_INFOS("Velopack") << "Using custom update URL override: " << velopack_override << LL_ENDL;
            velopack_set_update_url(velopack_override);
            velopack_check_for_updates("", "");
            return;
        }

        // Determine target asset feed name for this platform
#if LL_WINDOWS
        const std::string target_feed = "releases.win.json";
#elif LL_DARWIN
        const std::string target_feed = "releases.osx.json";
#elif LL_LINUX
        const std::string target_feed = "releases.linux.json";
#else
        const std::string target_feed = "";
#endif

        if (target_feed.empty())
        {
            LL_INFOS("Velopack") << "Velopack updater not supported on this platform" << LL_ENDL;
            return;
        }

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
        bool is_alpha_channel = (current_channel.find("Alpha") != std::string::npos);
        bool is_beta_channel = (current_channel.find("Beta") != std::string::npos);

        std::string matched_base_url;
        std::string matched_relnotes;
        std::string matched_tag;

        for (simdjson::dom::element release : releases)
        {
            std::string_view tag;
            if (release["tag_name"].get(tag) != simdjson::SUCCESS)
                continue;

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
                if (is_beta_channel && is_prerelease && tag.find("Alpha") != std::string_view::npos)
                {
                    continue; // Beta user doesn't want Alpha
                }
            }

            // Find platform feed asset in this release
            simdjson::dom::array assets;
            if (release["assets"].get(assets) != simdjson::SUCCESS)
                continue;

            for (simdjson::dom::element asset : assets)
            {
                std::string_view name;
                std::string_view download_url;
                if (asset["name"].get(name) == simdjson::SUCCESS && name == target_feed)
                {
                    if (asset["browser_download_url"].get(download_url) == simdjson::SUCCESS)
                    {
                        // Base URL is download URL minus the /releases.xxx.json filename
                        auto last_slash = download_url.rfind('/');
                        if (last_slash != std::string_view::npos)
                        {
                            matched_base_url = std::string(download_url.substr(0, last_slash));
                            matched_tag = std::string(tag);

                            std::string_view html_url;
                            if (release["html_url"].get(html_url) == simdjson::SUCCESS)
                            {
                                matched_relnotes = std::string(html_url);
                            }
                            break;
                        }
                    }
                }
            }

            if (!matched_base_url.empty())
            {
                break; // Found the best matching release feed
            }
        }

        if (matched_base_url.empty())
        {
            LL_INFOS("Velopack") << "No suitable Velopack release feed found for channel " << current_channel << LL_ENDL;
            return;
        }

        LL_INFOS("Velopack") << "Configured Velopack update feed for " << matched_tag << ": " << matched_base_url << LL_ENDL;
        velopack_set_update_url(matched_base_url);

        if (!matched_relnotes.empty())
        {
            LL_INFOS("Velopack") << "Release notes URL: " << matched_relnotes << LL_ENDL;
            LLEventPumps::instance().obtain("relnotes").post(matched_relnotes);
        }

        velopack_check_for_updates("", matched_relnotes);
#else
        LL_INFOS("VVM") << "Velopack not enabled in this build; update checks skipped" << LL_ENDL;
#endif
    }
}

void initVVMUpdateCheck()
{
    LL_INFOS("VVM") << "Initializing VVM update check" << LL_ENDL;
    LLCoros::instance().launch("VVMUpdateCheck", &query_vvm_coro);
}
