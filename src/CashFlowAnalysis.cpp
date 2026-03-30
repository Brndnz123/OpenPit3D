// CashFlowAnalysis.cpp
#include "CashFlowAnalysis.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

CashFlowAnalysis::CashFlowAnalysis() = default;

// ─────────────────────────────────────────────────────────────────────────────
//  Data input helpers
// ─────────────────────────────────────────────────────────────────────────────

void CashFlowAnalysis::addCapex(int year, double amount, const std::string& desc) {
    capexSchedule_.push_back({ year, amount, desc });
}

void CashFlowAnalysis::addAnnualCashFlow(double fcf) {
    // Store as pure FCF: revenue = max(fcf, 0), opex = max(-fcf, 0)
    revenues_.push_back(std::max(fcf, 0.0));
    opexes_  .push_back(std::max(-fcf, 0.0));
}

void CashFlowAnalysis::clear() {
    capexSchedule_.clear();
    revenues_.clear();
    opexes_.clear();
    table_.clear();
    npv_ = irr_ = paybackYears_ = 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  discountFactor
//
//  Mid-year convention: exponent is (t − 0.5) instead of t.
//  This reflects the assumption that cash flows within a year are evenly
//  distributed, making the effective receipt point the midpoint of the year.
//
//  For CAPEX in year 0 (spent before any production), we always use the
//  end-of-year convention regardless of the setting, because pre-production
//  capital is committed at t=0, not spread across the year.
// ─────────────────────────────────────────────────────────────────────────────

double CashFlowAnalysis::discountFactor(int year) const {
    if (year == 0) return 1.0;   // Year 0 capital is never discounted
    double exp = (convention_ == DiscountConvention::MID_YEAR)
                 ? (double)year - 0.5
                 : (double)year;
    return 1.0 / std::pow(1.0 + discountRate_, exp);
}

// ─────────────────────────────────────────────────────────────────────────────
//  buildTimeline — merges CAPEX schedule with annual operating rows
//
//  Rules:
//    • Year 0: pure CAPEX only (no operating cash flow)
//    • Year 1..N: operating cash flows (revenue − opex) ± any CAPEX tranche
//  Multiple CAPEX tranches in the same year are summed together.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<CashFlowAnalysis::YearRow> CashFlowAnalysis::buildTimeline() const {
    // Determine the total project life
    int maxCapexYear = 0;
    for (const auto& c : capexSchedule_)
        maxCapexYear = std::max(maxCapexYear, c.year);

    int operatingYears = (int)std::max(revenues_.size(), opexes_.size());
    int totalYears     = std::max(maxCapexYear, operatingYears - 1 + 1) + 1;

    std::vector<YearRow> rows(totalYears, {0, 0.0, 0.0, 0.0});
    for (int i = 0; i < totalYears; ++i) rows[i].year = i;

    // ── Assign CAPEX by year ──────────────────────────────────────────────────
    for (const auto& c : capexSchedule_) {
        if (c.year >= 0 && c.year < totalYears)
            rows[c.year].capex += c.amount;
    }

    // ── Assign operating flows (start from year 1) ───────────────────────────
    for (int i = 0; i < operatingYears; ++i) {
        int yr = i + 1;   // Year index: operating year 0 maps to calendar year 1
        if (yr >= totalYears) break;
        if (i < (int)revenues_.size()) rows[yr].rev  = revenues_[i];
        if (i < (int)opexes_.size())   rows[yr].opex = opexes_[i];
    }

    return rows;
}

// ─────────────────────────────────────────────────────────────────────────────
//  calcNPV  — parameterised by rate (used for both NPV and IRR search)
// ─────────────────────────────────────────────────────────────────────────────

double CashFlowAnalysis::calcNPV(const std::vector<double>& fcfs, double rate) const {
    double npv = 0.0;
    for (int t = 0; t < (int)fcfs.size(); ++t) {
        double exp_ = (t == 0) ? 0.0
                    : (convention_ == DiscountConvention::MID_YEAR)
                      ? (double)t - 0.5
                      : (double)t;
        double df = (t == 0) ? 1.0 : 1.0 / std::pow(1.0 + rate, exp_);
        npv += fcfs[t] * df;
    }
    return npv;
}

// ─────────────────────────────────────────────────────────────────────────────
//  calcIRR — Brent's method
//
//  IRR is the discount rate r at which NPV(r) = 0.
//  Brent's method (Illinois algorithm variant) is used because:
//    • It is guaranteed to converge if a bracket [lo, hi] with opposite signs
//      can be found.
//    • It combines bisection (robust) with secant/inverse-quadratic (fast).
//  We search for a bracket by doubling the upper bound from 1% to 1000%.
//  If no sign change is found (all-negative project), IRR is undefined → 0.
// ─────────────────────────────────────────────────────────────────────────────

double CashFlowAnalysis::calcIRR(const std::vector<double>& fcfs) const {
    // ── Bracket search ────────────────────────────────────────────────────────
    double lo = -0.999, hi = 0.01;
    double fLo = calcNPV(fcfs, lo);
    double fHi = calcNPV(fcfs, hi);

    // Expand hi until we find a sign change or give up
    int    tries = 0;
    while (fLo * fHi > 0.0 && tries < 60) {
        hi *= 2.0;
        fHi = calcNPV(fcfs, hi);
        ++tries;
    }
    if (fLo * fHi > 0.0) return 0.0;   // No IRR found (undiscounted project)

    // ── Brent's method ────────────────────────────────────────────────────────
    double a = lo, fa = fLo;
    double b = hi, fb = fHi;
    double c = a,  fc = fa;
    double d = b - a, e = d;

    static constexpr double TOL = 1e-10;
    static constexpr int    MAX_ITER = 200;

    for (int iter = 0; iter < MAX_ITER; ++iter) {
        if (fb * fc > 0.0) { c = a; fc = fa; d = e = b - a; }
        if (std::abs(fc) < std::abs(fb)) {
            a = b; fa = fb;
            b = c; fb = fc;
            c = a; fc = fa;
        }
        double tol1 = 2.0 * TOL * std::abs(b) + 0.5 * TOL;
        double xm   = 0.5 * (c - b);

        if (std::abs(xm) <= tol1 || std::abs(fb) < TOL) return b;

        if (std::abs(e) >= tol1 && std::abs(fa) > std::abs(fb)) {
            double s = fb / fa, p, q, r2;
            if (a == c) {
                p  = 2.0 * xm * s;
                q  = 1.0 - s;
            } else {
                double t2 = fa / fc;
                r2 = fb / fc;
                p  = s * (2.0 * xm * t2 * (t2 - r2) - (b - a) * (r2 - 1.0));
                q  = (t2 - 1.0) * (r2 - 1.0) * (s - 1.0);
            }
            if (p > 0.0) q = -q; else p = -p;
            if (2.0 * p < std::min(3.0 * xm * q - std::abs(tol1 * q),
                                   std::abs(e * q))) {
                e = d; d = p / q;
            } else {
                d = xm; e = d;
            }
        } else {
            d = xm; e = d;
        }
        a = b; fa = fb;
        b += (std::abs(d) > tol1) ? d : (xm > 0 ? tol1 : -tol1);
        fb = calcNPV(fcfs, b);
    }
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
//  generate  — main computation
// ─────────────────────────────────────────────────────────────────────────────

void CashFlowAnalysis::generate() {
    table_.clear();
    auto rows = buildTimeline();

    std::vector<double> fcfSeries;
    fcfSeries.reserve(rows.size());

    double cumulativeNPV = 0.0;
    bool   paybackFound  = false;
    double lastNegNPV    = 0.0;

    for (const auto& row : rows) {
        AnnualCashFlow acf;
        acf.year     = row.year;
        acf.revenue  = row.rev;
        acf.opex     = row.opex;
        acf.capex    = row.capex;

        // Free cash flow: revenue − operating costs − capital expenditures
        acf.freeCashFlow = row.rev - row.opex - row.capex;

        acf.discountFactor = discountFactor(row.year);
        acf.discountedFCF  = acf.freeCashFlow * acf.discountFactor;
        cumulativeNPV     += acf.discountedFCF;
        acf.cumulativeNPV  = cumulativeNPV;

        fcfSeries.push_back(acf.freeCashFlow);

        // ── Payback period (interpolated) ─────────────────────────────────────
        //  Payback = the fractional year when cumulative UNDISCOUNTED cash flow
        //  crosses zero.  Using undiscounted for payback is industry convention.
        //  (Note: some analysts use discounted payback — we report undiscounted here.)
        if (!paybackFound) {
            double runningUndiscounted = 0.0;
            for (const auto& f : fcfSeries) runningUndiscounted += f;
            if (runningUndiscounted >= 0.0 && paybackYears_ == 0.0) {
                // Interpolate: payback occurs somewhere within this year
                if (std::abs(acf.freeCashFlow) > 1e-9) {
                    double frac = -lastNegNPV / acf.freeCashFlow;
                    paybackYears_ = (double)row.year - 1.0 + frac;
                } else {
                    paybackYears_ = (double)row.year;
                }
                paybackFound = true;
            }
            lastNegNPV = runningUndiscounted;
        }

        table_.push_back(std::move(acf));
    }

    npv_ = cumulativeNPV;
    irr_ = calcIRR(fcfSeries);
    if (!paybackFound) paybackYears_ = std::numeric_limits<double>::infinity();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Aggregated totals
// ─────────────────────────────────────────────────────────────────────────────

double CashFlowAnalysis::totalCapex() const {
    double t = 0.0;
    for (const auto& c : capexSchedule_) t += c.amount;
    return t;
}

double CashFlowAnalysis::totalRevenue() const {
    return std::accumulate(revenues_.begin(), revenues_.end(), 0.0);
}

double CashFlowAnalysis::totalOpex() const {
    return std::accumulate(opexes_.begin(), opexes_.end(), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  sensitivityAnalysis
//
//  Rebuilds the financial model for each combination of price / cost variation
//  and records the resulting NPV and IRR.  This creates the data for a spider
//  or tornado chart in the UI.
//
//  Note: we rebuild from the stored fcf table, scaling revenues and opex
//  independently so both curves can be plotted on the same chart.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<SensitivityResult> CashFlowAnalysis::sensitivityAnalysis(
        double baseMetalPrice,
        double baseOpexPerTonne,
        double range,
        int    steps) const
{
    std::vector<SensitivityResult> results;
    results.reserve(steps * steps);

    for (int i = 0; i < steps; ++i) {
        double variation = -range + (2.0 * range * i) / (steps - 1);
        double priceMult = 1.0 + variation;
        double costMult  = 1.0 + variation;   // Independent variation

        // ── Price sensitivity (hold cost constant) ────────────────────────────
        {
            CashFlowAnalysis sim;
            sim.setDiscountRate(discountRate_);
            sim.setConvention(convention_);
            sim.setCapexSchedule(capexSchedule_);

            std::vector<double> scaledRevenues(revenues_.size());
            std::transform(revenues_.begin(), revenues_.end(),
                           scaledRevenues.begin(),
                           [priceMult](double r){ return r * priceMult; });
            sim.setAnnualRevenues(scaledRevenues);
            sim.setAnnualOpex(opexes_);
            sim.generate();

            SensitivityResult sr;
            sr.variation = variation;
            sr.price     = baseMetalPrice * priceMult;
            sr.cost      = baseOpexPerTonne;   // Unchanged
            sr.npv       = sim.npv();
            sr.irr       = sim.irr();
            results.push_back(sr);
        }
    }

    // ── Cost sensitivity (hold price constant) ────────────────────────────────
    for (int i = 0; i < steps; ++i) {
        double variation = -range + (2.0 * range * i) / (steps - 1);
        double costMult  = 1.0 + variation;

        CashFlowAnalysis sim;
        sim.setDiscountRate(discountRate_);
        sim.setConvention(convention_);
        sim.setCapexSchedule(capexSchedule_);
        sim.setAnnualRevenues(revenues_);

        std::vector<double> scaledOpex(opexes_.size());
        std::transform(opexes_.begin(), opexes_.end(), scaledOpex.begin(),
                       [costMult](double o){ return o * costMult; });
        sim.setAnnualOpex(scaledOpex);
        sim.generate();

        SensitivityResult sr;
        sr.variation = variation;
        sr.price     = baseMetalPrice;          // Unchanged
        sr.cost      = baseOpexPerTonne * costMult;
        sr.npv       = sim.npv();
        sr.irr       = sim.irr();
        results.push_back(sr);
    }

    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Export helpers
// ─────────────────────────────────────────────────────────────────────────────

void CashFlowAnalysis::exportCashFlowCSV(const std::string& filename) const {
    std::ofstream f(filename);
    if (!f) throw std::runtime_error("Cannot write: " + filename);

    f << std::fixed << std::setprecision(2);
    f << "Year,Revenue,Opex,Capex,FreeCashFlow,DiscountFactor,DiscountedFCF,CumulativeNPV\n";
    for (const auto& row : table_) {
        f << row.year        << ','
          << row.revenue     << ','
          << row.opex        << ','
          << row.capex       << ','
          << row.freeCashFlow<< ','
          << std::setprecision(6) << row.discountFactor << ','
          << std::setprecision(2) << row.discountedFCF  << ','
          << row.cumulativeNPV   << '\n';
    }

    f << "\nSummary\n";
    f << "NPV,"          << std::setprecision(2) << npv_          << '\n';
    f << "IRR,"          << std::setprecision(4) << irr_ * 100.0  << "%\n";
    f << "Payback Years,"<< std::setprecision(2) << paybackYears_ << '\n';
    f << "Total Capex,"  << totalCapex()  << '\n';
    f << "Total Revenue,"<< totalRevenue()<< '\n';
    f << "Total Opex,"   << totalOpex()   << '\n';
}

void CashFlowAnalysis::exportSensitivityCSV(
        const std::string& filename,
        double baseMetalPrice,
        double baseOpexPerTonne,
        double range,
        int    steps) const
{
    auto results = sensitivityAnalysis(baseMetalPrice, baseOpexPerTonne, range, steps);

    std::ofstream f(filename);
    if (!f) throw std::runtime_error("Cannot write: " + filename);

    f << std::fixed << std::setprecision(4);
    f << "Variation,Price,Cost,NPV,IRR\n";
    for (const auto& r : results) {
        f << r.variation << ',' << r.price << ',' << r.cost
          << ',' << std::setprecision(2) << r.npv
          << ',' << std::setprecision(4) << r.irr * 100.0 << "%\n";
    }
}
