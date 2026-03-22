// KSColorSolverDialog.cpp
//
// Two-mode dialog for colour-mixing guidance via Kubelka-Munk K/S law.
// See KSColorSolverDialog.hpp for full design notes.

#include "KSColorSolverDialog.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/MixedFilament.hpp"

#include <wx/button.h>
#include <wx/colordlg.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valnum.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <sstream>

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static wxColour hex_to_wxcolour(const std::string &hex)
{
    if (hex.size() < 7) return *wxBLACK;
    unsigned r = 0, g = 0, b = 0;
    std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
    return wxColour(r, g, b);
}

static std::string wxcolour_to_hex(const wxColour &c)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.Red(), c.Green(), c.Blue());
    return buf;
}

// Compute approximate CIE ΔE (perceptual distance, 0-100) between two "#RRGGBB" strings.
// Uses the luminance-weighted Euclidean approximation.
static float approx_delta_e(const std::string &a, const std::string &b)
{
    if (a.empty() || b.empty()) return -1.f;
    unsigned ar = 0, ag = 0, ab_ = 0, br = 0, bg = 0, bb = 0;
    std::sscanf(a.c_str() + 1, "%02x%02x%02x", &ar, &ag, &ab_);
    std::sscanf(b.c_str() + 1, "%02x%02x%02x", &br, &bg, &bb);
    float dr = float(ar) - float(br);
    float dg = float(ag) - float(bg);
    float db = float(ab_) - float(bb);
    float dist = std::sqrt(0.299f * dr * dr + 0.587f * dg * dg + 0.114f * db * db);
    // Scale: max possible ≈ 255 * sqrt(1) ≈ 255 → map to 0-100.
    return std::min(dist / 255.f * 100.f, 100.f);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

KSColorSolverDialog::KSColorSolverDialog(wxWindow               *parent,
                                         const std::vector<std::string> &filament_colors,
                                         const std::vector<float>       &filament_td1s,
                                         float                           layer_height,
                                         int                             initial_ratio_b,
                                         int                             initial_comp_a,
                                         int                             initial_comp_b)
    : DPIDialog(parent, wxID_ANY, _L("K/S Colour Solver"),
                wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_filament_colors(filament_colors)
    , m_filament_td1s(filament_td1s)
    , m_layer_height(layer_height)
{
    // Apply component selection from caller (e.g. from a configured mixed filament row)
    const int n = int(filament_colors.size());
    if (initial_comp_a >= 0 && initial_comp_a < n) m_comp_a_idx = initial_comp_a;
    if (initial_comp_b >= 0 && initial_comp_b < n) m_comp_b_idx = initial_comp_b;

    SetBackgroundColour(*wxWHITE);
    build_ui();

    // Pre-select the blend arc swatch at the configured mix ratio
    if (initial_ratio_b >= 0 && initial_ratio_b <= 100) {
        // Round to nearest 10% step that the swatch strip covers
        int snapped = (initial_ratio_b / 10) * 10;
        on_blend_swatch_clicked(snapped);
    }

    Fit();
    CentreOnParent();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

wxPanel *KSColorSolverDialog::make_swatch(wxWindow *parent, const wxColour &col, wxSize sz)
{
    auto *p = new wxPanel(parent, wxID_ANY, wxDefaultPosition, FromDIP(sz));
    p->SetBackgroundColour(col);
    p->SetMinSize(FromDIP(sz));
    return p;
}

void KSColorSolverDialog::refresh_swatch(wxPanel *swatch, const wxColour &col)
{
    if (!swatch) return;
    swatch->SetBackgroundColour(col);
    swatch->Refresh();
}

void KSColorSolverDialog::build_ui()
{
    auto *root = new wxBoxSizer(wxVERTICAL);

    // ---- Shared component selector ----------------------------------------
    auto *top_sizer = new wxFlexGridSizer(2, 2, FromDIP(4), FromDIP(8));
    top_sizer->AddGrowableCol(1);

    top_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Component A:")),
                   0, wxALIGN_CENTER_VERTICAL);
    m_comp_a_choice = new wxChoice(this, wxID_ANY);
    top_sizer->Add(m_comp_a_choice, 1, wxEXPAND);

    top_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Component B:")),
                   0, wxALIGN_CENTER_VERTICAL);
    m_comp_b_choice = new wxChoice(this, wxID_ANY);
    top_sizer->Add(m_comp_b_choice, 1, wxEXPAND);

    for (size_t i = 0; i < m_filament_colors.size(); ++i) {
        wxString label = wxString::Format("Filament %zu  %s", i + 1,
                                          m_filament_colors[i]);
        m_comp_a_choice->Append(label);
        m_comp_b_choice->Append(label);
    }
    // Use m_comp_a_idx / m_comp_b_idx which may have been set by the constructor
    if (m_comp_a_idx < int(m_filament_colors.size())) m_comp_a_choice->SetSelection(m_comp_a_idx);
    if (m_comp_b_idx < int(m_filament_colors.size())) m_comp_b_choice->SetSelection(m_comp_b_idx);

    m_comp_a_choice->Bind(wxEVT_CHOICE, &KSColorSolverDialog::on_calib_component_changed, this);
    m_comp_b_choice->Bind(wxEVT_CHOICE, &KSColorSolverDialog::on_calib_component_changed, this);

    root->Add(top_sizer, 0, wxEXPAND | wxALL, FromDIP(8));
    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    // ---- Mode toggle buttons -----------------------------------------------
    auto *mode_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_btn_mode_predict = new wxButton(this, wxID_ANY, _L("Prediction"));
    m_btn_mode_calib   = new wxButton(this, wxID_ANY, _L("Calibration Print"));
    mode_sizer->AddStretchSpacer();
    mode_sizer->Add(m_btn_mode_predict, 0, wxRIGHT, FromDIP(4));
    mode_sizer->Add(m_btn_mode_calib,   0);
    mode_sizer->AddStretchSpacer();
    root->Add(mode_sizer, 0, wxEXPAND | wxALL, FromDIP(6));

    m_btn_mode_predict->Bind(wxEVT_BUTTON,
        [this](wxCommandEvent &) { switch_mode(0); });
    m_btn_mode_calib->Bind(wxEVT_BUTTON,
        [this](wxCommandEvent &) { switch_mode(1); });

    // ---- Mode panels -------------------------------------------------------
    m_prediction_panel  = build_prediction_panel(this);
    m_calibration_panel = build_calibration_panel(this);

    root->Add(m_prediction_panel,  1, wxEXPAND | wxALL, FromDIP(8));
    root->Add(m_calibration_panel, 1, wxEXPAND | wxALL, FromDIP(8));

    // ---- Close button ------------------------------------------------------
    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
    auto *close_btn = new wxButton(this, wxID_CLOSE, _L("Close"));
    root->Add(close_btn, 0, wxALIGN_RIGHT | wxALL, FromDIP(8));
    close_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CLOSE); });

    SetSizer(root);
    switch_mode(0);
}

// ---------------------------------------------------------------------------
// Prediction panel
// ---------------------------------------------------------------------------

wxPanel *KSColorSolverDialog::build_prediction_panel(wxWindow *parent)
{
    auto *panel = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(*wxWHITE);
    auto *vs = new wxBoxSizer(wxVERTICAL);

    // ---- Layer height (affects K/S Beer-Lambert path) ----------------------
    auto *lh_row = new wxBoxSizer(wxHORIZONTAL);
    lh_row->Add(new wxStaticText(panel, wxID_ANY, _L("Layer height (mm):")),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    auto *lh_spin = new wxSpinCtrlDouble(panel, wxID_ANY,
        wxString::Format("%.2f", m_layer_height),
        wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 0.05, 0.5, m_layer_height, 0.05);
    lh_spin->Bind(wxEVT_SPINCTRLDOUBLE, [this, lh_spin](wxSpinDoubleEvent &) {
        m_layer_height = float(lh_spin->GetValue());
        rebuild_blend_strip();
    });
    lh_row->Add(lh_spin, 0);
    vs->Add(lh_row, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    // ---- Blend arc ---------------------------------------------------------
    // Shows every achievable colour for the selected A:B pair.
    // Replaces the "pick a colour and hope" approach — the user sees exactly
    // what this pair can produce before they pick a target.
    vs->Add(new wxStaticText(panel, wxID_ANY,
                _L("Achievable blend arc  (click any swatch to select that ratio):")),
            0, wxBOTTOM, FromDIP(3));

    m_blend_strip_panel = new wxPanel(panel, wxID_ANY);
    m_blend_strip_panel->SetBackgroundColour(*wxWHITE);
    auto *strip_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_blend_swatches.resize(11, nullptr);
    m_blend_colors.resize(11);
    for (int i = 0; i <= 10; ++i) {
        auto *col_vs = new wxBoxSizer(wxVERTICAL);
        auto *sw = make_swatch(m_blend_strip_panel, *wxLIGHT_GREY, wxSize(36, 36));
        m_blend_swatches[i] = sw;

        // Percentage label below swatch
        auto *lbl = new wxStaticText(m_blend_strip_panel, wxID_ANY,
                        wxString::Format("%d%%", i * 10),
                        wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
        lbl->SetMinSize(wxSize(FromDIP(36), -1));

        col_vs->Add(sw, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(2));
        col_vs->Add(lbl, 0, wxALIGN_CENTER_HORIZONTAL);
        strip_sizer->Add(col_vs, 0, wxRIGHT, FromDIP(3));

        int ratio_b = i * 10;
        sw->Bind(wxEVT_LEFT_DOWN, [this, ratio_b](wxMouseEvent &) {
            on_blend_swatch_clicked(ratio_b);
        });
        // Tooltip
        sw->SetToolTip(wxString::Format("A:%d%%  B:%d%%", 100 - ratio_b, ratio_b));
    }
    m_blend_strip_panel->SetSizer(strip_sizer);
    vs->Add(m_blend_strip_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    // Selected ratio display
    auto *sel_row = new wxBoxSizer(wxHORIZONTAL);
    m_blend_ratio_label = new wxStaticText(panel, wxID_ANY, _L("Click a swatch above to select a ratio"));
    m_blend_result_swatch = make_swatch(panel, *wxWHITE, wxSize(36, 24));
    sel_row->Add(m_blend_result_swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    sel_row->Add(m_blend_ratio_label, 0, wxALIGN_CENTER_VERTICAL);
    vs->Add(sel_row, 0, wxBOTTOM, FromDIP(4));

    // Apply selection button
    auto *apply_row = new wxBoxSizer(wxHORIZONTAL);
    auto *apply_btn = new wxButton(panel, wxID_ANY, _L("Apply selected ratio to mix"));
    apply_btn->Bind(wxEVT_BUTTON, &KSColorSolverDialog::on_predict_apply, this);
    apply_row->Add(apply_btn, 0);
    vs->Add(apply_row, 0, wxBOTTOM, FromDIP(8));

    vs->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    // ---- Optional: match a target colour -----------------------------------
    vs->Add(new wxStaticText(panel, wxID_ANY,
                _L("Match a target colour  (finds the closest swatch above):")),
            0, wxBOTTOM, FromDIP(3));

    auto *target_row = new wxBoxSizer(wxHORIZONTAL);
    m_target_picker = new wxColourPickerCtrl(panel, wxID_ANY, *wxRED);
    m_pred_target_swatch = make_swatch(panel, *wxRED);
    target_row->Add(new wxStaticText(panel, wxID_ANY, _L("Target:")),
                    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    target_row->Add(m_target_picker, 0, wxRIGHT, FromDIP(4));
    target_row->Add(m_pred_target_swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    m_pred_delta_label = new wxStaticText(panel, wxID_ANY, _L("ΔE: —"));
    target_row->Add(m_pred_delta_label, 0, wxALIGN_CENTER_VERTICAL);
    vs->Add(target_row, 0, wxBOTTOM, FromDIP(4));

    m_target_picker->Bind(wxEVT_COLOURPICKER_CHANGED,
        &KSColorSolverDialog::on_predict_target_changed, this);

    auto *find_btn = new wxButton(panel, wxID_ANY, _L("Find closest ratio"));
    find_btn->Bind(wxEVT_BUTTON, &KSColorSolverDialog::on_predict_solve, this);
    vs->Add(find_btn, 0, wxBOTTOM, FromDIP(4));

    // Warning / opaque-pair note
    m_pred_warn_label = new wxStaticText(panel, wxID_ANY, wxEmptyString);
    m_pred_warn_label->SetForegroundColour(wxColour(180, 60, 0));
    vs->Add(m_pred_warn_label, 0, wxBOTTOM, FromDIP(4));

    // ---- Alternative pair suggestions (shown when target is outside gamut) -
    m_suggest_hdr_label = new wxStaticText(panel, wxID_ANY, wxEmptyString);
    m_suggest_hdr_label->SetForegroundColour(wxColour(0, 90, 160));
    vs->Add(m_suggest_hdr_label, 0, wxBOTTOM, FromDIP(2));

    m_suggest_scroll = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition,
                                            wxSize(-1, FromDIP(120)),
                                            wxVSCROLL | wxBORDER_SIMPLE);
    m_suggest_scroll->SetScrollRate(0, FromDIP(10));
    m_suggest_scroll->SetBackgroundColour(*wxWHITE);
    m_suggest_scroll->SetSizer(new wxBoxSizer(wxVERTICAL));
    m_suggest_scroll->Hide();
    vs->Add(m_suggest_scroll, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    panel->SetSizer(vs);
    rebuild_blend_strip();
    return panel;
}

// ---------------------------------------------------------------------------
// Blend arc helpers
// ---------------------------------------------------------------------------

void KSColorSolverDialog::rebuild_blend_strip()
{
    FilamentColorDef a = component_a();
    FilamentColorDef b = component_b();
    if (a.hex_color.empty() || b.hex_color.empty()) return;
    if (m_blend_swatches.empty()) return;

    for (int i = 0; i <= 10; ++i) {
        int ratio_b = i * 10;
        FilamentColorDef aw = a; aw.weight = 100 - ratio_b;
        FilamentColorDef bw = b; bw.weight = ratio_b;
        m_blend_colors[i] = predict_mixed_color({aw, bw}, m_layer_height);
        if (m_blend_swatches[i])
            refresh_swatch(m_blend_swatches[i], hex_to_wxcolour(m_blend_colors[i]));
    }

    // Re-highlight selected swatch if any
    if (m_selected_ratio >= 0)
        select_blend_swatch(m_selected_ratio);

    if (m_blend_strip_panel)
        m_blend_strip_panel->Refresh();
}

void KSColorSolverDialog::on_blend_swatch_clicked(int ratio_b_percent)
{
    m_solved_ratio = ratio_b_percent;
    select_blend_swatch(ratio_b_percent);
}

void KSColorSolverDialog::select_blend_swatch(int ratio_b_percent)
{
    m_selected_ratio = ratio_b_percent;
    int idx = ratio_b_percent / 10;

    // Highlight selected, un-highlight others
    for (int i = 0; i <= 10; ++i) {
        if (!m_blend_swatches[i]) continue;
        wxColour border = (i == idx) ? wxColour(0, 120, 215) : wxColour(180, 180, 180);
        m_blend_swatches[i]->SetWindowStyle(wxBORDER_SIMPLE);
        m_blend_swatches[i]->SetBackgroundColour(
            hex_to_wxcolour(m_blend_colors.size() > size_t(i) ? m_blend_colors[i] : "#808080"));
        m_blend_swatches[i]->SetForegroundColour(border);
        m_blend_swatches[i]->Refresh();
    }

    // Update result swatch and label
    if (!m_blend_colors.empty() && idx < int(m_blend_colors.size())) {
        refresh_swatch(m_blend_result_swatch, hex_to_wxcolour(m_blend_colors[idx]));
    }
    if (m_blend_ratio_label) {
        m_blend_ratio_label->SetLabel(
            wxString::Format("A: %d%%    B: %d%%    %s",
                100 - ratio_b_percent, ratio_b_percent,
                m_blend_colors.size() > size_t(idx) ? m_blend_colors[idx] : ""));
    }
}

// ---------------------------------------------------------------------------
// Pair suggestion helpers
// ---------------------------------------------------------------------------

std::vector<KSColorSolverDialog::PairSuggestion>
KSColorSolverDialog::compute_pair_suggestions(const std::string &target_hex) const
{
    std::vector<PairSuggestion> out;
    const int n = int(m_filament_colors.size());
    for (int ia = 0; ia < n; ++ia) {
        for (int ib = ia + 1; ib < n; ++ib) {
            if (ia == m_comp_a_idx && ib == m_comp_b_idx) continue; // current pair
            FilamentColorDef a, b;
            a.hex_color = m_filament_colors[ia];
            a.td1s = ia < int(m_filament_td1s.size()) ? m_filament_td1s[ia] : 0.f;
            a.weight = 1;
            b.hex_color = m_filament_colors[ib];
            b.td1s = ib < int(m_filament_td1s.size()) ? m_filament_td1s[ib] : 0.f;
            b.weight = 1;
            MixRatioResult res = solve_mix_ratio_result(target_hex, a, b, m_layer_height);
            PairSuggestion s;
            s.idx_a = ia; s.idx_b = ib;
            s.best_ratio_b   = res.mix_b_percent;
            s.best_delta_e   = res.delta_e_approx;
            s.predicted_hex  = res.predicted_color;
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const PairSuggestion &x, const PairSuggestion &y) {
                  return x.best_delta_e < y.best_delta_e; });
    return out;
}

void KSColorSolverDialog::show_pair_suggestions(
    const std::vector<PairSuggestion> &suggestions)
{
    if (!m_suggest_scroll || !m_suggest_hdr_label) return;

    m_suggest_scroll->DestroyChildren();
    auto *sv = new wxBoxSizer(wxVERTICAL);

    for (size_t i = 0; i < suggestions.size() && i < 8; ++i) {
        const auto &s = suggestions[i];
        auto *row = new wxBoxSizer(wxHORIZONTAL);

        // Colour swatch of best achievable colour
        auto *sw = make_swatch(m_suggest_scroll, hex_to_wxcolour(s.predicted_hex),
                               wxSize(24, 16));
        row->Add(sw, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

        wxString lbl = wxString::Format(
            "Filament %d + Filament %d  →  A:%d%% B:%d%%  ΔE=%.1f",
            s.idx_a + 1, s.idx_b + 1,
            100 - s.best_ratio_b, s.best_ratio_b,
            s.best_delta_e);
        auto *txt = new wxStaticText(m_suggest_scroll, wxID_ANY, lbl);
        if (i == 0) {
            wxFont f = txt->GetFont();
            f.SetWeight(wxFONTWEIGHT_BOLD);
            txt->SetFont(f);
        }
        row->Add(txt, 1, wxALIGN_CENTER_VERTICAL);

        // "Use this pair" button
        auto *use_btn = new wxButton(m_suggest_scroll, wxID_ANY, _L("Use"),
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        int ia = s.idx_a, ib = s.idx_b;
        use_btn->Bind(wxEVT_BUTTON, [this, ia, ib](wxCommandEvent &) {
            m_comp_a_idx = ia;
            m_comp_b_idx = ib;
            if (m_comp_a_choice) m_comp_a_choice->SetSelection(ia);
            if (m_comp_b_choice) m_comp_b_choice->SetSelection(ib);
            rebuild_blend_strip();
            m_suggest_scroll->Hide();
            m_suggest_hdr_label->SetLabel(wxEmptyString);
            Layout(); Fit();
        });
        row->Add(use_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

        sv->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(3));
    }

    m_suggest_scroll->SetSizer(sv);
    m_suggest_scroll->FitInside();
    m_suggest_scroll->Show();
    Layout();
    Fit();
}

// ---------------------------------------------------------------------------
// Calibration panel
// ---------------------------------------------------------------------------

wxPanel *KSColorSolverDialog::build_calibration_panel(wxWindow *parent)
{
    auto *panel = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(*wxWHITE);
    auto *vs = new wxBoxSizer(wxVERTICAL);

    // ---- Options row -------------------------------------------------------
    auto *opt_row = new wxBoxSizer(wxHORIZONTAL);

    // Layer height
    opt_row->Add(new wxStaticText(panel, wxID_ANY, _L("Layer height (mm):")),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_calib_lh_ctrl = new wxSpinCtrlDouble(panel, wxID_ANY,
        wxString::Format("%.2f", m_layer_height),
        wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 0.05, 0.5, m_layer_height, 0.05);
    m_calib_lh_ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent &) {
        m_layer_height = float(m_calib_lh_ctrl->GetValue());
        on_calib_update_predicted();
    });
    opt_row->Add(m_calib_lh_ctrl, 0, wxRIGHT, FromDIP(12));

    // Number of viewing angles
    opt_row->Add(new wxStaticText(panel, wxID_ANY, _L("Viewing angles:")),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_n_angles_ctrl = new wxSpinCtrl(panel, wxID_ANY,
        wxString::Format("%d", m_n_angles),
        wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 3, 5, m_n_angles);
    m_n_angles_ctrl->Bind(wxEVT_SPINCTRL, &KSColorSolverDialog::on_n_angles_changed, this);
    opt_row->Add(m_n_angles_ctrl, 0);

    vs->Add(opt_row, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    // TD1S note: calibrated for 1.75 mm (nominal 1.8 mm) filament only
    auto *td_note = new wxStaticText(panel, wxID_ANY,
        _L("Note: TD1S values are calibrated for 1.75 mm (nominal 1.8 mm) FDM filament.\n"
           "For 2.85 mm filament, TD1S values will differ and require separate calibration."));
    td_note->SetForegroundColour(wxColour(100, 100, 100));
    vs->Add(td_note, 0, wxBOTTOM, FromDIP(6));

    // Measurement guide note
    auto *angle_note = new wxStaticText(panel, wxID_ANY,
        _L("Multi-angle measurement guide: because the sensor and light source share\n"
           "one optical plane, colour readings vary with viewing angle. Measure the\n"
           "printed swatch at each listed angle and enter the observed colour.\n"
           "The K/S fit uses the angle-averaged colour; per-angle ΔE is shown for\n"
           "diagnostic purposes."));
    angle_note->SetForegroundColour(wxColour(80, 80, 120));
    vs->Add(angle_note, 0, wxBOTTOM, FromDIP(6));

    vs->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    // ---- Scrollable calibration rows ---------------------------------------
    m_calib_scroll = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition,
                                          wxSize(-1, FromDIP(300)),
                                          wxVSCROLL | wxBORDER_SIMPLE);
    m_calib_scroll->SetScrollRate(0, FromDIP(10));
    m_calib_scroll->SetBackgroundColour(*wxWHITE);
    vs->Add(m_calib_scroll, 1, wxEXPAND | wxBOTTOM, FromDIP(6));

    // Build initial rows
    rebuild_calib_rows();

    // ---- Results & actions -------------------------------------------------
    auto *result_grid = new wxFlexGridSizer(3, 2, FromDIP(4), FromDIP(8));
    result_grid->AddGrowableCol(1);

    result_grid->Add(new wxStaticText(panel, wxID_ANY, _L("RMS ΔE:")),
                     0, wxALIGN_CENTER_VERTICAL);
    m_calib_rms_label = new wxStaticText(panel, wxID_ANY, _L("—"));
    result_grid->Add(m_calib_rms_label, 0);

    result_grid->Add(new wxStaticText(panel, wxID_ANY, _L("Fitted TD1S (A):")),
                     0, wxALIGN_CENTER_VERTICAL);
    m_calib_a_td_label = new wxStaticText(panel, wxID_ANY, _L("—"));
    result_grid->Add(m_calib_a_td_label, 0);

    result_grid->Add(new wxStaticText(panel, wxID_ANY, _L("Fitted TD1S (B):")),
                     0, wxALIGN_CENTER_VERTICAL);
    m_calib_b_td_label = new wxStaticText(panel, wxID_ANY, _L("—"));
    result_grid->Add(m_calib_b_td_label, 0);

    vs->Add(result_grid, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    auto *btn_row = new wxBoxSizer(wxHORIZONTAL);
    auto *fit_btn = new wxButton(panel, wxID_ANY, _L("Fit TD1S values"));
    fit_btn->Bind(wxEVT_BUTTON, &KSColorSolverDialog::on_calib_fit_td, this);
    auto *export_btn = new wxButton(panel, wxID_ANY, _L("Export CSV…"));
    export_btn->Bind(wxEVT_BUTTON, &KSColorSolverDialog::on_calib_export, this);
    btn_row->Add(fit_btn, 0, wxRIGHT, FromDIP(8));
    btn_row->Add(export_btn, 0);
    vs->Add(btn_row, 0);

    panel->SetSizer(vs);
    return panel;
}

// ---------------------------------------------------------------------------
// Calibration row builder
// ---------------------------------------------------------------------------

void KSColorSolverDialog::rebuild_calib_rows()
{
    // Preserve existing averaged colours across rebuilds
    std::vector<std::string> old_avg;
    for (auto &row : m_calib_rows)
        old_avg.push_back(row.avg_actual_hex);

    m_calib_rows.clear();

    if (!m_calib_scroll) return;
    m_calib_scroll->DestroyChildren();

    // Default ratio steps: 0, 10, 20, … 100 → 11 rows
    constexpr int steps = 11;
    // Default angle spacing: evenly between 0° and 90°
    auto default_angle = [this](int idx) -> float {
        return (m_n_angles <= 1) ? 0.f
            : idx * 90.f / float(m_n_angles - 1);
    };

    auto *rows_sizer = new wxBoxSizer(wxVERTICAL);

    // Column header
    {
        auto *hdr = new wxBoxSizer(wxHORIZONTAL);
        hdr->Add(new wxStaticText(m_calib_scroll, wxID_ANY, _L("B%")),
                 0, wxRIGHT, FromDIP(4));
        hdr->Add(new wxStaticText(m_calib_scroll, wxID_ANY, _L("K/S pred.")),
                 0, wxRIGHT, FromDIP(8));
        for (int a = 0; a < m_n_angles; ++a) {
            hdr->Add(new wxStaticText(m_calib_scroll, wxID_ANY,
                         wxString::Format(_L("Angle %d (°) / colour / ΔE"), a + 1)),
                     0, wxRIGHT, FromDIP(16));
        }
        hdr->Add(new wxStaticText(m_calib_scroll, wxID_ANY, _L("Avg ΔE")), 0);
        rows_sizer->Add(hdr, 0, wxBOTTOM, FromDIP(2));
        rows_sizer->Add(new wxStaticLine(m_calib_scroll), 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    }

    for (int step = 0; step < steps; ++step) {
        int ratio_b = step * 10;

        CalibRow row;
        row.ratio_b_percent = ratio_b;

        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Ratio label
        row_sizer->Add(new wxStaticText(m_calib_scroll, wxID_ANY,
                            wxString::Format("%3d%%", ratio_b)),
                        0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

        // Predicted swatch
        row.predicted_swatch = make_swatch(m_calib_scroll, *wxLIGHT_GREY,
                                           wxSize(28, 20));
        row_sizer->Add(row.predicted_swatch, 0,
                       wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        // Per-angle measurement columns
        for (int a = 0; a < m_n_angles; ++a) {
            AngleMeas meas;
            meas.angle_deg = default_angle(a);

            auto *angle_col = new wxBoxSizer(wxVERTICAL);

            // Angle degree field
            meas.angle_ctrl = new wxTextCtrl(m_calib_scroll, wxID_ANY,
                wxString::Format("%.0f", meas.angle_deg),
                wxDefaultPosition, FromDIP(wxSize(40, -1)), wxTE_PROCESS_ENTER);
            angle_col->Add(meas.angle_ctrl, 0, wxBOTTOM, FromDIP(2));

            // Colour picker
            meas.color_picker = new wxColourPickerCtrl(m_calib_scroll, wxID_ANY,
                *wxWHITE);
            size_t row_idx   = size_t(step);
            size_t angle_idx = size_t(a);
            meas.color_picker->Bind(wxEVT_COLOURPICKER_CHANGED,
                [this, row_idx, angle_idx](wxColourPickerEvent &evt) {
                    on_calib_actual_changed(row_idx, angle_idx, evt);
                });
            angle_col->Add(meas.color_picker, 0, wxBOTTOM, FromDIP(2));

            // Per-angle ΔE label
            meas.delta_label = new wxStaticText(m_calib_scroll, wxID_ANY, _L("—"));
            angle_col->Add(meas.delta_label, 0);

            row_sizer->Add(angle_col, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(12));
            row.measurements.push_back(meas);
        }

        // Average ΔE label
        row.avg_delta_label = new wxStaticText(m_calib_scroll, wxID_ANY, _L("—"));
        row_sizer->Add(row.avg_delta_label, 0, wxALIGN_CENTER_VERTICAL);

        rows_sizer->Add(row_sizer, 0, wxBOTTOM, FromDIP(4));
        m_calib_rows.push_back(std::move(row));
    }

    // Restore old averaged colours where possible
    for (size_t i = 0; i < m_calib_rows.size() && i < old_avg.size(); ++i)
        m_calib_rows[i].avg_actual_hex = old_avg[i];

    m_calib_scroll->SetSizer(rows_sizer);
    m_calib_scroll->FitInside();
    m_calib_scroll->Layout();

    on_calib_update_predicted();
}

// ---------------------------------------------------------------------------
// Mode switching
// ---------------------------------------------------------------------------

void KSColorSolverDialog::switch_mode(int mode)
{
    m_mode = mode;
    m_prediction_panel->Show(mode == 0);
    m_calibration_panel->Show(mode == 1);
    Layout();
    Fit();

    wxFont f_predict = m_btn_mode_predict->GetFont();
    f_predict.SetWeight(mode == 0 ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
    m_btn_mode_predict->SetFont(f_predict);

    wxFont f_calib = m_btn_mode_calib->GetFont();
    f_calib.SetWeight(mode == 1 ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
    m_btn_mode_calib->SetFont(f_calib);
}

// ---------------------------------------------------------------------------
// Component helpers
// ---------------------------------------------------------------------------

FilamentColorDef KSColorSolverDialog::component_a() const
{
    FilamentColorDef def;
    int idx = m_comp_a_idx;
    if (idx >= 0 && idx < int(m_filament_colors.size())) {
        def.hex_color = m_filament_colors[idx];
        def.td1s = (idx < int(m_filament_td1s.size())) ? m_filament_td1s[idx] : 0.f;
    }
    def.weight = 1;
    return def;
}

FilamentColorDef KSColorSolverDialog::component_b() const
{
    FilamentColorDef def;
    int idx = m_comp_b_idx;
    if (idx >= 0 && idx < int(m_filament_colors.size())) {
        def.hex_color = m_filament_colors[idx];
        def.td1s = (idx < int(m_filament_td1s.size())) ? m_filament_td1s[idx] : 0.f;
    }
    def.weight = 1;
    return def;
}

// ---------------------------------------------------------------------------
// Prediction mode callbacks
// ---------------------------------------------------------------------------

void KSColorSolverDialog::on_predict_target_changed(wxColourPickerEvent &evt)
{
    refresh_swatch(m_pred_target_swatch, evt.GetColour());
    // Auto-find the closest swatch whenever the picker changes
    wxCommandEvent dummy;
    on_predict_solve(dummy);
}

void KSColorSolverDialog::on_predict_solve(wxCommandEvent &)
{
    if (!m_target_picker) return;
    std::string target = wxcolour_to_hex(m_target_picker->GetColour());
    FilamentColorDef a = component_a();
    FilamentColorDef b = component_b();
    if (a.hex_color.empty() || b.hex_color.empty()) return;

    MixRatioResult res = solve_mix_ratio_result(target, a, b, m_layer_height);
    m_solved_ratio = res.mix_b_percent;

    // Highlight the closest swatch on the blend arc
    // (round to nearest 10% step)
    int closest_step = int(std::round(res.mix_b_percent / 10.0)) * 10;
    select_blend_swatch(closest_step);

    // ΔE indicator
    if (m_pred_delta_label)
        m_pred_delta_label->SetLabel(wxString::Format("ΔE: %.1f", res.delta_e_approx));

    // Warnings
    wxString warn;
    if (res.opaque_pair)
        warn = _L("Opaque pair: K/S predicts dark blends. In Layer-Cycle mode the "
                  "actual print will be closer to an RGB average (much lighter).");

    if (m_pred_warn_label) {
        m_pred_warn_label->SetLabel(warn);
        m_pred_warn_label->Show(!warn.empty());
        if (!warn.empty()) m_pred_warn_label->Wrap(FromDIP(400));
    }

    // If target is outside gamut, suggest better filament pairs
    if (res.at_gamut_boundary && m_filament_colors.size() >= 3) {
        auto suggestions = compute_pair_suggestions(target);
        if (!suggestions.empty()) {
            if (m_suggest_hdr_label)
                m_suggest_hdr_label->SetLabel(
                    _L("This pair cannot reach that colour. Better filament combinations:"));
            show_pair_suggestions(suggestions);
        }
    } else {
        if (m_suggest_scroll)  m_suggest_scroll->Hide();
        if (m_suggest_hdr_label) m_suggest_hdr_label->SetLabel(wxEmptyString);
    }

    Layout();
    Fit();
}

void KSColorSolverDialog::on_predict_apply(wxCommandEvent &)
{
    if (m_selected_ratio < 0) {
        wxMessageBox(_L("Click a swatch on the blend arc to select a ratio first."),
                     _L("K/S Solver"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxMessageBox(
        wxString::Format(
            _L("Apply ratio  A=%d%%  B=%d%%  to the mixed-filament row for "
               "the selected components."),
            100 - m_selected_ratio, m_selected_ratio),
        _L("Apply ratio"), wxOK | wxICON_INFORMATION, this);
}

// ---------------------------------------------------------------------------
// Calibration mode callbacks
// ---------------------------------------------------------------------------

void KSColorSolverDialog::on_calib_component_changed(wxCommandEvent &)
{
    m_comp_a_idx = m_comp_a_choice->GetSelection();
    m_comp_b_idx = m_comp_b_choice->GetSelection();
    // Rebuild blend arc in prediction mode too
    rebuild_blend_strip();
    on_calib_update_predicted();
}

void KSColorSolverDialog::on_calib_update_predicted()
{
    FilamentColorDef a = component_a();
    FilamentColorDef b = component_b();
    if (a.hex_color.empty() || b.hex_color.empty()) return;

    for (auto &row : m_calib_rows) {
        FilamentColorDef a_w = a;
        FilamentColorDef b_w = b;
        a_w.weight = 100 - row.ratio_b_percent;
        b_w.weight = row.ratio_b_percent;

        std::string pred = predict_mixed_color({a_w, b_w}, m_layer_height);
        refresh_swatch(row.predicted_swatch, hex_to_wxcolour(pred));

        // Update per-angle and average ΔE
        if (!row.avg_actual_hex.empty()) {
            float de = approx_delta_e(pred, row.avg_actual_hex);
            row.avg_delta_label->SetLabel(wxString::Format("%.1f", de));
        }
    }

    if (m_calib_scroll) {
        m_calib_scroll->FitInside();
        m_calib_scroll->Refresh();
    }
}

// Blend per-angle observed colours into one representative hex for K/S fitting.
// Uses equal weighting across all measurements that have been set.
std::string KSColorSolverDialog::blend_angle_measurements(const CalibRow &row)
{
    int count = 0;
    float r_sum = 0.f, g_sum = 0.f, b_sum = 0.f;
    for (const auto &m : row.measurements) {
        if (m.hex_color.empty()) continue;
        unsigned ri = 0, gi = 0, bi = 0;
        std::sscanf(m.hex_color.c_str() + 1, "%02x%02x%02x", &ri, &gi, &bi);
        r_sum += float(ri);
        g_sum += float(gi);
        b_sum += float(bi);
        ++count;
    }
    if (count == 0) return {};
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
        int(std::round(r_sum / count)),
        int(std::round(g_sum / count)),
        int(std::round(b_sum / count)));
    return buf;
}

void KSColorSolverDialog::on_calib_actual_changed(size_t row_idx,
                                                   size_t angle_idx,
                                                   wxColourPickerEvent &evt)
{
    if (row_idx >= m_calib_rows.size()) return;
    CalibRow &row = m_calib_rows[row_idx];
    if (angle_idx >= row.measurements.size()) return;

    AngleMeas &meas = row.measurements[angle_idx];
    meas.hex_color = wxcolour_to_hex(evt.GetColour());

    // Update predicted colour for this row
    FilamentColorDef a = component_a();
    FilamentColorDef b = component_b();
    a.weight = 100 - row.ratio_b_percent;
    b.weight = row.ratio_b_percent;
    std::string pred = predict_mixed_color({a, b}, m_layer_height);

    // Per-angle ΔE
    float de = approx_delta_e(pred, meas.hex_color);
    meas.delta_label->SetLabel(wxString::Format("%.1f", de));

    // Average colour and ΔE
    row.avg_actual_hex = blend_angle_measurements(row);
    if (!row.avg_actual_hex.empty()) {
        float avg_de = approx_delta_e(pred, row.avg_actual_hex);
        row.avg_delta_label->SetLabel(wxString::Format("%.1f", avg_de));
    }

    update_calib_deltas();
}

void KSColorSolverDialog::on_n_angles_changed(wxSpinEvent &)
{
    m_n_angles = m_n_angles_ctrl->GetValue();
    rebuild_calib_rows();
    Layout();
}

void KSColorSolverDialog::rebuild_calib_predicted()
{
    on_calib_update_predicted();
}

void KSColorSolverDialog::update_calib_deltas()
{
    if (!m_calib_rms_label) return;
    float rms = calib_rms_error();
    if (rms >= 0.f)
        m_calib_rms_label->SetLabel(wxString::Format("%.2f", rms));
}

float KSColorSolverDialog::calib_rms_error() const
{
    FilamentColorDef a = component_a();
    FilamentColorDef b = component_b();

    int n = 0;
    float sum_sq = 0.f;
    for (const auto &row : m_calib_rows) {
        if (row.avg_actual_hex.empty()) continue;
        FilamentColorDef aw = a;
        FilamentColorDef bw = b;
        aw.weight = 100 - row.ratio_b_percent;
        bw.weight = row.ratio_b_percent;
        std::string pred = predict_mixed_color({aw, bw}, m_layer_height);
        float de = approx_delta_e(pred, row.avg_actual_hex);
        sum_sq += de * de;
        ++n;
    }
    return (n > 0) ? std::sqrt(sum_sq / float(n)) : -1.f;
}

// ---------------------------------------------------------------------------
// Fit TD1S optimiser (2-D grid search)
//
// We search (td_a, td_b) in [0.1, 5.0] at 0.1 mm steps, choose the pair
// that minimises RMS ΔE across all calibration rows that have measurements.
// This is intentionally brute-force so it is easy to understand/audit.
// TD1S is defined for 1.75 mm filament (nominal 1.8 mm); fitting for other
// diameters requires re-running on samples from that diameter.
// ---------------------------------------------------------------------------

void KSColorSolverDialog::on_calib_fit_td(wxCommandEvent &)
{
    // Gather rows with data
    struct DataPoint { int ratio_b; std::string actual_hex; };
    std::vector<DataPoint> pts;
    for (const auto &row : m_calib_rows) {
        if (!row.avg_actual_hex.empty())
            pts.push_back({row.ratio_b_percent, row.avg_actual_hex});
    }
    if (pts.size() < 3) {
        wxMessageBox(
            _L("Please enter observed colours for at least 3 ratio steps "
               "(using the angle-averaged values) before fitting."),
            _L("Fit TD1S"), wxOK | wxICON_WARNING, this);
        return;
    }

    std::string base_a = component_a().hex_color;
    std::string base_b = component_b().hex_color;
    if (base_a.empty() || base_b.empty()) return;

    float best_rms = 1e9f;
    float best_ta  = 0.f, best_tb = 0.f;

    // Grid: 0.1..5.0 in 0.1 steps → 50 × 50 = 2500 evaluations
    for (int ia = 1; ia <= 50; ++ia) {
        float ta = ia * 0.1f;
        for (int ib = 1; ib <= 50; ++ib) {
            float tb = ib * 0.1f;

            FilamentColorDef a_def, b_def;
            a_def.hex_color = base_a; a_def.td1s = ta; a_def.weight = 1;
            b_def.hex_color = base_b; b_def.td1s = tb; b_def.weight = 1;

            float sum_sq = 0.f;
            for (const auto &pt : pts) {
                FilamentColorDef aw = a_def;
                FilamentColorDef bw = b_def;
                aw.weight = 100 - pt.ratio_b;
                bw.weight = pt.ratio_b;
                std::string pred = predict_mixed_color({aw, bw}, m_layer_height);
                float de = approx_delta_e(pred, pt.actual_hex);
                sum_sq += de * de;
            }
            float rms = std::sqrt(sum_sq / float(pts.size()));
            if (rms < best_rms) {
                best_rms = rms;
                best_ta  = ta;
                best_tb  = tb;
            }
        }
    }

    m_fitted_td_a = best_ta;
    m_fitted_td_b = best_tb;

    m_calib_a_td_label->SetLabel(wxString::Format("%.2f mm", best_ta));
    m_calib_b_td_label->SetLabel(wxString::Format("%.2f mm", best_tb));
    m_calib_rms_label->SetLabel(wxString::Format("%.2f (after fit)", best_rms));

    wxMessageBox(
        wxString::Format(
            _L("Fitted TD1S values (1.75 mm filament):\n"
               "  Component A: %.2f mm\n"
               "  Component B: %.2f mm\n"
               "  RMS ΔE: %.2f\n\n"
               "You can enter these into the filament profile under\n"
               "\"Transmission Distance\" (td1s)."),
            best_ta, best_tb, best_rms),
        _L("Fit TD1S result"), wxOK | wxICON_INFORMATION, this);
}

// ---------------------------------------------------------------------------
// Export CSV
// ---------------------------------------------------------------------------

void KSColorSolverDialog::on_calib_export(wxCommandEvent &)
{
    wxFileDialog dlg(this, _L("Save calibration CSV"), "", "ks_calibration.csv",
                     "CSV files (*.csv)|*.csv", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;

    wxFile f(dlg.GetPath(), wxFile::write);
    if (!f.IsOpened()) {
        wxMessageBox(_L("Could not open file for writing."),
                     _L("Export"), wxOK | wxICON_ERROR, this);
        return;
    }

    // Header
    std::ostringstream ss;
    ss << "ratio_b_percent,predicted_hex,avg_actual_hex,avg_delta_e";
    for (int a = 0; a < m_n_angles; ++a)
        ss << ",angle_" << a << "_deg,angle_" << a << "_hex,angle_" << a << "_delta_e";
    ss << "\n";

    FilamentColorDef ca = component_a();
    FilamentColorDef cb = component_b();

    for (const auto &row : m_calib_rows) {
        FilamentColorDef aw = ca; aw.weight = 100 - row.ratio_b_percent;
        FilamentColorDef bw = cb; bw.weight = row.ratio_b_percent;
        std::string pred = predict_mixed_color({aw, bw}, m_layer_height);

        float avg_de = row.avg_actual_hex.empty() ? -1.f
                       : approx_delta_e(pred, row.avg_actual_hex);

        ss << row.ratio_b_percent << ","
           << pred << ","
           << (row.avg_actual_hex.empty() ? "" : row.avg_actual_hex) << ","
           << (avg_de < 0.f ? "" : std::to_string(avg_de));

        for (const auto &meas : row.measurements) {
            float de = meas.hex_color.empty() ? -1.f
                       : approx_delta_e(pred, meas.hex_color);
            ss << "," << meas.angle_deg
               << "," << meas.hex_color
               << "," << (de < 0.f ? "" : std::to_string(de));
        }
        ss << "\n";
    }

    f.Write(ss.str().c_str(), ss.str().size());
    f.Close();

    wxMessageBox(_L("Calibration data exported successfully."),
                 _L("Export"), wxOK | wxICON_INFORMATION, this);
}

// ---------------------------------------------------------------------------
// DPI
// ---------------------------------------------------------------------------

void KSColorSolverDialog::on_dpi_changed(const wxRect &)
{
    Layout();
    Fit();
}

}} // namespace Slic3r::GUI
