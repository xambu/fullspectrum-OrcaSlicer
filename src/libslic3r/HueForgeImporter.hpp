#ifndef slic3r_HueForgeImporter_hpp_
#define slic3r_HueForgeImporter_hpp_

#include <string>
#include <vector>

namespace Slic3r {

// ---------------------------------------------------------------------------
// HueForge filament database importer
//
// Parses a HueForge-compatible filament JSON file and matches entries to
// Orca filament presets by fuzzy brand+name+material matching.
// Matched presets have their filament_td1s config option written.
// ---------------------------------------------------------------------------

struct HueForgeEntry
{
    std::string name;
    std::string brand;
    std::string material;
    std::string hex_color; // "#RRGGBB"
    float       td = 0.f;
};

struct HueForgeImportResult
{
    int total_entries  = 0; // entries parsed from JSON
    int matched        = 0; // presets updated with TD
    int unmatched      = 0; // entries with no preset match
    std::vector<std::string> unmatched_names; // names of unmatched entries
};

// Parse a HueForge JSON file.
// Returns parsed entries on success; empty on parse error.
std::vector<HueForgeEntry> hueforge_parse_json(const std::string &json_path);

// Fuzzy string normalizer: lowercase, strips non-alphanumeric chars.
std::string hueforge_normalize(const std::string &s);

// Returns true if the two normalised strings are "close enough" to match.
// Strategy: after normalizing, check if one contains the other (or vice-versa).
bool hueforge_fuzzy_match(const std::string &a_norm, const std::string &b_norm);

// Write td values from `entries` to matching filament presets in `bundle`.
// `bundle` must not be null.  Returns an import result summary.
class PresetBundle;
HueForgeImportResult hueforge_apply_to_presets(const std::vector<HueForgeEntry> &entries,
                                               PresetBundle &bundle);

} // namespace Slic3r

#endif // slic3r_HueForgeImporter_hpp_
