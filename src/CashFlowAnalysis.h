// CashFlowAnalysis.h
#pragma once
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  PhaseCapex — describes a single scheduled capital expenditure tranche.
//
//  Mining projects rarely spend all capital upfront. Instead, CAPEX is phased
//  across pre-stripping, construction, equipment commissioning, and expansion
//  pushbacks.  Modelling this correctly has a large impact on NPV because early
//  capital is discounted less than late capital.
// ─────────────────────────────────────────────────────────────────────────────

struct PhaseCapex {
    int    year   = 0;       // Year in which this tranche is spent (year 0 = today)
    double amount = 0.0;     // Capital amount ($)
    std::string description; // e.g. "Pre-strip phase 1", "Mill construction"
};

// ─────────────────────────────────────────────────────────────────────────────
//  DiscountConvention
//
//  END_OF_YEAR  (standard):  CF_t / (1+r)^t
//    Cash flows accrue at year end.  Conservative — understates NPV slightly.
//
//  MID_YEAR (industry standard for mining):  CF_t / (1+r)^(t - 0.5)
//    Assumes cash flows are evenly distributed throughout the year.
//    Produces ~(1+r)^0.5 higher NPV than end-of-year — a material difference
//    at high discount rates over long project lives.
// ─────────────────────────────────────────────────────────────────────────────

enum class DiscountConvention { END_OF_YEAR, MID_YEAR };

// ─────────────────────────────────────────────────────────────────────────────
//  AnnualCashFlow — one year's financial breakdown
// ─────────────────────────────────────────────────────────────────────────────

struct AnnualCashFlow {
    int    year           = 0;
    double revenue        = 0.0;    // Gross revenue ($)
    double opex           = 0.0;    // Operating expenditure (positive value, cash outflow)
    double capex          = 0.0;    // Capital expenditure for this year (positive = outflow)
    double freeCashFlow   = 0.0;    // revenue - opex - capex
    double discountFactor = 1.0;    // 1 / (1+r)^t  (or mid-year equivalent)
    double discountedFCF  = 0.0;    // freeCashFlow * discountFactor
    double cumulativeNPV  = 0.0;    // Running NPV including all years up to and including this
};

// ─────────────────────────────────────────────────────────────────────────────
//  SensitivityResult — one point in a sensitivity table
// ─────────────────────────────────────────────────────────────────────────────

struct SensitivityResult {
    double variation;   // e.g. -0.20 = 20% downside
    double price;       // Adjusted metal price
    double cost;        // Adjusted operating cost
    double npv;         // Resulting NPV at this price/cost combination
    double irr;         // Resulting IRR
};

// ─────────────────────────────────────────────────────────────────────────────
//  CashFlowAnalysis
//
//  Build a financial model by:
//    1. setCapexSchedule() — set phased capital expenditures
//    2. addAnnualRevenue() / addAnnualOpex() — populate yearly operating flows
//       OR addAnnualCashFlow() for pre-computed FCF
//    3. generate() — compute NPV, IRR, payback, discounted cash flow table
//    4. Read results via npv(), irr(), paybackYears(), cashFlowTable(), etc.
// ─────────────────────────────────────────────────────────────────────────────

class CashFlowAnalysis {
public:
    CashFlowAnalysis();

    // ── Configuration ─────────────────────────────────────────────────────────

    void setDiscountRate   (double r)                        { discountRate_ = r; }
    void setConvention     (DiscountConvention c)            { convention_   = c; }
    void setCapexSchedule  (const std::vector<PhaseCapex>& s){ capexSchedule_ = s; }

    // Add a single CAPEX tranche (alternative to setCapexSchedule)
    void addCapex(int year, double amount, const std::string& desc = "");

    // Per-year operating flows.  Both vectors must be the same length and
    // indexed from year 1 (year 0 is reserved for initial CAPEX).
    void setAnnualRevenues(const std::vector<double>& rev)  { revenues_ = rev; }
    void setAnnualOpex    (const std::vector<double>& opex) { opexes_   = opex; }

    // Convenience: add a pre-computed free cash flow for a single year
    // (revenue and opex will be stored as 0 / positive FCF respectively)
    void addAnnualCashFlow(double fcf);

    // Clear all data
    void clear();

    // ── Computation ───────────────────────────────────────────────────────────

    // Build the discounted cash flow table and compute all metrics.
    // Must be called before reading any results.
    void generate();

    // ── Results ───────────────────────────────────────────────────────────────

    double npv()          const { return npv_;           }
    double irr()          const { return irr_;           }
    double paybackYears() const { return paybackYears_;  }
    double totalCapex()   const;
    double totalRevenue() const;
    double totalOpex()    const;

    const std::vector<AnnualCashFlow>& cashFlowTable() const { return table_; }

    // ── Export ────────────────────────────────────────────────────────────────

    // Detailed discounted cash flow table
    void exportCashFlowCSV  (const std::string& filename) const;

    // Spider / tornado sensitivity table
    // Varies metal price and operating cost independently in steps
    // from (1 - range) to (1 + range) relative to their base values.
    std::vector<SensitivityResult> sensitivityAnalysis(
        double baseMetalPrice,
        double baseOpexPerTonne,
        double range   = 0.30,
        int    steps   = 11) const;

    void exportSensitivityCSV(const std::string& filename,
                               double baseMetalPrice,
                               double baseOpexPerTonne,
                               double range = 0.30,
                               int    steps = 11) const;

private:
    // ── Inputs ────────────────────────────────────────────────────────────────
    double               discountRate_ = 0.08;
    DiscountConvention   convention_   = DiscountConvention::MID_YEAR;
    std::vector<PhaseCapex> capexSchedule_;
    std::vector<double>  revenues_;
    std::vector<double>  opexes_;

    // ── Computed results ──────────────────────────────────────────────────────
    double npv_          = 0.0;
    double irr_          = 0.0;
    double paybackYears_ = 0.0;
    std::vector<AnnualCashFlow> table_;

    // ── Helpers ───────────────────────────────────────────────────────────────

    // Discount factor for a given year under the chosen convention
    double discountFactor(int year) const;

    // NPV calculation on an arbitrary FCF series (used by IRR solver)
    double calcNPV(const std::vector<double>& fcfs, double rate) const;

    // IRR by Brent's method (robust bracket solver)
    double calcIRR(const std::vector<double>& fcfs) const;

    // Build a merged timeline: CAPEX schedule + operating cash flows
    // Returns a vector of {year, revenue, opex, capex} tuples
    struct YearRow { int year; double rev; double opex; double capex; };
    std::vector<YearRow> buildTimeline() const;
};
