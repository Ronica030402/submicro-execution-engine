#include "hawkes_engine.hpp"
#include <iostream>
#include <cassert>
#include <vector>

using namespace hft;

int main() {
    std::cout << "Testing VectorizedMultiKernelHawkes..." << std::endl;

    // Setup multi-kernel parameters
    std::array<double, 4> alphas_self = {0.5, 0.4, 0.3, 0.2};
    std::array<double, 4> alphas_cross = {0.1, 0.1, 0.05, 0.05};
    std::array<double, 4> betas = {100.0, 10.0, 1.0, 0.1}; // Different time scales

    VectorizedMultiKernelHawkes engine(
        10.0, 10.0, // mu_buy, mu_sell
        alphas_self, alphas_cross, betas
    );

    // Initial intensities should be mu
    double initial_buy = engine.get_buy_intensity();
    double initial_sell = engine.get_sell_intensity();
    
    std::cout << "Initial Buy Intensity: " << initial_buy << std::endl;
    assert(std::abs(initial_buy - 10.0) < 1e-9);

    // Simulate a burst of buy events
    Timestamp t0 = now();
    for (int i = 0; i < 5; ++i) {
        TradingEvent event;
        event.arrival_time = t0;
        event.event_type = Side::BUY;
        engine.update(event);
    }

    double burst_buy = engine.get_buy_intensity();
    std::cout << "Burst Buy Intensity: " << burst_buy << std::endl;
    assert(burst_buy > initial_buy);

    // Simulate decay
    std::cout << "Simulating 1 second decay..." << std::endl;
    TradingEvent decay_event;
    decay_event.arrival_time = t0 + std::chrono::seconds(1);
    decay_event.event_type = Side::BUY; // Just a dummy event to trigger update
    engine.update(decay_event);

    double decayed_buy = engine.get_buy_intensity();
    std::cout << "Decayed Buy Intensity: " << decayed_buy << std::endl;
    assert(decayed_buy < burst_buy);

    std::cout << "VectorizedMultiKernelHawkes tests passed!" << std::endl;
    return 0;
}
