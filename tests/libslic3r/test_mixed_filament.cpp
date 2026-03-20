#include <catch2/catch_all.hpp>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/MixedFilament.hpp"
#include "libslic3r/HueForgeImporter.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <fstream>
#include <sstream>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::ContainsSubstring;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> make_colors(int n, const std::string &hex = "#FF0000")
{
    return std::vector<std::string>(size_t(n), hex);
}

// ---------------------------------------------------------------------------
// MixedFilament struct
// ---------------------------------------------------------------------------

TEST_CASE("MixedFilament default values are sane", "[MixedFilament]")
{
    MixedFilament mf;
    REQUIRE(mf.component_a == 1);
    REQUIRE(mf.component_b == 2);
    REQUIRE(mf.ratio_a == 1);
    REQUIRE(mf.ratio_b == 1);
    REQUIRE(mf.mix_b_percent == 50);
    REQUIRE(mf.distribution_mode == int(MixedFilament::Simple));
    REQUIRE(mf.enabled);
    REQUIRE_FALSE(mf.deleted);
    REQUIRE_FALSE(mf.custom);
}

TEST_CASE("MixedFilament equality operator", "[MixedFilament]")
{
    MixedFilament a;
    MixedFilament b;
    REQUIRE(a == b);

    b.mix_b_percent = 75;
    REQUIRE(a != b);
}

// ---------------------------------------------------------------------------
// MixedFilamentManager::auto_generate
// ---------------------------------------------------------------------------

TEST_CASE("auto_generate produces C(N,2) pairs for N physical filaments", "[MixedFilamentManager]")
{
    SECTION("2 filaments → 1 pair") {
        MixedFilamentManager mgr;
        mgr.auto_generate(make_colors(2));
        REQUIRE(mgr.mixed_filaments().size() == 1);
        REQUIRE(mgr.enabled_count() == 1);
    }

    SECTION("3 filaments → 3 pairs") {
        MixedFilamentManager mgr;
        mgr.auto_generate(make_colors(3));
        REQUIRE(mgr.mixed_filaments().size() == 3);
        REQUIRE(mgr.enabled_count() == 3);
    }

    SECTION("4 filaments → 6 pairs") {
        MixedFilamentManager mgr;
        mgr.auto_generate(make_colors(4));
        REQUIRE(mgr.mixed_filaments().size() == 6);
        REQUIRE(mgr.enabled_count() == 6);
    }

    SECTION("1 filament → 0 pairs") {
        MixedFilamentManager mgr;
        mgr.auto_generate(make_colors(1));
        REQUIRE(mgr.mixed_filaments().empty());
    }
}

TEST_CASE("auto_generate assigns ascending component IDs (1-based)", "[MixedFilamentManager]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(3));
    const auto &rows = mgr.mixed_filaments();
    // Expected pairs: (1,2), (1,3), (2,3)
    REQUIRE(rows[0].component_a == 1);
    REQUIRE(rows[0].component_b == 2);
    REQUIRE(rows[1].component_a == 1);
    REQUIRE(rows[1].component_b == 3);
    REQUIRE(rows[2].component_a == 2);
    REQUIRE(rows[2].component_b == 3);
}

// ---------------------------------------------------------------------------
// MixedFilamentManager::total_filaments
// ---------------------------------------------------------------------------

TEST_CASE("total_filaments returns physical + enabled mixed count", "[MixedFilamentManager]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(3)); // 3 pairs
    REQUIRE(mgr.total_filaments(3) == 6);
}

// ---------------------------------------------------------------------------
// MixedFilamentManager::is_mixed and mixed_index_from_filament_id
// ---------------------------------------------------------------------------

TEST_CASE("is_mixed returns false for physical IDs and true for virtual IDs", "[MixedFilamentManager]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(2)); // 1 mixed (ID 3)

    REQUIRE_FALSE(mgr.is_mixed(1, 2));
    REQUIRE_FALSE(mgr.is_mixed(2, 2));
    REQUIRE(mgr.is_mixed(3, 2));
    REQUIRE_FALSE(mgr.is_mixed(4, 2)); // out of range
}

// ---------------------------------------------------------------------------
// MixedFilamentManager::resolve
// ---------------------------------------------------------------------------

TEST_CASE("resolve returns physical ID unchanged for non-mixed filament", "[MixedFilamentManager]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(2));
    REQUIRE(mgr.resolve(1, 2, 0) == 1);
    REQUIRE(mgr.resolve(2, 2, 0) == 2);
}

TEST_CASE("resolve alternates components with 1:1 ratio", "[MixedFilamentManager]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(2));
    // Virtual filament 3 = mixed(1,2) with ratio 1:1
    // layer 0 → component_a (1), layer 1 → component_b (2)
    REQUIRE(mgr.resolve(3, 2, 0) == 1);
    REQUIRE(mgr.resolve(3, 2, 1) == 2);
    REQUIRE(mgr.resolve(3, 2, 2) == 1);
    REQUIRE(mgr.resolve(3, 2, 3) == 2);
}

TEST_CASE("resolve with ratio_a=2 ratio_b=1 gives AABAABAAB pattern", "[MixedFilamentManager]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(2));
    auto &mf = mgr.mixed_filaments()[0];
    mf.ratio_a = 2;
    mf.ratio_b = 1;
    // cycle = 3: pos 0,1 → A, pos 2 → B
    REQUIRE(mgr.resolve(3, 2, 0) == 1);
    REQUIRE(mgr.resolve(3, 2, 1) == 1);
    REQUIRE(mgr.resolve(3, 2, 2) == 2);
    REQUIRE(mgr.resolve(3, 2, 3) == 1);
}

// ---------------------------------------------------------------------------
// MixedFilamentManager::serialize_custom_entries / load_custom_entries
// ---------------------------------------------------------------------------

TEST_CASE("serialize and load round-trip preserves all rows", "[MixedFilamentManager][3mf]")
{
    MixedFilamentManager orig;
    orig.auto_generate(make_colors(3));
    // Disable one row
    orig.mixed_filaments()[1].enabled = false;
    orig.mixed_filaments()[1].deleted = true;

    const std::string serialized = orig.serialize_custom_entries();
    REQUIRE_FALSE(serialized.empty());

    MixedFilamentManager loaded;
    loaded.auto_generate(make_colors(3));
    loaded.load_custom_entries(serialized, make_colors(3));

    const auto &orig_rows  = orig.mixed_filaments();
    const auto &loaded_rows = loaded.mixed_filaments();
    REQUIRE(orig_rows.size() == loaded_rows.size());

    for (size_t i = 0; i < orig_rows.size(); ++i) {
        DYNAMIC_SECTION("Row " << i << " enabled flag") {
            REQUIRE(orig_rows[i].enabled == loaded_rows[i].enabled);
        }
        DYNAMIC_SECTION("Row " << i << " deleted flag") {
            REQUIRE(orig_rows[i].deleted == loaded_rows[i].deleted);
        }
        DYNAMIC_SECTION("Row " << i << " components") {
            REQUIRE(orig_rows[i].component_a == loaded_rows[i].component_a);
            REQUIRE(orig_rows[i].component_b == loaded_rows[i].component_b);
        }
    }
}

TEST_CASE("empty serialized string leaves auto rows unchanged", "[MixedFilamentManager][3mf]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(2));
    mgr.load_custom_entries("", make_colors(2));
    REQUIRE(mgr.mixed_filaments().size() == 1);
    REQUIRE(mgr.enabled_count() == 1);
}

TEST_CASE("load_custom_entries ignores rows with out-of-range component IDs", "[MixedFilamentManager][3mf]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(2));
    // component 3 does not exist for a 2-filament setup
    mgr.load_custom_entries("1,3,1,0,50", make_colors(2));
    REQUIRE(mgr.mixed_filaments().size() == 1);
}

// ---------------------------------------------------------------------------
// MixedFilamentManager::normalize_manual_pattern
// ---------------------------------------------------------------------------

TEST_CASE("normalize_manual_pattern accepts valid digit tokens", "[MixedFilamentManager]")
{
    REQUIRE(MixedFilamentManager::normalize_manual_pattern("121212") == "121212");
    REQUIRE(MixedFilamentManager::normalize_manual_pattern("AABB") == "1122");
    REQUIRE(MixedFilamentManager::normalize_manual_pattern("11221122") == "11221122");
}

TEST_CASE("normalize_manual_pattern strips separators", "[MixedFilamentManager]")
{
    REQUIRE(MixedFilamentManager::normalize_manual_pattern("1 2 1 2") == "1212");
    REQUIRE(MixedFilamentManager::normalize_manual_pattern("1-2-1-2") == "1212");
}

TEST_CASE("normalize_manual_pattern returns empty string for invalid input", "[MixedFilamentManager]")
{
    REQUIRE(MixedFilamentManager::normalize_manual_pattern("").empty());
    REQUIRE(MixedFilamentManager::normalize_manual_pattern("xyz").empty());
    REQUIRE(MixedFilamentManager::normalize_manual_pattern("12,").empty()); // trailing comma
}

// ---------------------------------------------------------------------------
// MixedFilamentManager::mix_percent_from_manual_pattern
// ---------------------------------------------------------------------------

TEST_CASE("mix_percent_from_manual_pattern computes B fraction correctly", "[MixedFilamentManager]")
{
    // "2222" → 100% B
    REQUIRE(MixedFilamentManager::mix_percent_from_manual_pattern("2222") == 100);
    // "1111" → 0% B
    REQUIRE(MixedFilamentManager::mix_percent_from_manual_pattern("1111") == 0);
    // "12" → 50% B
    REQUIRE(MixedFilamentManager::mix_percent_from_manual_pattern("12") == 50);
    // "112" → 33% B (rounded)
    REQUIRE(MixedFilamentManager::mix_percent_from_manual_pattern("112") == 33);
}

// ---------------------------------------------------------------------------
// PrintConfig keys exist with correct types and defaults
// ---------------------------------------------------------------------------

TEST_CASE("PrintConfig mixed filament keys are registered with correct defaults", "[PrintConfig][MixedFilament]")
{
    Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();

    SECTION("mixed_filament_gradient_mode default false") {
        const auto *opt = config.option<ConfigOptionBool>("mixed_filament_gradient_mode");
        REQUIRE(opt != nullptr);
        REQUIRE(opt->value == false);
    }

    SECTION("mixed_filament_height_lower_bound default 0.04") {
        const auto *opt = config.option<ConfigOptionFloat>("mixed_filament_height_lower_bound");
        REQUIRE(opt != nullptr);
        REQUIRE_THAT(opt->value, Catch::Matchers::WithinAbs(0.04, 1e-6));
    }

    SECTION("mixed_filament_height_upper_bound default 0.16") {
        const auto *opt = config.option<ConfigOptionFloat>("mixed_filament_height_upper_bound");
        REQUIRE(opt != nullptr);
        REQUIRE_THAT(opt->value, Catch::Matchers::WithinAbs(0.16, 1e-6));
    }

    SECTION("mixed_filament_advanced_dithering default false") {
        const auto *opt = config.option<ConfigOptionBool>("mixed_filament_advanced_dithering");
        REQUIRE(opt != nullptr);
        REQUIRE(opt->value == false);
    }

    SECTION("mixed_filament_definitions default empty") {
        const auto *opt = config.option<ConfigOptionString>("mixed_filament_definitions");
        REQUIRE(opt != nullptr);
        REQUIRE(opt->value.empty());
    }

    SECTION("dithering_z_step_size default 0.0") {
        const auto *opt = config.option<ConfigOptionFloat>("dithering_z_step_size");
        REQUIRE(opt != nullptr);
        REQUIRE_THAT(opt->value, Catch::Matchers::WithinAbs(0.0, 1e-6));
    }

    SECTION("dithering_local_z_mode default false") {
        const auto *opt = config.option<ConfigOptionBool>("dithering_local_z_mode");
        REQUIRE(opt != nullptr);
        REQUIRE(opt->value == false);
    }

    SECTION("dithering_step_painted_zones_only default false") {
        const auto *opt = config.option<ConfigOptionBool>("dithering_step_painted_zones_only");
        REQUIRE(opt != nullptr);
        REQUIRE(opt->value == false);
    }
}

// ---------------------------------------------------------------------------
// Blend colour helpers
// ---------------------------------------------------------------------------

TEST_CASE("blend_color returns a valid hex color string", "[MixedFilamentManager]")
{
    const std::string result = MixedFilamentManager::blend_color("#FF0000", "#0000FF", 1, 1);
    REQUIRE(result.size() == 7);
    REQUIRE(result[0] == '#');
}

TEST_CASE("blend_color with ratio 1:0 returns first color", "[MixedFilamentManager]")
{
    const std::string result = MixedFilamentManager::blend_color("#FF0000", "#0000FF", 1, 0);
    REQUIRE(result == "#FF0000");
}

TEST_CASE("blend_color with ratio 0:1 returns second color", "[MixedFilamentManager]")
{
    const std::string result = MixedFilamentManager::blend_color("#FF0000", "#0000FF", 0, 1);
    REQUIRE(result == "#0000FF");
}

// ---------------------------------------------------------------------------
// remove_physical_filament
// ---------------------------------------------------------------------------

TEST_CASE("remove_physical_filament drops pairs containing the removed ID", "[MixedFilamentManager]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(3)); // pairs: (1,2),(1,3),(2,3)
    mgr.remove_physical_filament(1);   // removes (1,2) and (1,3); (2,3) becomes (1,2)
    REQUIRE(mgr.mixed_filaments().size() == 1);
    REQUIRE(mgr.mixed_filaments()[0].component_a == 1);
    REQUIRE(mgr.mixed_filaments()[0].component_b == 2);
}

// ---------------------------------------------------------------------------
// add_custom_filament
// ---------------------------------------------------------------------------

TEST_CASE("add_custom_filament appends a custom row", "[MixedFilamentManager]")
{
    MixedFilamentManager mgr;
    mgr.auto_generate(make_colors(3));
    const size_t before = mgr.mixed_filaments().size();
    mgr.add_custom_filament(1, 3, 25, make_colors(3));
    REQUIRE(mgr.mixed_filaments().size() == before + 1);
    REQUIRE(mgr.mixed_filaments().back().custom);
    REQUIRE(mgr.mixed_filaments().back().mix_b_percent == 25);
}

// ---------------------------------------------------------------------------
// Remap and stable-ID tests (require PresetBundle)
// ---------------------------------------------------------------------------

namespace {

static std::vector<std::string> split_rows(const std::string &serialized)
{
    std::vector<std::string> rows;
    std::stringstream ss(serialized);
    std::string row;
    while (std::getline(ss, row, ';')) {
        if (!row.empty())
            rows.push_back(row);
    }
    return rows;
}

static std::string join_rows(const std::vector<std::string> &rows)
{
    std::ostringstream ss;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i != 0)
            ss << ';';
        ss << rows[i];
    }
    return ss.str();
}

static unsigned int virtual_id_for_stable_id(const std::vector<MixedFilament> &mixed, size_t num_physical, uint64_t stable_id)
{
    unsigned int next_virtual_id = unsigned(num_physical + 1);
    for (const MixedFilament &mf : mixed) {
        if (!mf.enabled || mf.deleted)
            continue;
        if (mf.stable_id == stable_id)
            return next_virtual_id;
        ++next_virtual_id;
    }
    return 0;
}

} // namespace

TEST_CASE("Mixed filament remap follows stable row ids when same-pair rows reorder", "[MixedFilament]")
{
    PresetBundle bundle;
    bundle.filament_presets = {"Default Filament", "Default Filament"};
    bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values = {"#FF0000", "#0000FF"};
    bundle.update_multi_material_filament_presets();

    auto &mgr = bundle.mixed_filaments;
    auto &mixed = mgr.mixed_filaments();
    REQUIRE(mixed.size() == 1);

    mixed[0].deleted = true;
    mixed[0].enabled = false;

    const auto colors = bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values;
    mgr.add_custom_filament(1, 2, 25, colors);
    mgr.add_custom_filament(1, 2, 75, colors);

    auto &old_mixed = mgr.mixed_filaments();
    REQUIRE(old_mixed.size() == 3);
    REQUIRE(old_mixed[1].enabled);
    REQUIRE(old_mixed[2].enabled);
    const uint64_t first_custom_id = old_mixed[1].stable_id;
    const uint64_t second_custom_id = old_mixed[2].stable_id;

    std::vector<std::string> rows = split_rows(mgr.serialize_custom_entries());
    REQUIRE(rows.size() == 3);
    std::swap(rows[1], rows[2]);

    auto *definitions = bundle.project_config.option<ConfigOptionString>("mixed_filament_definitions");
    REQUIRE(definitions != nullptr);
    definitions->value = join_rows(rows);

    bundle.filament_presets.push_back(bundle.filament_presets.back());
    bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values.push_back("#00FF00");
    bundle.update_multi_material_filament_presets(size_t(-1), 2);

    const std::vector<unsigned int> remap = bundle.consume_last_filament_id_remap();
    REQUIRE(remap.size() >= 5);

    const auto &rebuilt = bundle.mixed_filaments.mixed_filaments();
    const unsigned int new_first_custom_virtual_id = virtual_id_for_stable_id(rebuilt, 3, first_custom_id);
    const unsigned int new_second_custom_virtual_id = virtual_id_for_stable_id(rebuilt, 3, second_custom_id);

    REQUIRE(new_first_custom_virtual_id != 0);
    REQUIRE(new_second_custom_virtual_id != 0);
    CHECK(remap[3] == new_first_custom_virtual_id);
    CHECK(remap[4] == new_second_custom_virtual_id);
}

TEST_CASE("Mixed filament remap keeps later painted colors stable when an earlier mixed row is deleted", "[MixedFilament]")
{
    PresetBundle bundle;
    bundle.filament_presets = {"Default Filament", "Default Filament", "Default Filament", "Default Filament"};
    bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    bundle.update_multi_material_filament_presets();

    auto &mixed = bundle.mixed_filaments.mixed_filaments();
    // 4 physical filaments → C(4,2) = 6 auto-generated pairs.
    REQUIRE(mixed.size() >= 6);

    const uint64_t stable_id_6 = mixed[1].stable_id;
    const uint64_t stable_id_7 = mixed[2].stable_id;
    const uint64_t stable_id_8 = mixed[3].stable_id;

    const std::vector<MixedFilament> old_mixed = mixed;
    mixed[0].enabled = false;
    mixed[0].deleted = true;

    bundle.update_mixed_filament_id_remap(old_mixed, 4, 4);
    const std::vector<unsigned int> remap = bundle.consume_last_filament_id_remap();

    // remap size = old_total + 1 = (4 physical + 6 enabled virtual) + 1 = 11.
    REQUIRE(remap.size() >= 11);
    CHECK(remap[6] == virtual_id_for_stable_id(mixed, 4, stable_id_6));
    CHECK(remap[7] == virtual_id_for_stable_id(mixed, 4, stable_id_7));
    CHECK(remap[8] == virtual_id_for_stable_id(mixed, 4, stable_id_8));
}

TEST_CASE("Mixed filament grouped manual patterns normalize and round-trip", "[MixedFilament]")
{
    const std::vector<std::string> colors = {"#FF0000", "#0000FF"};

    MixedFilamentManager mgr;
    mgr.add_custom_filament(1, 2, 50, colors);
    REQUIRE(mgr.mixed_filaments().size() == 1);

    MixedFilament &row = mgr.mixed_filaments().front();
    row.manual_pattern = MixedFilamentManager::normalize_manual_pattern("1/1/1/1/1/1/1/2, 1/1/1/2/1/1/1/1");
    REQUIRE(row.manual_pattern == "11111112,11121111");

    const std::string serialized = mgr.serialize_custom_entries();

    MixedFilamentManager loaded;
    loaded.load_custom_entries(serialized, colors);
    REQUIRE(loaded.mixed_filaments().size() == 1);
    CHECK(loaded.mixed_filaments().front().manual_pattern == "11111112,11121111");
    CHECK(loaded.mixed_filaments().front().mix_b_percent == 13);
}

TEST_CASE("Mixed filament perimeter resolver uses grouped manual patterns by inset", "[MixedFilament]")
{
    const std::vector<std::string> colors = {"#00FFFF", "#FF00FF"};

    MixedFilamentManager mgr;
    mgr.add_custom_filament(1, 2, 50, colors);
    REQUIRE(mgr.mixed_filaments().size() == 1);

    MixedFilament &row = mgr.mixed_filaments().front();
    row.manual_pattern = MixedFilamentManager::normalize_manual_pattern("12,21");
    REQUIRE(row.manual_pattern == "12,21");

    const unsigned int mixed_filament_id = 3;
    CHECK(mgr.resolve(mixed_filament_id, 2, 0) == 1);
    CHECK(mgr.resolve(mixed_filament_id, 2, 1) == 2);

    CHECK(mgr.resolve_perimeter(mixed_filament_id, 2, 0, 0) == 1);
    CHECK(mgr.resolve_perimeter(mixed_filament_id, 2, 1, 0) == 2);
    CHECK(mgr.resolve_perimeter(mixed_filament_id, 2, 0, 1) == 2);
    CHECK(mgr.resolve_perimeter(mixed_filament_id, 2, 1, 1) == 1);
    CHECK(mgr.resolve_perimeter(mixed_filament_id, 2, 0, 3) == 2);
    CHECK(mgr.resolve_perimeter(mixed_filament_id, 2, 1, 3) == 1);

    const std::vector<unsigned int> ordered_layer0 = mgr.ordered_perimeter_extruders(mixed_filament_id, 2, 0);
    const std::vector<unsigned int> ordered_layer1 = mgr.ordered_perimeter_extruders(mixed_filament_id, 2, 1);
    REQUIRE(ordered_layer0.size() == 2);
    REQUIRE(ordered_layer1.size() == 2);
    CHECK(ordered_layer0[0] == 1);
    CHECK(ordered_layer0[1] == 2);
    CHECK(ordered_layer1[0] == 2);
    CHECK(ordered_layer1[1] == 1);
}

// ---------------------------------------------------------------------------
// ExtrusionPath inset index preservation
// ---------------------------------------------------------------------------

TEST_CASE("ExtrusionPath copies preserve inset index", "[MixedFilament]")
{
    ExtrusionPath src(erPerimeter);
    src.inset_idx = 3;

    ExtrusionPath copied(src);
    CHECK(copied.inset_idx == 3);

    ExtrusionPath assigned(erExternalPerimeter);
    assigned.inset_idx = 0;
    assigned = src;
    CHECK(assigned.inset_idx == 3);
}

// ---------------------------------------------------------------------------
// predict_mixed_color — Kubelka-Munk K/S blending API
// ---------------------------------------------------------------------------

// Parse "#RRGGBB" into {r,g,b} in [0,255].
static std::array<int, 3> parse_hex(const std::string &hex)
{
    int r = 0, g = 0, b = 0;
    if (hex.size() >= 7 && hex[0] == '#') {
        r = std::stoi(hex.substr(1, 2), nullptr, 16);
        g = std::stoi(hex.substr(3, 2), nullptr, 16);
        b = std::stoi(hex.substr(5, 2), nullptr, 16);
    }
    return {r, g, b};
}

TEST_CASE("predict_mixed_color returns valid #RRGGBB string", "[MixedFilament][KS]")
{
    const std::string result = predict_mixed_color({{"#FF0000", 1}, {"#0000FF", 1}});
    REQUIRE(result.size() == 7);
    REQUIRE(result[0] == '#');
    // Verify all characters are valid hex digits
    for (size_t i = 1; i < 7; ++i) {
        const char c = result[i];
        REQUIRE(((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')));
    }
}

TEST_CASE("predict_mixed_color single component returns that color", "[MixedFilament][KS]")
{
    const std::string result = predict_mixed_color({{"#FF0000", 100}});
    REQUIRE(result == "#FF0000");
}

TEST_CASE("predict_mixed_color 100 pct A returns A color", "[MixedFilament][KS]")
{
    const std::string result = predict_mixed_color({{"#FF0000", 100}, {"#0000FF", 0}});
    REQUIRE(result == "#FF0000");
}

TEST_CASE("predict_mixed_color 100 pct B returns B color", "[MixedFilament][KS]")
{
    const std::string result = predict_mixed_color({{"#FF0000", 0}, {"#0000FF", 100}});
    REQUIRE(result == "#0000FF");
}

TEST_CASE("predict_mixed_color is symmetric for equal weights", "[MixedFilament][KS]")
{
    const std::string ab = predict_mixed_color({{"#FF8000", 1}, {"#0080FF", 1}});
    const std::string ba = predict_mixed_color({{"#0080FF", 1}, {"#FF8000", 1}});
    REQUIRE(ab == ba);
}

TEST_CASE("predict_mixed_color same color blends to itself", "[MixedFilament][KS]")
{
    // Mixing a color with itself at any ratio must yield the same color.
    const std::string result = predict_mixed_color({{"#4488CC", 3}, {"#4488CC", 1}});
    REQUIRE(result == "#4488CC");
}

TEST_CASE("predict_mixed_color K/S is subtractive: red-dominant mix is redder than balanced", "[MixedFilament][KS]")
{
    // Use real-world-ish filament colors (not pure primaries) so K/S K ratios
    // stay finite and the subtractive effect shows clearly.
    // Red-ish: #E53935  Blue-ish: #1565C0
    const std::string balanced = predict_mixed_color({{"#E53935", 50}, {"#1565C0", 50}});
    const std::string red_dom  = predict_mixed_color({{"#E53935", 75}, {"#1565C0", 25}});

    const auto c_balanced = parse_hex(balanced);
    const auto c_red_dom  = parse_hex(red_dom);

    // Red-dominant blend must have higher R channel than balanced blend.
    CHECK(c_red_dom[0] >= c_balanced[0]);
    // Blue channel must be lower in the red-dominant blend.
    CHECK(c_red_dom[2] <= c_balanced[2]);
}

TEST_CASE("predict_mixed_color multi-component 3-way blend returns valid color", "[MixedFilament][KS]")
{
    // Three-component gradient blend.
    const std::string result = predict_mixed_color({
        {"#FF0000", 50},
        {"#00FF00", 25},
        {"#0000FF", 25}
    });
    REQUIRE(result.size() == 7);
    REQUIRE(result[0] == '#');
}

TEST_CASE("predict_mixed_color weights need not sum to 100", "[MixedFilament][KS]")
{
    // 1:1 ratio via weight values other than 50/50
    const std::string r1 = predict_mixed_color({{"#AABBCC", 1},  {"#334455", 1}});
    const std::string r2 = predict_mixed_color({{"#AABBCC", 50}, {"#334455", 50}});
    REQUIRE(r1 == r2);
}

// ---------------------------------------------------------------------------
// TD-weighted K/S (Option A: filament_td1s)
// ---------------------------------------------------------------------------

TEST_CASE("predict_mixed_color TD-weighted path differs from RGB-only path", "[MixedFilament][KS][TD]")
{
    // Light gray + dark gray at 50/50 with td1s=2.0 at default layer_height=1.0mm.
    // Beer–Lambert: opacity = 1 - exp(-1.0 / 2.0) = 1 - exp(-0.5) ≈ 0.393
    // TD path scales K/S down → lighter result than the unscaled RGB-only path.
    // Pure saturated colours (e.g. #FF0000 + #0000FF) both map to near-black in K/S
    // space and aren't suitable for this test; moderate grays are used instead.
    const std::string td_result = predict_mixed_color({
        {"#AAAAAA", 50, 2.0f},
        {"#555555", 50, 2.0f}
    });
    const std::string rgb_result = predict_mixed_color({
        {"#AAAAAA", 50, 0.0f},
        {"#555555", 50, 0.0f}
    });
    // Both must be valid hex colors.
    REQUIRE(td_result.size() == 7);
    REQUIRE(td_result[0] == '#');
    REQUIRE(rgb_result.size() == 7);
    REQUIRE(rgb_result[0] == '#');
    // TD-scaled K/S produces a lighter blend than unscaled K/S — results must differ.
    CHECK(td_result != rgb_result);
}

TEST_CASE("predict_mixed_color TD-weighted path scales with layer_height", "[MixedFilament][KS][TD]")
{
    // Same pair of moderate-gray filaments, same td1s=1mm, but different layer heights.
    // Beer–Lambert: opacity = 1 - exp(-layer_height / td1s)
    //   At 0.2mm, td1s=1: opacity = 1 - exp(-0.2) ≈ 0.181  (thin / mostly transparent)
    //   At 1.0mm, td1s=1: opacity = 1 - exp(-1.0) ≈ 0.632  (thicker / more opaque)
    // Thin layers absorb less → lighter result; thick layers absorb more → darker result.
    const std::string thin_result = predict_mixed_color(
        {{"#AAAAAA", 50, 1.0f}, {"#555555", 50, 1.0f}},
        /*layer_height=*/0.2f);
    const std::string thick_result = predict_mixed_color(
        {{"#AAAAAA", 50, 1.0f}, {"#555555", 50, 1.0f}},
        /*layer_height=*/1.0f);

    REQUIRE(thin_result.size() == 7);
    REQUIRE(thin_result[0] == '#');
    REQUIRE(thick_result.size() == 7);
    REQUIRE(thick_result[0] == '#');
    // Different layer heights must produce different blended colors.
    CHECK(thin_result != thick_result);
}

TEST_CASE("predict_mixed_color TD layer_height clamps to 1mm when zero", "[MixedFilament][KS][TD]")
{
    // layer_height=0 should clamp to 1mm (same as default 1.0mm call).
    const std::string zero_h  = predict_mixed_color(
        {{"#AAAAAA", 50, 1.0f}, {"#555555", 50, 1.0f}},
        /*layer_height=*/0.f);
    const std::string one_mm  = predict_mixed_color(
        {{"#AAAAAA", 50, 1.0f}, {"#555555", 50, 1.0f}},
        /*layer_height=*/1.0f);

    REQUIRE(zero_h.size() == 7);
    REQUIRE(zero_h[0] == '#');
    CHECK(zero_h == one_mm);
}

TEST_CASE("predict_mixed_color falls back to RGB-only when one component lacks TD", "[MixedFilament][KS][TD]")
{
    // One td1s=0, one td1s=2 — must use RGB-only path (same as both td1s=0).
    const std::string mixed_td = predict_mixed_color({
        {"#E53935", 50, 2.0f},
        {"#1565C0", 50, 0.0f}  // no TD for this one
    });
    const std::string rgb_only = predict_mixed_color({
        {"#E53935", 50, 0.0f},
        {"#1565C0", 50, 0.0f}
    });
    REQUIRE(mixed_td == rgb_only);
}

// ---------------------------------------------------------------------------
// HueForge JSON parser (Option B)
// ---------------------------------------------------------------------------

// Write a minimal HueForge JSON fixture to a temp file and parse it.
static std::string write_hueforge_fixture()
{
    const std::string path = std::string(TEST_DATA_DIR) + "/hueforge_fixture.json";
    std::ofstream ofs(path);
    ofs << R"([
  {"brand": "Bambu Lab",  "name": "PLA Basic - Red",    "material": "PLA", "color": "#F01515", "td": 1.5},
  {"brand": "PolyMaker", "name": "PolyTerra Teal",      "material": "PLA", "color": "#009688", "td": 2.1},
  {"brand": "Overture",  "name": "PETG Transparent",    "material": "PETG","color": "#CCE5FF", "td": 0.3}
])";
    return path;
}

TEST_CASE("hueforge_parse_json extracts 3 entries from fixture", "[HueForge][KS]")
{
    const std::string path = write_hueforge_fixture();
    const auto entries = hueforge_parse_json(path);
    REQUIRE(entries.size() == 3);

    SECTION("First entry: Bambu Lab PLA Basic Red") {
        REQUIRE(entries[0].brand    == "Bambu Lab");
        REQUIRE(entries[0].name     == "PLA Basic - Red");
        REQUIRE(entries[0].material == "PLA");
        REQUIRE(entries[0].hex_color == "#F01515");
        REQUIRE_THAT(entries[0].td, Catch::Matchers::WithinAbs(1.5, 1e-4));
    }
    SECTION("Second entry: PolyMaker PolyTerra Teal") {
        REQUIRE(entries[1].brand    == "PolyMaker");
        REQUIRE_THAT(entries[1].td, Catch::Matchers::WithinAbs(2.1, 1e-4));
    }
    SECTION("Third entry: Overture PETG Transparent") {
        REQUIRE(entries[2].material == "PETG");
        REQUIRE_THAT(entries[2].td, Catch::Matchers::WithinAbs(0.3, 1e-4));
    }
}

TEST_CASE("hueforge_parse_json returns empty on missing file", "[HueForge][KS]")
{
    const auto entries = hueforge_parse_json("/nonexistent/path/that/does/not/exist.json");
    REQUIRE(entries.empty());
}

// ---------------------------------------------------------------------------
// Fuzzy matching
// ---------------------------------------------------------------------------

TEST_CASE("hueforge_fuzzy_match handles case and special chars", "[HueForge][KS]")
{
    using namespace Slic3r;
    SECTION("Exact match after normalize") {
        const std::string a = hueforge_normalize("Bambu PLA Basic - Red");
        const std::string b = hueforge_normalize("bambu pla basic red");
        CHECK(hueforge_fuzzy_match(a, b));
    }
    SECTION("Preset name contains entry key") {
        // Orca preset: "Bambu PLA Basic @BBL A1M" — the entry key is a substring.
        const std::string preset_norm = hueforge_normalize("Bambu PLA Basic Red @BBL A1M");
        const std::string entry_norm  = hueforge_normalize("bambu pla basic red");
        CHECK(hueforge_fuzzy_match(preset_norm, entry_norm));
    }
    SECTION("No match for completely different strings") {
        const std::string a = hueforge_normalize("Bambu PLA Basic Red");
        const std::string b = hueforge_normalize("Overture PETG Transparent");
        CHECK_FALSE(hueforge_fuzzy_match(a, b));
    }
    SECTION("Empty strings do not match") {
        CHECK_FALSE(hueforge_fuzzy_match("", "bambuplabasicred"));
        CHECK_FALSE(hueforge_fuzzy_match("bambuplabasicred", ""));
    }
}

// ---------------------------------------------------------------------------
// K/S Ratio Solver
// ---------------------------------------------------------------------------

TEST_CASE("solve_mix_ratio: target = pure A yields 0% B", "[MixedFilament][KS][Solver]")
{
    // Asking for component A's own color should give mix_b_percent ≈ 0.
    const FilamentColorDef a{"#CC3300", 1};
    const FilamentColorDef b{"#003366", 1};
    const int result = solve_mix_ratio(a.hex_color, a, b);
    CHECK(result <= 5);
}

TEST_CASE("solve_mix_ratio: target = pure B yields 100% B", "[MixedFilament][KS][Solver]")
{
    const FilamentColorDef a{"#CC3300", 1};
    const FilamentColorDef b{"#003366", 1};
    const int result = solve_mix_ratio(b.hex_color, a, b);
    CHECK(result >= 95);
}

TEST_CASE("solve_mix_ratio: target = 50/50 blend recovers near 50%", "[MixedFilament][KS][Solver]")
{
    // Blend A and B at exactly 50/50, then ask the solver to recover that ratio.
    const FilamentColorDef a{"#AAAAAA", 1};
    const FilamentColorDef b{"#555555", 1};
    const std::string blend_50 = predict_mixed_color({{"#AAAAAA", 50}, {"#555555", 50}});
    const int result = solve_mix_ratio(blend_50, a, b);
    // Allow ±10% tolerance (K/S round-trips through 8-bit quantization).
    CHECK(result >= 40);
    CHECK(result <= 60);
}

TEST_CASE("solve_mix_ratio_result: result struct is consistent", "[MixedFilament][KS][Solver]")
{
    const FilamentColorDef a{"#E53935", 1};
    const FilamentColorDef b{"#1565C0", 1};
    const MixRatioResult res = solve_mix_ratio_result("#8B2090", a, b);

    // predicted_color must be a valid hex string.
    REQUIRE(res.predicted_color.size() == 7);
    REQUIRE(res.predicted_color[0] == '#');

    // mix_b_percent must be in range.
    CHECK(res.mix_b_percent >= 0);
    CHECK(res.mix_b_percent <= 100);

    // delta_e_approx must be non-negative.
    CHECK(res.delta_e_approx >= 0.f);

    // The predicted_color at the returned ratio must equal predict_mixed_color directly.
    const std::string direct = predict_mixed_color({
        {"#E53935", 100 - res.mix_b_percent},
        {"#1565C0", res.mix_b_percent}
    });
    CHECK(res.predicted_color == direct);
}

// ---------------------------------------------------------------------------
// Coaxial filament (TD/translucency) scenario
//
// This models the 3D-printable coaxial filament use case:
//   - An opaque inner core (e.g. vivid red, td1s ≈ 0 = fully opaque)
//   - A translucent outer shell (e.g. clear/white, high td1s = 3 mm)
// The apparent color of the finished filament depends on the shell thickness,
// which maps directly to layer_height in Beer–Lambert:
//   opacity = 1 - exp(-layer_height / td1s)
//
// At thin shell (0.1 mm) → mostly transparent → inner color dominates.
// At thick shell (2.0 mm) → more opaque → shell color dominates.
// The ratio solver finds what ratio of inner:outer gives a target appearance.
// ---------------------------------------------------------------------------

TEST_CASE("Coaxial filament: thin shell — inner (opaque) color dominates", "[MixedFilament][KS][Coaxial]")
{
    // Inner: vivid red, fully opaque (td1s = 0 → RGB-only K/S path).
    // Outer: near-white translucent (td1s = 3.0 mm).
    // At 0.1 mm shell the outer is nearly invisible.
    const FilamentColorDef inner{"#CC2200", 1, 0.f};   // opaque red
    const FilamentColorDef outer{"#EEEEEE", 1, 3.0f};  // translucent white

    // Since inner has td1s=0 the RGB-only K/S path is used.
    // A 90% inner / 10% outer blend should look close to the inner color.
    const std::string result = predict_mixed_color(
        {{"#CC2200", 90, 0.f}, {"#EEEEEE", 10, 0.f}});

    const auto rgb = parse_hex(result);
    // Red channel must dominate (>= 150).
    CHECK(rgb[0] >= 150);
    // Blue channel must stay low (<= 80).
    CHECK(rgb[2] <= 80);
}

TEST_CASE("Coaxial filament: TD solver finds ratio for target hue", "[MixedFilament][KS][Coaxial]")
{
    // Both components have TD data so Beer–Lambert path is active.
    // Inner: opaque red-orange, td1s = 0.4 mm (moderately opaque).
    // Outer: translucent sky-blue, td1s = 2.5 mm.
    const FilamentColorDef inner{"#D84315", 1, 0.4f};
    const FilamentColorDef outer{"#4FC3F7", 1, 2.5f};

    // Target: something between orange and blue — a muted salmon/mauve.
    const std::string target = "#9C7070";
    const float layer_height = 0.2f;  // typical FDM layer

    const MixRatioResult res = solve_mix_ratio_result(target, inner, outer, layer_height);

    // Solver must return a valid percentage.
    CHECK(res.mix_b_percent >= 0);
    CHECK(res.mix_b_percent <= 100);

    // Predicted color must be a valid hex.
    REQUIRE(res.predicted_color.size() == 7);
    REQUIRE(res.predicted_color[0] == '#');

    // Perceptual error should be below 30 (on a 0-100 scale) —
    // the K/S gamut may not perfectly reproduce all target hues,
    // but the solver should find the closest achievable point.
    CHECK(res.delta_e_approx < 30.f);

    // As outer (blue) proportion increases the red channel should decrease.
    const std::string less_outer = predict_mixed_color(
        {{inner.hex_color, 80, inner.td1s}, {outer.hex_color, 20, outer.td1s}}, layer_height);
    const std::string more_outer = predict_mixed_color(
        {{inner.hex_color, 20, inner.td1s}, {outer.hex_color, 80, outer.td1s}}, layer_height);

    const auto rgb_less = parse_hex(less_outer);
    const auto rgb_more = parse_hex(more_outer);
    // More outer (blue) → lower red, higher blue.
    CHECK(rgb_less[0] >= rgb_more[0]);
    CHECK(rgb_less[2] <= rgb_more[2]);
}

TEST_CASE("Coaxial filament: layer height affects predicted color", "[MixedFilament][KS][Coaxial]")
{
    // Both have TD data — Beer–Lambert is active.
    // A thicker outer shell layer means more opacity → outer color dominates more.
    const FilamentColorDef inner{"#BF360C", 50, 0.5f};  // dark orange-red
    const FilamentColorDef outer{"#E3F2FD", 50, 2.0f};  // very light blue (translucent)

    const std::string thin  = predict_mixed_color(
        {{inner.hex_color, 50, inner.td1s}, {outer.hex_color, 50, outer.td1s}},
        /*layer_height=*/0.1f);
    const std::string thick = predict_mixed_color(
        {{inner.hex_color, 50, inner.td1s}, {outer.hex_color, 50, outer.td1s}},
        /*layer_height=*/1.0f);

    // Colors must differ between thin and thick shells.
    CHECK(thin != thick);

    // Thick shell → more outer (light blue) opacity → higher blue channel.
    const auto rgb_thin  = parse_hex(thin);
    const auto rgb_thick = parse_hex(thick);
    CHECK(rgb_thick[2] >= rgb_thin[2]);
}
