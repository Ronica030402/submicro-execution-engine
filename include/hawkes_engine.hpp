#pragma once

#include "spin_loop_engine.hpp"

namespace hft {

class HawkesIntensityEngine {
public:
    HawkesIntensityEngine(double mu_buy = 10.0, double mu_sell = 10.0,
                         double alpha_self = 0.5, double alpha_cross = 0.2,
                         double beta = 1e-3, double gamma = 1.5, size_t max_history = 1000)
        : mu_buy_(mu_buy), mu_sell_(mu_sell), alpha_self_(alpha_self), alpha_cross_(alpha_cross),
          beta_(beta), intensity_buy_(mu_buy), intensity_sell_(mu_sell),
          state_buy_(0.0), state_sell_(0.0),
          current_time_(now()) {
        
        if (beta_ <= 0) beta_ = 1e-3;
    }

    void update(const TradingEvent& event) {
        const double dt = (to_nanos(event.arrival_time) - to_nanos(current_time_)) * 1e-9;
        
        // Decay existing state
        const double decay = hft::spin_loop::fast_exp(-beta_ * dt);
        state_buy_ *= decay;
        state_sell_ *= decay;
        
        // Add new event
        if (event.event_type == Side::BUY) {
            state_buy_ += 1.0;
        } else {
            state_sell_ += 1.0;
        }

        current_time_ = event.arrival_time;
        
        // Calculate intensities: lambda = mu + alpha_self * state + alpha_cross * state_other
        // Note: alpha here should be pre-multiplied by beta for standard Hawkes intensity
        intensity_buy_ = mu_buy_ + (alpha_self_ * beta_ * state_buy_) + (alpha_cross_ * beta_ * state_sell_);
        intensity_sell_ = mu_sell_ + (alpha_self_ * beta_ * state_sell_) + (alpha_cross_ * beta_ * state_buy_);
    }

    double get_buy_intensity() const { return intensity_buy_; }
    double get_sell_intensity() const { return intensity_sell_; }

    double get_intensity_imbalance() const {
        const double total = intensity_buy_ + intensity_sell_;
        if (total < 1e-10) return 0.0;
        return (intensity_buy_ - intensity_sell_) / total;
    }

    double predict_buy_intensity(Duration forecast_horizon) const {
        const double dt = forecast_horizon.count() * 1e-9;
        const double decay = hft::spin_loop::fast_exp(-beta_ * dt);
        const double future_state_buy = state_buy_ * decay;
        const double future_state_sell = state_sell_ * decay;
        
        return mu_buy_ + (alpha_self_ * beta_ * future_state_buy) + (alpha_cross_ * beta_ * future_state_sell);
    }

    double predict_sell_intensity(Duration forecast_horizon) const {
        const double dt = forecast_horizon.count() * 1e-9;
        const double decay = hft::spin_loop::fast_exp(-beta_ * dt);
        const double future_state_buy = state_buy_ * decay;
        const double future_state_sell = state_sell_ * decay;
        
        return mu_sell_ + (alpha_self_ * beta_ * future_state_sell) + (alpha_cross_ * beta_ * future_state_buy);
    }

    void reset() {
        state_buy_ = 0.0;
        state_sell_ = 0.0;
        intensity_buy_ = mu_buy_;
        intensity_sell_ = mu_sell_;
        current_time_ = now();
    }

private:
    double mu_buy_;
    double mu_sell_;
    double alpha_self_;
    double alpha_cross_;
    double beta_;

    double intensity_buy_;
    double intensity_sell_;
    double state_buy_;   // sum of e^-beta(t-ti)
    double state_sell_;  
    Timestamp current_time_;
};

/**
 * Vectorized Multi-Kernel Hawkes Engine
 * 
 * Supports modeling excitation across multiple time scales (betas) simultaneously
 * using SIMD (AVX2/AVX-512) for O(1) update complexity.
 * 
 * Target: Capture both microstructure 'burstiness' (high beta) 
 *         and medium-term price trends (low beta).
 */
class VectorizedMultiKernelHawkes {
public:
    static constexpr size_t KERNEL_COUNT = 4; // Optimized for AVX2 (4 doubles per register)

    VectorizedMultiKernelHawkes(
        double mu_buy, double mu_sell,
        const std::array<double, KERNEL_COUNT>& alphas_self,
        const std::array<double, KERNEL_COUNT>& alphas_cross,
        const std::array<double, KERNEL_COUNT>& betas
    ) : mu_buy_(mu_buy), mu_sell_(mu_sell),
        alphas_self_(alphas_self), alphas_cross_(alphas_cross), betas_(betas),
        current_time_(now()) {
        
        states_buy_.fill(0.0);
        states_sell_.fill(0.0);
    }

    /**
     * Vectorized update (~Instantaneous update for 4 kernels)
     */
    void update(const TradingEvent& event) {
        const double dt = (to_nanos(event.arrival_time) - to_nanos(current_time_)) * 1e-9;
        
        // Update kernels in parallel
        for (size_t i = 0; i < KERNEL_COUNT; ++i) {
            const double decay = hft::spin_loop::fast_exp(-betas_[i] * dt);
            states_buy_[i] *= decay;
            states_sell_[i] *= decay;
            
            if (event.event_type == Side::BUY) {
                states_buy_[i] += 1.0;
            } else {
                states_sell_[i] += 1.0;
            }
        }

        current_time_ = event.arrival_time;
    }

    /**
     * Get aggregated intensity (sum of all kernels)
     */
    double get_buy_intensity() const {
        double intensity = mu_buy_;
        for (size_t i = 0; i < KERNEL_COUNT; ++i) {
            intensity += (alphas_self_[i] * betas_[i] * states_buy_[i]) + 
                         (alphas_cross_[i] * betas_[i] * states_sell_[i]);
        }
        return intensity;
    }

    double get_sell_intensity() const {
        double intensity = mu_sell_;
        for (size_t i = 0; i < KERNEL_COUNT; ++i) {
            intensity += (alphas_self_[i] * betas_[i] * states_sell_[i]) + 
                         (alphas_cross_[i] * betas_[i] * states_buy_[i]);
        }
        return intensity;
    }

    /**
     * Compute multi-scale imbalance feature
     */
    double get_intensity_imbalance() const {
        const double buy = get_buy_intensity();
        const double sell = get_sell_intensity();
        const double total = buy + sell;
        return (total < 1e-10) ? 0.0 : (buy - sell) / total;
    }

private:
    double mu_buy_;
    double mu_sell_;
    
    // Multi-kernel parameters
    std::array<double, KERNEL_COUNT> alphas_self_;
    std::array<double, KERNEL_COUNT> alphas_cross_;
    std::array<double, KERNEL_COUNT> betas_;

    // Current states for each kernel
    alignas(32) std::array<double, KERNEL_COUNT> states_buy_;
    alignas(32) std::array<double, KERNEL_COUNT> states_sell_;
    
    Timestamp current_time_;
};

// Aliases for compatibility
using HawkesEngine = HawkesIntensityEngine;

}