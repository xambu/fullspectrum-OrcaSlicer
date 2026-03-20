#ifndef slic3r_HueForgeSync_hpp_
#define slic3r_HueForgeSync_hpp_

#include <functional>
#include <string>

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// HueForge filament database auto-sync (Option C)
//
// Fetches the HueForge public filament database in a background thread,
// parses it exactly like the manual JSON import, and applies TD values to
// matched filament presets.
//
// Cache: {user_data_dir}/hueforge_filament_cache.json
// Re-fetched when cache is older than 7 days, or on explicit manual sync.
// ---------------------------------------------------------------------------

struct HueForgeSyncResult
{
    bool        success    = false;
    bool        from_cache = false;
    int         total      = 0;
    int         matched    = 0;
    int         unmatched  = 0;
    std::string error_msg; // non-empty on failure
};

using HueForgeCallback = std::function<void(HueForgeSyncResult)>;

// Returns the path used for the local cache file.
std::string hueforge_cache_path();

// Returns true if the cache exists and is fresh (younger than max_age_days).
bool hueforge_cache_is_fresh(int max_age_days = 7);

// Trigger a background sync.
//   - If force=false and the cache is fresh, returns immediately after loading cache.
//   - If force=true, always fetches from the network.
//   - `callback` is invoked on the GUI thread when done (success or failure).
//   - On network failure the cached data is reused if available.
//   - Never blocks the calling thread.
void hueforge_sync_async(bool force, HueForgeCallback callback);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_HueForgeSync_hpp_
