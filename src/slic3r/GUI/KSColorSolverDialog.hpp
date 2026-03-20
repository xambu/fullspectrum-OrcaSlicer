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
//   Pick a target colour → K/S solver finds the optimal A:B ratio and shows
//   the predicted result colour plus an approximate ΔE error score.
//
// Mode B – Calibration Print
//   Reference the 3D-printable multi-colour filament calibration models
//   (coaxial or dual/tri/quad swatch).  A table of N ratio steps shows the
//   K/S predicted colour for each step.  The user picks the *actual* observed
//   colour after printing.  The dialog computes per-step ΔE and offers a
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

    // ---- Prediction mode callbacks ----------------------------------------
    void on_predict_target_changed(wxColourPickerEvent &evt);
    void on_predict_solve(wxCommandEvent &evt);
    void on_predict_apply(wxCommandEvent &evt);

    // ---- Calibration mode callbacks ---------------------------------------
    void on_calib_component_changed(wxCommandEvent &evt);
    void on_calib_update_predicted();
    void on_calib_actual_changed(size_t row_idx, size_t angle_idx, wxColourPickerEvent &evt);
    void on_n_angles_changed(wxSpinEvent &evt);
    void rebuild_calib_rows();   // rebuild row widgets when n_angles changes
    void on_calib_fit_td(wxCommandEvent &evt);
    void on_calib_export(wxCommandEvent &evt);

    // ---- Mode switching ---------------------------------------------------
    void switch_mode(int mode);  // 0 = Prediction, 1 = Calibration

    // ---- DPIDialog --------------------------------------------------------
    void on_dpi_changed(const wxRect &suggested_rect) override;

    // ---- State ------------------------------------------------------------
    int m_mode = 0;  // 0 = Prediction, 1 = Calibration

    std::vector<std::string> m_filament_colors;
    std::vector<float>       m_filament_td1s;
    float                    m_layer_height = 0.2f;

    // Currently selected component indices (0-based into m_filament_colors)
    int m_comp_a_idx = 0;
    int m_comp_b_idx = 1;

    // ---- Prediction mode widgets ------------------------------------------
    wxPanel            *m_prediction_panel  = nullptr;
    wxColourPickerCtrl *m_target_picker     = nullptr;
    wxPanel            *m_pred_target_swatch = nullptr;
    wxPanel            *m_pred_result_swatch = nullptr;
    wxStaticText       *m_pred_ratio_label  = nullptr;
    wxStaticText       *m_pred_delta_label  = nullptr;
    wxButton           *m_pred_apply_btn    = nullptr;

    int m_solved_ratio = 50;  // last solved mix_b_percent

    // ---- Calibration mode widgets ----------------------------------------
    wxPanel            *m_calibration_panel  = nullptr;
    wxSpinCtrlDouble   *m_calib_lh_ctrl      = nullptr;
    wxSpinCtrl         *m_n_angles_ctrl      = nullptr;  // 3..5 angular measurements per row
    int                 m_n_angles           = 3;

    // One angular measurement slot in a calibration row.
    // Multiple angles compensate for the sensor/light-source being co-planar:
    // measuring at different viewing angles reduces directional colour bias.
    struct AngleMeas {
        wxTextCtrl         *angle_ctrl   = nullptr;  // angle in degrees (editable)
        wxColourPickerCtrl *color_picker = nullptr;  // observed colour at this angle
        wxStaticText       *delta_label  = nullptr;  // ΔE vs. K/S prediction
        float               angle_deg   = 0.f;
        std::string         hex_color;               // "" = not set
    };

    // One row per calibration ratio step.
    struct CalibRow {
        int                  ratio_b_percent;          // 0..100
        wxPanel             *predicted_swatch = nullptr;
        // Per-angle measurements (3..5).  Angles are evenly spaced 0°–90° by
        // default; the user can edit each angle_ctrl to reflect their setup.
        std::vector<AngleMeas> measurements;
        wxStaticText        *avg_delta_label  = nullptr;  // ΔE of angle-averaged colour
        std::string          avg_actual_hex;              // averaged/blended colour
    };
    std::vector<CalibRow> m_calib_rows;
    wxScrolledWindow     *m_calib_scroll = nullptr;   // scrollable area holding rows

    wxStaticText *m_calib_rms_label  = nullptr;
    wxStaticText *m_calib_a_td_label = nullptr;
    wxStaticText *m_calib_b_td_label = nullptr;

    float m_fitted_td_a = 0.f;
    float m_fitted_td_b = 0.f;

    // ---- Calibration helpers -----------------------------------------------
    // Blend the per-angle measured colours into a single representative colour
    // for K/S fitting.  Equal weighting across all set measurements.
    static std::string blend_angle_measurements(const CalibRow &row);

    // ---- Mode toggle buttons (capsule style) ------------------------------
    wxButton *m_btn_mode_predict = nullptr;
    wxButton *m_btn_mode_calib   = nullptr;

    // ---- Shared component selector (top of dialog) -----------------------
    wxChoice *m_comp_a_choice = nullptr;
    wxChoice *m_comp_b_choice = nullptr;

    FilamentColorDef component_a() const;
    FilamentColorDef component_b() const;
    void             rebuild_calib_predicted();
    void             update_calib_deltas();
    float            calib_rms_error() const;
};

}} // namespace Slic3r::GUI
