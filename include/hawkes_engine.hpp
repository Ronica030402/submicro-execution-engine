#pragma once

#include "common_types.hpp"
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>

namespace hft {

// hawkes process intensity model
// lambda_i(t) = mu_i + sum_j sum_k alpha_ij * phi(t - t_k)
// using power law kernel phi(t) = (beta + t)^(-gamma)
class HawkesEngine {
private:
    double mu_b, mu_s;      // baseline intensities
    double alpha_self;      // self excitation  
    double alpha_cross;     // cross excitation
    double beta, gamma;     // kernel params
    
    std::deque<TradingEvent> buy_hist, sell_hist;
    size_t max_hist;
    
    double lambda_b, lambda_s;  // current intensities
    uint64_t last_update;
    
    // recalc intensity - this gets called a lot so keep it fast
    void recalc() {
        auto now = get_time_ns();
        
        lambda_b = mu_b;
        lambda_s = mu_s;
        
        // self excitation from buy events
        for (auto& e : buy_hist) {
            double dt = (now - e.arrival_time) * 1e-9;  // ns to seconds
            if (dt > 0) {
                double k = std::pow(beta + dt, -gamma);
                lambda_b += alpha_self * k;
                lambda_s += alpha_cross * k;
            }
        }
        
        // self excitation from sell events  
        for (auto& e : sell_hist) {
            double dt = (now - e.arrival_time) * 1e-9;
            if (dt > 0) {
                double k = std::pow(beta + dt, -gamma);
                lambda_s += alpha_self * k;
                lambda_b += alpha_cross * k;
            }
        }
        
        last_update = now;
    }
    
public:
    HawkesEngine(double mu_buy = 10.0, double mu_sell = 10.0,
                 double a_self = 0.5, double a_cross = 0.2,
                 double b = 1e-3, double g = 1.5, size_t hist = 1000) 
        : mu_b(mu_buy), mu_s(mu_sell), alpha_self(a_self), alpha_cross(a_cross),
          beta(b), gamma(g), max_hist(hist), lambda_b(mu_buy), lambda_s(mu_sell),
          last_update(0) {
        
        if (gamma <= 1.0) gamma = 1.5;  // prevent divergence
        if (beta <= 0) beta = 1e-6;     // prevent div by 0
    }
    
    void update(const TradingEvent& event) {
        if (event.event_type == Side::BUY) {
            buy_hist.push_back(event);
            if (buy_hist.size() > max_hist) buy_hist.pop_front();
        } else {
            sell_hist.push_back(event);
            if (sell_hist.size() > max_hist) sell_hist.pop_front();
        }
        
        recalc();
    }
    
    double buy_intensity() const { return lambda_b; }
    double sell_intensity() const { return lambda_s; }
    
    // ========================================================================
    // Get current intensity for sell orders
    // ========================================================================
    double get_sell_intensity() const {
        return intensity_sell_;
    }
    
    // ========================================================================
    // Get intensity imbalance (directional signal)
    // Positive = more buy pressure, Negative = more sell pressure
    // ========================================================================
    double get_intensity_imbalance() const {
        const double total = intensity_buy_ + intensity_sell_;
        if (total < 1e-10) return 0.0;
        return (intensity_buy_ - intensity_sell_) / total;
    }
    
    // ========================================================================
    // Predict intensity at future time (for latency compensation)
    // ========================================================================
    double predict_buy_intensity(Duration forecast_horizon) const {
        Timestamp future_time = current_time_ + forecast_horizon;
        return compute_intensity(Side::BUY, future_time);
    }
    
    double predict_sell_intensity(Duration forecast_horizon) const {
        Timestamp future_time = current_time_ + forecast_horizon;
        return compute_intensity(Side::SELL, future_time);
    }
    
    // ========================================================================
    // Reset the engine (clear history)
    // ========================================================================
    void reset() {
        buy_events_.clear();
        sell_events_.clear();
        intensity_buy_ = mu_buy_;
        intensity_sell_ = mu_sell_;
        current_time_ = now();
    }
    
    // ========================================================================
    // Get event counts for diagnostics
    // ========================================================================
    size_t get_buy_event_count() const { return buy_events_.size(); }
    size_t get_sell_event_count() const { return sell_events_.size(); }
    
private:
    // ========================================================================
    // Power-Law Kernel: K(τ) = (β + τ)^(-γ)
    // ========================================================================
    double power_law_kernel(double tau_seconds) const {
        if (tau_seconds < 0.0) return 0.0;
        return std::pow(beta_ + tau_seconds, -gamma_);
    }
    
    // ========================================================================
    // Recalculate intensity based on current event history
    // λ_i(t) = μ_i + Σ_j Σ_{t_k < t} α_ij * K(t - t_k)
    // ========================================================================
    void recalculate_intensity() {
        intensity_buy_ = compute_intensity(Side::BUY, current_time_);
        intensity_sell_ = compute_intensity(Side::SELL, current_time_);
    }
    
    // ========================================================================
    // Compute intensity for a given side at specified time
    // ========================================================================
    double compute_intensity(Side side, Timestamp eval_time) const {
        double intensity = (side == Side::BUY) ? mu_buy_ : mu_sell_;
        
        const int64_t eval_nanos = to_nanos(eval_time);
        
        // Self-excitation from same-side events
        const auto& same_events = (side == Side::BUY) ? buy_events_ : sell_events_;
        for (const auto& evt : same_events) {
            const int64_t event_nanos = to_nanos(evt.arrival_time);
            if (event_nanos < eval_nanos) {
                const double tau_seconds = (eval_nanos - event_nanos) * 1e-9;
                intensity += alpha_self_ * power_law_kernel(tau_seconds);
            }
        }
        
        // Cross-excitation from opposite-side events
        const auto& cross_events = (side == Side::BUY) ? sell_events_ : buy_events_;
        for (const auto& evt : cross_events) {
            const int64_t event_nanos = to_nanos(evt.arrival_time);
            if (event_nanos < eval_nanos) {
                const double tau_seconds = (eval_nanos - event_nanos) * 1e-9;
                intensity += alpha_cross_ * power_law_kernel(tau_seconds);
            }
        }
        
        return std::max(intensity, 1e-10);  // Prevent negative/zero intensity
    }
    
    // ========================================================================
    // Member variables
    // ========================================================================
    double mu_buy_;          // Baseline intensity for buy events
    double mu_sell_;         // Baseline intensity for sell events
    double alpha_self_;      // Self-excitation coefficient
    double alpha_cross_;     // Cross-excitation coefficient
    double beta_;            // Power-law kernel offset
    double gamma_;           // Power-law decay exponent
    size_t max_history_;     // Maximum event history size
    
    Timestamp current_time_;
    double intensity_buy_;   // Current buy intensity
    double intensity_sell_;  // Current sell intensity
    
    // Event history (using deque for efficient pop_front)
    std::deque<TradingEvent> buy_events_;
    std::deque<TradingEvent> sell_events_;
};

} // namespace hft
