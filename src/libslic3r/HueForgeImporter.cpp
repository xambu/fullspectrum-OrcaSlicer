#include "HueForgeImporter.hpp"
#include "PresetBundle.hpp"
#include "PrintConfig.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <boost/log/trivial.hpp>

namespace Slic3r {

// ---------------------------------------------------------------------------
// JSON parsing
// ---------------------------------------------------------------------------

// Try to read a float from a JSON value that may be float, int, or string.
static float json_to_float(const nlohmann::json &v, float fallback = 0.f)
{
    if (v.is_number())
        return v.get<float>();
    if (v.is_string()) {
        try {
            return std::stof(v.get<std::string>());
        } catch (...) {}
    }
    return fallback;
}

// Try to read a string field, return "" if absent or not a string.
static std::string json_str(const nlohmann::json &obj, const std::string &key)
{
    if (!obj.contains(key))
        return {};
    const auto &v = obj[key];
    if (v.is_string())
        return v.get<std::string>();
    return {};
}

std::vector<HueForgeEntry> hueforge_parse_json(const std::string &json_path)
{
    std::vector<HueForgeEntry> entries;

    std::ifstream ifs(json_path);
    if (!ifs.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "HueForgeImporter: cannot open " << json_path;
        return entries;
    }

    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const nlohmann::json::parse_error &e) {
        BOOST_LOG_TRIVIAL(error) << "HueForgeImporter: JSON parse error: " << e.what();
        return entries;
    }

    // Accept top-level array or {"filaments": [...]} wrapper.
    nlohmann::json arr;
    if (root.is_array()) {
        arr = root;
    } else if (root.is_object()) {
        if (root.contains("filaments") && root["filaments"].is_array())
            arr = root["filaments"];
        else if (root.contains("data") && root["data"].is_array())
            arr = root["data"];
        else {
            BOOST_LOG_TRIVIAL(error) << "HueForgeImporter: unexpected JSON structure";
            return entries;
        }
    } else {
        BOOST_LOG_TRIVIAL(error) << "HueForgeImporter: root must be array or object";
        return entries;
    }

    for (const auto &item : arr) {
        if (!item.is_object())
            continue;

        HueForgeEntry e;
        // Common key variants in HueForge databases.
        e.name     = json_str(item, "name");
        if (e.name.empty()) e.name = json_str(item, "Name");
        e.brand    = json_str(item, "brand");
        if (e.brand.empty()) e.brand = json_str(item, "Brand");
        if (e.brand.empty()) e.brand = json_str(item, "manufacturer");
        e.material = json_str(item, "material");
        if (e.material.empty()) e.material = json_str(item, "Material");
        if (e.material.empty()) e.material = json_str(item, "type");

        // Color: "#RRGGBB" or "RRGGBB".
        std::string color = json_str(item, "color");
        if (color.empty()) color = json_str(item, "Color");
        if (color.empty()) color = json_str(item, "hex");
        if (!color.empty() && color[0] != '#')
            color = "#" + color;
        e.hex_color = color;

        // TD value: try td, td1s, transmission_distance, TD.
        float td = 0.f;
        for (const char *key : {"td", "td1s", "transmission_distance", "TD", "Td"}) {
            if (item.contains(key)) {
                td = json_to_float(item[key]);
                if (td > 0.f)
                    break;
            }
        }
        e.td = td;

        if (e.name.empty() && e.brand.empty())
            continue; // skip empty entries

        entries.push_back(std::move(e));
    }

    BOOST_LOG_TRIVIAL(info) << "HueForgeImporter: parsed " << entries.size() << " entries from " << json_path;
    return entries;
}

// ---------------------------------------------------------------------------
// Fuzzy matching
// ---------------------------------------------------------------------------

std::string hueforge_normalize(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c))
            out.push_back(static_cast<char>(std::tolower(c)));
        // strip spaces and special chars
    }
    return out;
}

bool hueforge_fuzzy_match(const std::string &a_norm, const std::string &b_norm)
{
    if (a_norm.empty() || b_norm.empty())
        return false;
    // One must contain the other.
    return a_norm.find(b_norm) != std::string::npos ||
           b_norm.find(a_norm) != std::string::npos;
}

// Build normalized candidate string from an entry: brand + name + material.
static std::string entry_key(const HueForgeEntry &e)
{
    return hueforge_normalize(e.brand + e.name + e.material);
}

// ---------------------------------------------------------------------------
// Apply to presets
// ---------------------------------------------------------------------------

HueForgeImportResult hueforge_apply_to_presets(const std::vector<HueForgeEntry> &entries,
                                               PresetBundle &bundle)
{
    HueForgeImportResult result;
    result.total_entries = static_cast<int>(entries.size());

    // Build normalized lookup table of all filament presets.
    struct PresetKey {
        std::string normalized;
        Preset     *preset = nullptr;
    };
    std::vector<PresetKey> preset_keys;
    preset_keys.reserve(bundle.filaments.size());
    for (Preset &p : bundle.filaments) {
        if (p.is_system && p.name.find("Default") != std::string::npos)
            continue; // skip generic defaults
        preset_keys.push_back({ hueforge_normalize(p.name), &p });
    }

    for (const HueForgeEntry &e : entries) {
        const std::string key = entry_key(e);
        bool matched = false;

        for (PresetKey &pk : preset_keys) {
            if (!hueforge_fuzzy_match(key, pk.normalized))
                continue;

            // Write td1s into the matched preset config (coFloats, index 0).
            ConfigOptionFloats *opt = pk.preset->config.option<ConfigOptionFloats>("filament_td1s", true);
            if (opt) {
                if (opt->values.empty()) opt->values.push_back(double(e.td));
                else opt->values[0] = double(e.td);
            } else {
                pk.preset->config.set_key_value("filament_td1s", new ConfigOptionFloats{ double(e.td) });
            }

            ++result.matched;
            matched = true;
            BOOST_LOG_TRIVIAL(info) << "HueForgeImporter: matched \"" << e.brand << " " << e.name
                                    << "\" → \"" << pk.preset->name << "\" td=" << e.td;
            break; // first match wins
        }

        if (!matched) {
            ++result.unmatched;
            result.unmatched_names.push_back(e.brand + " " + e.name);
        }
    }

    return result;
}

} // namespace Slic3r
