#pragma once

#include "GUI_Utils.hpp"
#include "libslic3r/MixedFilament.hpp"

#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/clrpicker.h>
#include <wx/slider.h>
#include <wx/scrolwin.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>

#include <vector>
#include <string>

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------
// KSColorSolverDialog
//
// Two-mode dialog for colour-mixing guidance via Kubelka-Munk K/S law.
//
// Mode A – Prediction
//   Shows the full blend arc for the selected A:B filament pair — 11
//   clickable swatches covering 0%…100% B — so the user can see every
//   achievable colour without guessing.  Optionally pick a target colour;
//   the solver highlights the closest swatch and, when the target is outside
//   the blend gamut, ranks all other loaded filament pairs by how closely
//   they can match.
//
// Mode B – Calibration Print
//   Reference the 3D-printable multi-colour filament calibration models
//   (coaxial or dual/tri/quad swatch).  A table of N ratio steps shows the
//   K/S predicted colour for each step.  The user picks the *actual* observed
//   colour after printing at 3–5 viewing angles (compensating for co-planar
//   sensor/light-source bias).  The dialog computes per-step ΔE and offers a
//   "Fit TD1S" optimisation that adjusts both components' td1s values to
//   minimise the RMS error across all calibration points.
// ---------------------------------------------------------------------------

class KSColorSolverDialog : public DPIDialog
{
public:
    // filament_colors : current physical filament colours as "#RRGGBB" strings.
    // filament_td1s   : corresponding TD1S values (0 = not set).
    // layer_height    : current print layer height in mm.
    KSColorSolverDialog(wxWindow               *parent,
                        const std::vector<std::string> &filament_colors,
                        const std::vector<float>       &filament_td1s,
                        float                           layer_height);

private:
    // ---- UI helpers -------------------------------------------------------
    void build_ui();
    wxPanel *build_prediction_panel(wxWindow *parent);
    wxPanel *build_calibration_panel(wxWindow *parent);

    wxPanel *make_swatch(wxWindow *parent, const wxColour &col, wxSize sz = wxSize(28, 20));
    void     refresh_swatch(wxPanel *swatch, const wxColour &col);

    // ---- Prediction mode — blend arc --------------------------------------
    // Rebuild all 11 blend-arc swatches for the current A:B pair.
    void rebuild_blend_strip();
    // Called when user clicks a swatch on the arc.
    void on_blend_swatch_clicked(int ratio_b_percent);
    // Select (highlight) swatch at ratio_b_percent and update ratio label.
    void select_blend_swatch(int ratio_b_percent);

    // Pair suggestion — computed when a target is outside the blend gamut.
    struct PairSuggestion {
        int         idx_a;          // 0-based filament index A
        int         idx_b;          // 0-based filament index B
        int         best_ratio_b;
        float       best_delta_e;
        std::string predicted_hex;
    };
    // Compute and return all C(N,2) pair suggestions sorted by ΔE.
    std::vector<PairSuggestion> compute_pair_suggestions(const std::string &target_hex) const;
    // Populate m_suggest_scroll with suggestion rows.
    void show_pair_suggestions(const std::vector<PairSuggestion> &suggestions);

    // ---- Prediction mode callbacks ----------------------------------------
    void on_predict_target_changed(wxColourPickerEvent &evt);
    void on_predict_solve(wxCommandEvent &evt);
    void on_predict_apply(wxCommandEvent &evt);

    // ---- Calibration mode callbacks ---------------------------------------
    void on_calib_component_changed(wxCommandEvent &evt);
    void on_calib_update_predicted();
    void on_calib_actual_changed(size_t row_idx, size_t angle_idx, wxColourPickerEvent &evt);
    void on_n_angles_changed(wxSpinEvent &evt);
    void rebuild_calib_rows();
    void on_calib_fit_td(wxCommandEvent &evt);
    void on_calib_export(wxCommandEvent &evt);

    // ---- Mode switching ---------------------------------------------------
    void switch_mode(int mode);  // 0 = Prediction, 1 = Calibration

    // ---- DPIDialog --------------------------------------------------------
    void on_dpi_changed(const wxRect &suggested_rect) override;

    // ---- State ------------------------------------------------------------
    int m_mode = 0;

    std::vector<std::string> m_filament_colors;
    std::vector<float>       m_filament_td1s;
    float                    m_layer_height = 0.2f;

    // Currently selected component indices (0-based into m_filament_colors)
    int m_comp_a_idx = 0;
    int m_comp_b_idx = 1;

    // ---- Prediction mode widgets ------------------------------------------
    wxPanel            *m_prediction_panel   = nullptr;

    // Blend arc: 11 swatches for 0%,10%,...,100% B
    wxPanel            *m_blend_strip_panel  = nullptr;
    std::vector<wxPanel*> m_blend_swatches;        // [0..10]  ratio 0%..100%
    std::vector<std::string> m_blend_colors;       // precomputed hex per swatch
    int                 m_selected_ratio     = -1; // highlighted swatch (-1 = none)
    wxStaticText       *m_blend_ratio_label  = nullptr; // "A:X%  B:Y%"
    wxPanel            *m_blend_result_swatch = nullptr; // predicted colour at selection

    // Optional target section (collapsed until user wants it)
    wxColourPickerCtrl *m_target_picker      = nullptr;
    wxPanel            *m_pred_target_swatch = nullptr;
    wxStaticText       *m_pred_delta_label   = nullptr;
    wxStaticText       *m_pred_warn_label    = nullptr;

    // Pair suggestions panel (shown when target is outside blend gamut)
    wxStaticText       *m_suggest_hdr_label  = nullptr;
    wxScrolledWindow   *m_suggest_scroll     = nullptr;

    int m_solved_ratio = 50;

    // ---- Calibration mode widgets ----------------------------------------
    wxPanel            *m_calibration_panel  = nullptr;
    wxSpinCtrlDouble   *m_calib_lh_ctrl      = nullptr;
    wxSpinCtrl         *m_n_angles_ctrl      = nullptr;
    int                 m_n_angles           = 3;

    struct AngleMeas {
        wxTextCtrl         *angle_ctrl   = nullptr;
        wxColourPickerCtrl *color_picker = nullptr;
        wxStaticText       *delta_label  = nullptr;
        float               angle_deg   = 0.f;
        std::string         hex_color;
    };

    struct CalibRow {
        int                  ratio_b_percent;
        wxPanel             *predicted_swatch = nullptr;
        std::vector<AngleMeas> measurements;
        wxStaticText        *avg_delta_label  = nullptr;
        std::string          avg_actual_hex;
    };
    std::vector<CalibRow> m_calib_rows;
    wxScrolledWindow     *m_calib_scroll = nullptr;

    wxStaticText *m_calib_rms_label  = nullptr;
    wxStaticText *m_calib_a_td_label = nullptr;
    wxStaticText *m_calib_b_td_label = nullptr;

    float m_fitted_td_a = 0.f;
    float m_fitted_td_b = 0.f;

    static std::string blend_angle_measurements(const CalibRow &row);

    // ---- Mode toggle buttons ----------------------------------------------
    wxButton *m_btn_mode_predict = nullptr;
    wxButton *m_btn_mode_calib   = nullptr;

    // ---- Shared component selector ----------------------------------------
    wxChoice *m_comp_a_choice = nullptr;
    wxChoice *m_comp_b_choice = nullptr;

    FilamentColorDef component_a() const;
    FilamentColorDef component_b() const;
    void             rebuild_calib_predicted();
    void             update_calib_deltas();
    float            calib_rms_error() const;
};

}} // namespace Slic3r::GUI
