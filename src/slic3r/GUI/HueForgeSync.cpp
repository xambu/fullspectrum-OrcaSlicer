#include "HueForgeSync.hpp"
#include "GUI_App.hpp"

#include "libslic3r/HueForgeImporter.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include "../Utils/Http.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <wx/app.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace Slic3r {
namespace GUI {

namespace fs = boost::filesystem;

// ---------------------------------------------------------------------------
// Public HueForge filament database endpoint.
// This is the community-maintained JSON on GitHub releases; it contains no
// proprietary data and is licensed permissively by the HueForge project.
// URL is intentionally stored as a single string so it can be changed easily.
static constexpr const char *k_hueforge_db_url =
    "https://raw.githubusercontent.com/cabbagecreature/hueforge-filaments/main/filaments.json";

static constexpr int k_cache_max_age_days = 7;
static constexpr long k_connect_timeout_s = 10;
static constexpr long k_total_timeout_s   = 30;

// ---------------------------------------------------------------------------

std::string hueforge_cache_path()
{
    fs::path p = fs::path(data_dir()) / "hueforge_filament_cache.json";
    return p.string();
}

bool hueforge_cache_is_fresh(int max_age_days)
{
    const std::string path = hueforge_cache_path();
    if (!fs::exists(path))
        return false;

    const auto last_write = fs::last_write_time(path);
    const auto now        = std::time(nullptr);
    const auto age_s      = static_cast<long>(now) - static_cast<long>(last_write);
    const long max_age_s  = static_cast<long>(max_age_days) * 24 * 3600;
    return age_s < max_age_s;
}

// Post a callback onto the GUI thread via a wxQueuedCall.
static void post_to_gui(HueForgeCallback cb, HueForgeSyncResult result)
{
    // wxGetApp().CallAfter posts the lambda to the main event loop.
    wxGetApp().CallAfter([cb = std::move(cb), result = std::move(result)]() mutable {
        cb(std::move(result));
    });
}

// Apply entries from `json_text` to presets and populate `result`.
static void apply_json_to_presets(const std::string &json_text,
                                  bool               from_cache,
                                  HueForgeSyncResult &result)
{
    // Write to cache file (even if reading from network).
    const std::string cache = hueforge_cache_path();
    if (!from_cache) {
        try {
            std::ofstream ofs(cache);
            ofs << json_text;
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "HueForgeSync: failed to write cache to " << cache;
        }
    }

    // Parse a temporary file (hueforge_parse_json takes a file path).
    // Write json_text to a temp path and parse it.
    const std::string tmp_path = cache + ".tmp";
    {
        std::ofstream ofs(tmp_path);
        ofs << json_text;
    }

    const auto entries = hueforge_parse_json(tmp_path);
    fs::remove(tmp_path);

    if (entries.empty()) {
        result.success   = false;
        result.error_msg = "No filament entries found in database";
        return;
    }

    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (!bundle) {
        result.success   = false;
        result.error_msg = "Preset bundle not available";
        return;
    }

    const auto import = hueforge_apply_to_presets(entries, *bundle);
    result.success    = true;
    result.from_cache = from_cache;
    result.total      = import.total_entries;
    result.matched    = import.matched;
    result.unmatched  = import.unmatched;
}

void hueforge_sync_async(bool force, HueForgeCallback callback)
{
    const std::string cache_path = hueforge_cache_path();

    // Use cache if fresh and not forced.
    if (!force && hueforge_cache_is_fresh(k_cache_max_age_days)) {
        std::ifstream ifs(cache_path);
        if (ifs.is_open()) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            const std::string cached_json = ss.str();

            HueForgeSyncResult result;
            apply_json_to_presets(cached_json, /*from_cache=*/true, result);
            post_to_gui(std::move(callback), std::move(result));
            return;
        }
    }

    // Fetch from network in background thread.
    Http::get(k_hueforge_db_url)
        .timeout_connect(k_connect_timeout_s)
        .timeout_max(k_total_timeout_s)
        .on_complete([callback](std::string body, unsigned http_status) mutable {
            if (http_status < 200 || http_status >= 300) {
                HueForgeSyncResult result;
                result.success   = false;
                result.error_msg = "HTTP " + std::to_string(http_status);
                post_to_gui(std::move(callback), std::move(result));
                return;
            }
            HueForgeSyncResult result;
            apply_json_to_presets(body, /*from_cache=*/false, result);
            post_to_gui(std::move(callback), std::move(result));
        })
        .on_error([callback, cache_path](std::string body, std::string error, unsigned) mutable {
            BOOST_LOG_TRIVIAL(warning) << "HueForgeSync: network error: " << error;

            // Try to fall back to cached data.
            std::ifstream ifs(cache_path);
            if (ifs.is_open()) {
                std::ostringstream ss;
                ss << ifs.rdbuf();
                const std::string cached_json = ss.str();
                if (!cached_json.empty()) {
                    HueForgeSyncResult result;
                    apply_json_to_presets(cached_json, /*from_cache=*/true, result);
                    result.error_msg = "Network error (used cached data): " + error;
                    post_to_gui(std::move(callback), std::move(result));
                    return;
                }
            }

            // No cache available — report failure silently.
            HueForgeSyncResult result;
            result.success   = false;
            result.error_msg = error;
            post_to_gui(std::move(callback), std::move(result));
        })
        .perform(); // non-blocking background thread
}

} // namespace GUI
} // namespace Slic3r
