#include <catch2/catch_all.hpp>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/MixedFilament.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

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
