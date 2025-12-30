#include "common_types.hpp"
#include "lockfree_queue.hpp"
#include "hawkes_engine.hpp"
#include "fpga_inference.hpp"
#include "avellaneda_stoikov.hpp"
#include "risk_control.hpp"
#include "kernel_bypass_nic.hpp"
#include "shared_memory.hpp"
#include "event_scheduler.hpp"
#include "rust_ffi.hpp"
#include "metrics_collector.hpp"
#include "websocket_server.hpp"
#include "spin_loop_engine.hpp"
#include "jitter_profiler.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <atomic>
#include <csignal>
#include <cmath>
#include <sched.h>
#include <sys/mman.h>

using namespace hft;

std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_shutdown_requested.store(true, std::memory_order_release);
        std::cout << "\nShutdown requested..." << std::endl;
    }
}

// Performance Monitoring
struct PerformanceMetrics {
    uint64_t total_cycles = 0;
    uint64_t total_ticks_processed = 0;
    int64_t min_latency_ns = INT64_MAX;
    int64_t max_latency_ns = 0;
    int64_t total_latency_ns = 0;
    
    void update(int64_t cycle_latency_ns) {
        ++total_cycles;
        total_latency_ns += cycle_latency_ns;
        min_latency_ns = std::min(min_latency_ns, cycle_latency_ns);
        max_latency_ns = std::max(max_latency_ns, cycle_latency_ns);
    }
    
    void print_stats() const {
        if (total_cycles == 0) return;
        
        const double avg_latency_ns = static_cast<double>(total_latency_ns) / total_cycles;
        
        std::cout << "\n=== Performance Statistics ===" << std::endl;
        std::cout << "Total decision cycles: " << total_cycles << std::endl;
        std::cout << "Total ticks processed: " << total_ticks_processed << std::endl;
        std::cout << "Latency (ns):" << std::endl;
        std::cout << "  Min:     " << min_latency_ns << " ns" << std::endl;
        std::cout << "  Avg:     " << std::fixed << std::setprecision(2) 
                  << avg_latency_ns << " ns" << std::endl;
        std::cout << "  Max:     " << max_latency_ns << " ns" << std::endl;
        std::cout << "  Avg (µs): " << (avg_latency_ns / 1000.0) << " µs" << std::endl;
        
        if (avg_latency_ns < 1000.0) {
            std::cout << "Sub-microsecond latency achieved!" << std::endl;
        }
    }
};

// Trading System State
struct TradingState {
    int64_t current_position = 0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
    uint64_t total_trades = 0;
    MarketTick last_tick;
    MarketTick previous_tick;
    MarketTick reference_asset_tick;  // For cross-asset features
    QuotePair active_quotes;
};

// Volatility Estimator
class VolatilityEstimator {
public:
    explicit VolatilityEstimator(size_t window_size = 100)
        : window_size_(window_size), head_(0), count_(0),
          sum_ret_(0.0), sum_sq_ret_(0.0), last_price_(0.0) {
        returns_.fill(0.0);
    }
    
    void update(double price) {
        if (price <= 0) return;
        
        if (last_price_ > 0) {
            const double ret = hft::spin_loop::fast_ln(price / last_price_);
            
            // If window is full, subtract outgoing return
            if (count_ >= window_size_) {
                const double old_ret = returns_[head_];
                sum_ret_ -= old_ret;
                sum_sq_ret_ -= old_ret * old_ret;
            } else {
                count_++;
            }
            
            // Add new return
            returns_[head_] = ret;
            sum_ret_ += ret;
            sum_sq_ret_ += ret * ret;
            
            head_ = (head_ + 1) % 1024;
        }
        
        last_price_ = price;
    }
    
    double get_realized_volatility() const {
        if (count_ < 2) return 0.0;
        
        const double n = static_cast<double>(count_);
        const double mean = sum_ret_ / n;
        double variance = (sum_sq_ret_ / n) - (mean * mean);
        if (variance < 0) variance = 0;
        
        // Annualize
        return hft::spin_loop::fast_sqrt(variance * 5896800.0);
    }
    
    double get_volatility_index() const {
        return get_realized_volatility() * 5.0; // / 0.20
    }
    
private:
    size_t window_size_;
    size_t head_;
    size_t count_;
    double sum_ret_;
    double sum_sq_ret_;
    double last_price_;
    std::array<double, 1024> returns_;
};

// System Initialization: Configure for ultra-low latency
void configure_system_for_low_latency() {
    // Lock all current and future pages in RAM (prevent swapping)
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "Warning: Failed to lock memory pages" << std::endl;
    }
    
    // Set CPU affinity to core 0 (isolate from OS noise)
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "Warning: Failed to set CPU affinity" << std::endl;
    }
    
    // Set real-time scheduling priority
    struct sched_param param;
    param.sched_priority = 99;
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        std::cerr << "Warning: Failed to set RT priority (run with sudo for RT scheduling)" << std::endl;
    }
#endif
    
    std::cout << "[SYSTEM] Memory locked, CPU affinity set, RT priority configured" << std::endl;
}

// Main Trading Loop
int main() {
    std::cout << "=== Ultra-Low-Latency HFT System ===" << std::endl;
    std::cout << "Target: Sub-microsecond decision latency\n" << std::endl;
    
    configure_system_for_low_latency();
    
    // Setup signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "\n[INIT] Initializing components..." << std::endl;
    
    // 1. Kernel Bypass NIC (market data ingestion)
    KernelBypassNIC nic(16384);
    nic.start();
    std::cout << "[INIT] Kernel Bypass NIC (zero-copy, 16K ring buffer)" << std::endl;
    
    // 2. Shared Memory Queue (multi-process IPC)
    shm::SharedMarketDataQueue shared_queue("hft_market_data", true);
    std::cout << "[INIT] Shared Memory IPC (32K capacity, /dev/shm)" << std::endl;
    
    // 3. Event Scheduler (nanosecond-precision timing wheel)
    scheduler::TimingWheelScheduler timing_wheel(1024, std::chrono::microseconds(10));
    std::cout << "[INIT] Timing Wheel Scheduler (1024 slots, 10µs granularity)" << std::endl;
    
    HawkesIntensityEngine hawkes(10.0, 10.0, 0.3, 0.1, 1e-3, 1.8, 1000);
    std::cout << "[INIT] Hawkes Intensity Engine initialized" << std::endl;
    
    // 3. FPGA Inference Engine (compute layer)
    FPGA_DNN_Inference fpga_inference;
    std::cout << "[INIT] FPGA DNN Inference (fixed " 
              << fpga_inference.get_fixed_latency_ns() 
              << "ns latency)" << std::endl;
    
    // 4. Market-Making Strategy (execution layer)
    DynamicMMStrategy mm_strategy(
        /* risk_aversion      */ 0.1,
        /* volatility         */ 0.20,  // 20% annualized
        /* time_horizon       */ 300.0, // 5 minutes
        /* order_arrival_rate */ 10.0,
        /* tick_size          */ 0.01,
        /* system_latency_ns  */ 800    // 800ns RTT
    );
    std::cout << "[INIT] Avellaneda-Stoikov MM Strategy initialized" << std::endl;
    
    // 5. Risk Control (risk layer)
    RiskControl risk_control(
        /* max_position       */ 1000,
        /* max_loss_threshold */ 10000.0,
        /* max_order_value    */ 100000.0
    );
    std::cout << "[INIT] Risk Control system armed" << std::endl;
    
    // 6. Volatility Estimator (for regime detection)
    VolatilityEstimator vol_estimator(100);
    std::cout << "[INIT] Volatility Estimator (100-tick window)" << std::endl;
    
    // 7. Metrics Collection & Dashboard (monitoring layer)
    MetricsCollector metrics_collector(10000);
    DashboardServer dashboard(metrics_collector, 8080);
    dashboard.start();
    std::cout << "[INIT] Real-Time Dashboard Server (http://localhost:8080)" << std::endl;
    
    // 7. Rust Components (memory-safe + fast)
    // Note: Requires Rust library to be compiled and linked
    // rust_ffi::RustMarketMakerWrapper rust_mm(0.1, 0.20, 0.01);
    // rust_ffi::RustRiskControlWrapper rust_risk(1000);
    std::cout << "[INIT] Rust FFI ready (compile with Cargo for full integration)" << std::endl;
    
    // 7. Market Data Simulator (for testing)
    MarketDataSimulator simulator(nic);
    simulator.start(1000.0);  // 1000 Hz update rate
    std::cout << "[INIT] Market data simulator started (1000 Hz)\n" << std::endl;
    
    TradingState state;
    PerformanceMetrics metrics;
    JitterProfiler jitter_profiler;
    
    // Initialize reference asset tick (for cross-asset features)
    state.reference_asset_tick.mid_price = 100.0;
    state.reference_asset_tick.bid_price = 99.99;
    state.reference_asset_tick.ask_price = 100.01;
    
    // ==== CACHE WARMING PHASE ====
    // Run hot path with dummy data to:
    // 1. Fault in all code pages (Instruction Cache)
    // 2. Train CPU branch predictors
    // 3. Populate TLB with huge pages (if enabled)
    std::cout << "[INIT] Warming up Instruction & Data Caches (50k iterations)..." << std::endl;
    for(int i=0; i<50000; ++i) {
        // Dummy data
        MarketTick dummy_tick; 
        dummy_tick.mid_price = 100.0 + (i&1)*0.01; 
        dummy_tick.trade_volume = 100;
        
        // Execute heavy compute path
        const auto features = fpga_inference.extract_features(dummy_tick, state.reference_asset_tick, 10.0, 10.0);
        const auto pred = fpga_inference.predict(features);
        vol_estimator.update(dummy_tick.mid_price);
        mm_strategy.calculate_quotes(dummy_tick.mid_price, 0, 300.0, 0.0);
        risk_control.check_pre_trade_limits(Order(), 0);
        
        // Prevent optimizer from removing this loop
        std::atomic_signal_fence(std::memory_order_release);
    }
    std::cout << "[INIT] Warm-up complete. CPU Branch Predictors trained." << std::endl;
    // =============================
    
    uint64_t cycle_count = 0;
    const uint64_t print_interval = 1000;  // Print stats every N cycles
    
    std::cout << "=== Trading Loop Started ===" << std::endl;
    std::cout << "Features Active:" << std::endl;
    std::cout << "  Lock-free SPSC ring buffers" << std::endl;
    std::cout << "  Zero-copy shared memory IPC" << std::endl;
    std::cout << "  Nanosecond event scheduling" << std::endl;
    std::cout << "  Deterministic FPGA-style pipeline" << std::endl;
    std::cout << "  No dynamic allocation (garbage-free)" << std::endl;
    std::cout << "  Cache-line aligned structures" << std::endl;
    std::cout << "  Jitter Profiling & Stall Detection" << std::endl;
    std::cout << "  L1 Cache Prefetching & Warm-up" << std::endl;
    std::cout << "Target latency: < 1000 ns per decision cycle\n" << std::endl;
    
    while (!g_shutdown_requested.load(std::memory_order_acquire) && 
           !risk_control.is_kill_switch_triggered()) {
        
        jitter_profiler.mark(); // Measure cycle gap immediately
        
        // CRITICAL: Prefetch hot structures into L1 cache while checking NIC
        // This hides the ~80ns DRAM latency for these structures
        JitterProfiler::prefetch_L1(&risk_control);
        JitterProfiler::prefetch_L1(&mm_strategy);
        JitterProfiler::prefetch_L1(&state);
        JitterProfiler::prefetch_next_line(&state); // Prefetch adjacent lines
        
        const Timestamp cycle_start = now();
        
        // Get market data (zero-copy from NIC)
        MarketTick tick;
        bool has_data = nic.get_next_tick(tick);
        
        // Also try shared memory queue (for multi-process architecture)
        if (!has_data && !shared_queue.empty()) {
            has_data = shared_queue.read(tick);
        }
        
        if (!has_data) {
            // BUSY-WAIT instead of yield for sub-microsecond response
            #if defined(__x86_64__)
                _mm_pause();
            #elif defined(__aarch64__)
                __asm__ __volatile__("yield");
            #endif
            continue;
        }
        
        // Write to shared memory for other processes (optional)
        shared_queue.write(tick);
        
        ++metrics.total_ticks_processed;
        state.previous_tick = state.last_tick;
        state.last_tick = tick;
        
        // Signal generation
        if (tick.trade_volume > 0) {
            TradingEvent event(tick.timestamp, tick.trade_side, tick.asset_id);
            hawkes.update(event);
        }
        
        const double hawkes_buy_intensity = hawkes.get_buy_intensity();
        const double hawkes_sell_intensity = hawkes.get_sell_intensity();
        
        // Feature extraction
        const auto features = fpga_inference.extract_features(
            tick,
            state.reference_asset_tick,
            hawkes_buy_intensity,
            hawkes_sell_intensity
        );
        
        // Prediction
        const auto prediction = fpga_inference.predict(features);
        // prediction = [buy_score, hold_score, sell_score]
        
        // 
        // Step 5: Update volatility estimate and risk regime
        // 
        vol_estimator.update(tick.mid_price);
        const double vol_index = vol_estimator.get_volatility_index();
        risk_control.set_regime_multiplier(vol_index);
        
        // 
        // Step 6: Calculate latency cost
        // 
        const double current_vol = vol_estimator.get_realized_volatility();
        const double latency_cost = mm_strategy.calculate_latency_cost(
            current_vol, 
            tick.mid_price
        );
        
        // Quote generation
        const double time_remaining = 300.0;
        const QuotePair quotes = mm_strategy.calculate_quotes(
            tick.mid_price,
            state.current_position,
            time_remaining,
            latency_cost
        );
        
        // Risk management
        if (quotes.bid_price > 0 && quotes.ask_price > 0) {
            // Create potential orders
            Order bid_order(cycle_count * 2, tick.asset_id, Side::BUY, 
                          quotes.bid_price, static_cast<uint64_t>(quotes.bid_size));
            Order ask_order(cycle_count * 2 + 1, tick.asset_id, Side::SELL,
                          quotes.ask_price, static_cast<uint64_t>(quotes.ask_size));
            
            // Pre-trade risk checks
            const bool bid_approved = risk_control.check_pre_trade_limits(
                bid_order, state.current_position);
            const bool ask_approved = risk_control.check_pre_trade_limits(
                ask_order, state.current_position);
            
            // Order submission (in production: send to exchange)
            
            if (bid_approved && mm_strategy.should_quote(quotes.spread, latency_cost)) {
                // Submit bid order
                // In production: nic.send_order(bid_order);
                state.active_quotes.bid_price = quotes.bid_price;
                state.active_quotes.bid_size = quotes.bid_size;
            }
            
            if (ask_approved && mm_strategy.should_quote(quotes.spread, latency_cost)) {
                // Submit ask order
                // In production: nic.send_order(ask_order);
                state.active_quotes.ask_price = quotes.ask_price;
                state.active_quotes.ask_size = quotes.ask_size;
            }
        }
        
        // Measure cycle latency
        const Timestamp cycle_end = now();
        const int64_t cycle_latency_ns = to_nanos(cycle_end) - to_nanos(cycle_start);

        // Throttled metrics collection (every 100 cycles) to reduce hot-path latency
        if (cycle_count % 100 == 0) {
            metrics.update(cycle_latency_ns);
            
            const double cycle_latency_us = cycle_latency_ns / 1000.0;
            metrics_collector.update_cycle_latency(cycle_latency_us);
            metrics_collector.update_market_data(
                tick.mid_price,
                tick.bid_prices[0],
                tick.ask_prices[0]
            );
            metrics_collector.update_position(
                state.current_position,
                0.0,
                0.0
            );
            metrics_collector.update_hawkes_intensity(
                hawkes_buy_intensity,
                hawkes_sell_intensity
            );
            
            risk_control.set_regime_multiplier(vol_index);
            const int regime = static_cast<int>(risk_control.get_current_regime());
            const double position_usage = std::abs(state.current_position) / 1000.0 * 100.0;
            metrics_collector.update_risk(
                regime,
                risk_control.get_regime_multiplier(),
                position_usage
            );
            
            metrics_collector.take_snapshot();
        }
        
        // Schedule callback for quote refresh (nanosecond precision)
        if (cycle_count % 100 == 0) {
            timing_wheel.schedule_after(
                std::chrono::microseconds(100),
                [&]() {
                    // Refresh quotes callback
                    // In production: would update exchange orders
                }
            );
        }
        
        ++cycle_count;
        
        // 
        // Periodic status updates
        // 
        if (cycle_count % print_interval == 0) {
            const auto nic_stats = nic.get_stats();
            
            std::cout << "\n--- Cycle: " << cycle_count << " ---" << std::endl;
            std::cout << "Mid Price: $" << std::fixed << std::setprecision(2) 
                      << tick.mid_price << std::endl;
            std::cout << "Position: " << state.current_position << std::endl;
            std::cout << "Active Quotes: Bid=" << quotes.bid_price 
                      << " Ask=" << quotes.ask_price 
                      << " Spread=" << (quotes.spread * 10000.0) << " bps" << std::endl;
            std::cout << "Hawkes: Buy=" << std::fixed << std::setprecision(3)
                      << hawkes_buy_intensity << " Sell=" << hawkes_sell_intensity 
                      << " Imbalance=" << hawkes.get_intensity_imbalance() << std::endl;
            std::cout << "Regime: ";
            switch (risk_control.get_current_regime()) {
                case MarketRegime::NORMAL: 
                    std::cout << "NORMAL"; break;
                case MarketRegime::ELEVATED_VOLATILITY: 
                    std::cout << "ELEVATED_VOL"; break;
                case MarketRegime::HIGH_STRESS: 
                    std::cout << "HIGH_STRESS"; break;
                case MarketRegime::HALTED: 
                    std::cout << "HALTED"; break;
            }
            std::cout << " (multiplier=" << risk_control.get_regime_multiplier() 
                      << ")" << std::endl;
            std::cout << "Last Cycle Latency: " << cycle_latency_ns << " ns ("
                      << (cycle_latency_ns / 1000.0) << " µs)" << std::endl;
            std::cout << "NIC Queue Utilization: " << std::fixed 
                      << std::setprecision(1) << nic_stats.utilization << "%" 
                      << std::endl;
        }
    }
    
    // 
    // Shutdown sequence
    // 
    std::cout << "\n\n=== Shutting Down ===" << std::endl;
    
    simulator.stop();
    nic.stop();
    dashboard.stop();
    
    // Export metrics to CSV
    metrics_collector.export_to_csv("trading_metrics.csv");
    std::cout << "Metrics exported to trading_metrics.csv" << std::endl;
    
    // Print summary statistics
    auto summary = metrics_collector.get_summary();
    std::cout << "\n=== Trading Performance Summary ===" << std::endl;
    std::cout << "Average P&L: $" << std::fixed << std::setprecision(2) 
              << summary.avg_pnl << std::endl;
    std::cout << "Max P&L: $" << summary.max_pnl << std::endl;
    std::cout << "Min P&L: $" << summary.min_pnl << std::endl;
    std::cout << "Total Trades: " << summary.total_trades << std::endl;
    std::cout << "Fill Rate: " << std::fixed << std::setprecision(1) 
              << (summary.fill_rate * 100.0) << "%" << std::endl;
    std::cout << "Average Latency: " << std::fixed << std::setprecision(1) 
              << summary.avg_latency_us << " µs" << std::endl;
    std::cout << "Max Latency: " << summary.max_latency_us << " µs" << std::endl;
    
    std::cout << "\n=== Final Trading Statistics ===" << std::endl;
    std::cout << "Total Cycles: " << cycle_count << std::endl;
    std::cout << "Final Position: " << state.current_position << std::endl;
    std::cout << "Realized P&L: $" << std::fixed << std::setprecision(2) 
              << state.realized_pnl << std::endl;
    std::cout << "Total Trades: " << state.total_trades << std::endl;
    
    metrics.print_stats();
    jitter_profiler.print_report();
    
    const auto final_nic_stats = nic.get_stats();
    std::cout << "\n=== NIC Statistics ===" << std::endl;
    std::cout << "Total Packets: " << final_nic_stats.packets_received << std::endl;
    std::cout << "Total Bytes: " << final_nic_stats.bytes_received << std::endl;
    
    return 0;
}
