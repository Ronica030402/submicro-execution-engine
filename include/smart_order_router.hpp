#pragma once

#include "common_types.hpp"
#include "avellaneda_stoikov.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <optional>
#include <algorithm>
#include <cmath>

using hft::Timestamp;
using hft::Duration;
using hft::MarketRegime;
using hft::now;
using hft::to_nanos;

using hft::DynamicMMStrategy;

struct VenueInfo {
    std::string venue_id;
    std::string venue_name;
    bool is_active;

    std::string endpoint;
    double baseline_latency_us;

    double maker_fee_bps;
    double taker_fee_bps;
    double min_order_size;
    double max_order_size;

    double typical_bid_depth;
    double typical_ask_depth;
    double fill_rate;
};

struct VenueState {
    Timestamp last_heartbeat_sent;
    Timestamp last_heartbeat_received;

    double current_rtt_us;
    double ema_rtt_us;
    double std_dev_rtt_us;

    bool is_connected;
    uint64_t consecutive_timeouts;
    uint64_t total_heartbeats_sent;
    uint64_t total_heartbeats_received;

    uint64_t orders_sent;
    uint64_t orders_filled;
    uint64_t orders_rejected;
    uint64_t orders_timeout;
};

struct RoutingDecision {
    std::string selected_venue;
    double expected_latency_us;
    double latency_budget_us;
    double price_quality;
    double latency_quality;
    double liquidity_quality;
    double composite_score;
    std::string rejection_reason;
};

struct RoutingConfig {

    double latency_safety_margin;
    double latency_spike_threshold;

    double price_weight;
    double latency_weight;
    double liquidity_weight;

    double min_fill_rate;
    double min_composite_score;

    int64_t heartbeat_interval_ms;
    int64_t heartbeat_timeout_ms;
    double rtt_ema_alpha;
};

class SmartOrderRouter {
public:

    explicit SmartOrderRouter(const RoutingConfig& config = default_config())
        : config_(config)
        , as_model_(nullptr)
    {
    }

    bool initialize(DynamicMMStrategy* as_model) {
        if (!as_model) {
            return false;
        }

        as_model_ = as_model;

        initialize_venues();

        return true;
    }

    void add_venue(const VenueInfo& venue) {
        venues_[venue.venue_id] = venue;

        VenueState state;
        state.last_heartbeat_sent = Timestamp{};
        state.last_heartbeat_received = Timestamp{};
        state.current_rtt_us = venue.baseline_latency_us;
        state.ema_rtt_us = venue.baseline_latency_us;
        state.std_dev_rtt_us = venue.baseline_latency_us * 0.1;
        state.is_connected = true;
        state.consecutive_timeouts = 0;
        state.total_heartbeats_sent = 0;
        state.total_heartbeats_received = 0;
        state.orders_sent = 0;
        state.orders_filled = 0;
        state.orders_rejected = 0;
        state.orders_timeout = 0;

        venue_states_[venue.venue_id] = state;
    }

    void remove_venue(const std::string& venue_id) {
        venues_.erase(venue_id);
        venue_states_.erase(venue_id);
    }

    std::vector<std::string> get_active_venues() const {
        std::vector<std::string> active;
        for (const auto& [venue_id, venue] : venues_) {
            if (venue.is_active && venue_states_.at(venue_id).is_connected) {
                active.push_back(venue_id);
            }
        }
        return active;
    }

    void send_heartbeat(const std::string& venue_id, Timestamp now) {
        auto it = venue_states_.find(venue_id);
        if (it == venue_states_.end()) {
            return;
        }

        VenueState& state = it->second;
        state.last_heartbeat_sent = now;
        state.total_heartbeats_sent++;

    }

    void receive_heartbeat(const std::string& venue_id, Timestamp sent_time, Timestamp received_time) {
        auto it = venue_states_.find(venue_id);
        if (it == venue_states_.end()) {
            return;
        }

        VenueState& state = it->second;
        state.last_heartbeat_received = received_time;
        state.total_heartbeats_received++;
        state.consecutive_timeouts = 0;
        state.is_connected = true;

        const double rtt_ns = static_cast<double>((received_time - sent_time).count());
        const double rtt_us = rtt_ns / 1000.0;
        state.current_rtt_us = rtt_us;

        const double alpha = config_.rtt_ema_alpha;
        state.ema_rtt_us = alpha * rtt_us + (1.0 - alpha) * state.ema_rtt_us;

        const double delta = rtt_us - state.ema_rtt_us;
        state.std_dev_rtt_us = std::sqrt(
            alpha * delta * delta + (1.0 - alpha) * state.std_dev_rtt_us * state.std_dev_rtt_us
        );
    }

    void check_heartbeat_timeouts(Timestamp now) {
        const int64_t timeout_ns = config_.heartbeat_timeout_ms * 1'000'000;

        for (auto& [venue_id, state] : venue_states_) {
            if (state.last_heartbeat_sent == Timestamp{}) {
                continue;
            }

            const Duration time_since_sent_duration = now - state.last_heartbeat_sent;
            const int64_t time_since_sent = std::chrono::duration_cast<std::chrono::nanoseconds>(time_since_sent_duration).count();

            if (time_since_sent > timeout_ns && state.is_connected) {

                state.consecutive_timeouts++;

                if (state.consecutive_timeouts >= 3) {

                    state.is_connected = false;
                }
            }
        }
    }

    double calculate_latency_budget(
        double mid_price,
        double current_volatility,
        int32_t current_position,
        int32_t order_size,
        MarketRegime regime
    ) const {
        if (!as_model_) {

            return 1000.0;
        }

        auto quotes = as_model_->calculate_quotes(
            mid_price,
            current_position,
            600.0,
            0.0
        );

        const double latency_cost = as_model_->calculate_latency_cost(
            current_volatility,
            mid_price
        );

        const double bid_spread = mid_price - quotes.bid_price;
        const double ask_spread = quotes.ask_price - mid_price;
        const double expected_profit = (order_size > 0) ? ask_spread : bid_spread;

        double urgency_multiplier = 1.0;
        switch (regime) {
            case MarketRegime::NORMAL:
                urgency_multiplier = 1.0;
                break;
            case MarketRegime::ELEVATED_VOLATILITY:
                urgency_multiplier = 1.5;
                break;
            case MarketRegime::HIGH_STRESS:
                urgency_multiplier = 3.0;
                break;
            case MarketRegime::HALTED:
                urgency_multiplier = 10.0;
                break;
        }

        const double position_ratio = static_cast<double>(current_position) / 1000.0;
        const double position_urgency = 1.0 + std::abs(position_ratio);
        urgency_multiplier *= position_urgency;

        double latency_budget_us;

        if (expected_profit > latency_cost * 1.1) {

            const double profit_margin = expected_profit - latency_cost;

            latency_budget_us = (profit_margin / current_volatility) *
                               (1000.0 / urgency_multiplier);

            latency_budget_us = std::clamp(latency_budget_us, 100.0, 10000.0);
        } else {

            latency_budget_us = 100.0;
        }

        latency_budget_us *= config_.latency_safety_margin;

        return latency_budget_us;
    }

    RoutingDecision route_order(
        double mid_price,
        double current_volatility,
        int32_t current_position,
        int32_t order_size,
        MarketRegime regime,
        const std::unordered_map<std::string, double>& venue_prices
    ) {
        RoutingDecision decision;
        decision.selected_venue = "";
        decision.rejection_reason = "";

        decision.latency_budget_us = calculate_latency_budget(
            mid_price,
            current_volatility,
            current_position,
            order_size,
            regime
        );

        std::vector<std::string> candidate_venues;

        for (const auto& [venue_id, venue] : venues_) {
            if (!venue.is_active) {
                continue;
            }

            const auto& state = venue_states_.at(venue_id);

            if (!state.is_connected) {
                continue;
            }

            const double venue_latency = state.ema_rtt_us;
            if (venue_latency > decision.latency_budget_us) {
                continue;
            }

            const double spike_threshold = state.ema_rtt_us +
                                          (config_.latency_spike_threshold * state.std_dev_rtt_us);
            if (state.current_rtt_us > spike_threshold) {
                continue;
            }

            const double fill_rate = (state.orders_sent > 0) ?
                static_cast<double>(state.orders_filled) / state.orders_sent :
                venue.fill_rate;

            if (fill_rate < config_.min_fill_rate) {
                continue;
            }

            const double abs_order_size = std::abs(static_cast<double>(order_size));
            if (abs_order_size < venue.min_order_size ||
                abs_order_size > venue.max_order_size) {
                continue;
            }

            candidate_venues.push_back(venue_id);
        }

        if (candidate_venues.empty()) {
            decision.rejection_reason = "No venues meet latency budget (" +
                                       std::to_string(decision.latency_budget_us) +
                                       " us) and connectivity requirements";
            return decision;
        }

        std::vector<std::pair<std::string, double >>scored_venues;

        for (const auto& venue_id : candidate_venues) {
            const auto& venue = venues_.at(venue_id);
            const auto& state = venue_states_.at(venue_id);

            double price_quality = 0.0;
            if (venue_prices.count(venue_id) > 0) {
                const double venue_price = venue_prices.at(venue_id);

                double best_price = venue_price;
                for (const auto& [vid, price] : venue_prices) {
                    if (order_size > 0) {
                        best_price = std::min(best_price, price);
                    } else {
                        best_price = std::max(best_price, price);
                    }
                }

                const double price_diff = (order_size > 0) ?
                    (venue_price - best_price) / best_price :
                    (best_price - venue_price) / best_price;

                price_quality = std::max(0.0, 1.0 - (price_diff * 100.0));
            } else {
                price_quality = 0.5;
            }

            const double latency_ratio = state.ema_rtt_us / decision.latency_budget_us;
            const double latency_quality = std::max(0.0, 1.0 - latency_ratio);

            const double required_liquidity = std::abs(static_cast<double>(order_size));
            const double available_liquidity = (order_size > 0) ?
                venue.typical_ask_depth : venue.typical_bid_depth;
            const double liquidity_ratio = std::min(1.0, available_liquidity / required_liquidity);
            const double liquidity_quality = liquidity_ratio;

            const double composite_score =
                config_.price_weight * price_quality +
                config_.latency_weight * latency_quality +
                config_.liquidity_weight * liquidity_quality;

            scored_venues.push_back({venue_id, composite_score});
        }

        auto best_venue = std::max_element(
            scored_venues.begin(),
            scored_venues.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; }
        );

        if (best_venue == scored_venues.end() || best_venue->second < config_.min_composite_score) {
            decision.rejection_reason = "No venues meet minimum composite score (" +
                                       std::to_string(config_.min_composite_score) + ")";
            return decision;
        }

        decision.selected_venue = best_venue->first;
        decision.composite_score = best_venue->second;
        decision.expected_latency_us = venue_states_.at(best_venue->first).ema_rtt_us;

        const auto& venue = venues_.at(best_venue->first);
        const auto& state = venue_states_.at(best_venue->first);

        if (venue_prices.count(best_venue->first) > 0) {
            const double venue_price = venue_prices.at(best_venue->first);
            double best_price = venue_price;
            for (const auto& [vid, price] : venue_prices) {
                if (order_size > 0) {
                    best_price = std::min(best_price, price);
                } else {
                    best_price = std::max(best_price, price);
                }
            }
            const double price_diff = (order_size > 0) ?
                (venue_price - best_price) / best_price :
                (best_price - venue_price) / best_price;
            decision.price_quality = std::max(0.0, 1.0 - (price_diff * 100.0));
        }

        decision.latency_quality = std::max(0.0, 1.0 - (state.ema_rtt_us / decision.latency_budget_us));

        const double required_liquidity = std::abs(static_cast<double>(order_size));
        const double available_liquidity = (order_size > 0) ?
            venue.typical_ask_depth : venue.typical_bid_depth;
        decision.liquidity_quality = std::min(1.0, available_liquidity / required_liquidity);

        return decision;
    }

    void record_order_result(const std::string& venue_id, bool filled, bool timeout) {
        auto it = venue_states_.find(venue_id);
        if (it == venue_states_.end()) {
            return;
        }

        VenueState& state = it->second;
        state.orders_sent++;

        if (filled) {
            state.orders_filled++;
        } else if (timeout) {
            state.orders_timeout++;
        } else {
            state.orders_rejected++;
        }
    }

    std::optional<VenueState> get_venue_state(const std::string& venue_id) const {
        auto it = venue_states_.find(venue_id);
        if (it != venue_states_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    const std::unordered_map<std::string, VenueState>& get_all_venue_states() const {
        return venue_states_;
    }

private:

    static RoutingConfig default_config() {
        RoutingConfig config;

        config.latency_safety_margin = 0.8;
        config.latency_spike_threshold = 2.0;

        config.price_weight = 0.5;
        config.latency_weight = 0.3;
        config.liquidity_weight = 0.2;

        config.min_fill_rate = 0.85;
        config.min_composite_score = 0.6;

        config.heartbeat_interval_ms = 100;
        config.heartbeat_timeout_ms = 1000;
        config.rtt_ema_alpha = 0.2;

        return config;
    }

    void initialize_venues() {

        VenueInfo binance;
        binance.venue_id = "BINANCE";
        binance.venue_name = "Binance";
        binance.is_active = true;
        binance.endpoint = "api.binance.com:443";
        binance.baseline_latency_us = 500.0;
        binance.maker_fee_bps = -1.0;
        binance.taker_fee_bps = 4.0;
        binance.min_order_size = 0.001;
        binance.max_order_size = 10000.0;
        binance.typical_bid_depth = 5000.0;
        binance.typical_ask_depth = 5000.0;
        binance.fill_rate = 0.95;
        add_venue(binance);

        VenueInfo coinbase;
        coinbase.venue_id = "COINBASE";
        coinbase.venue_name = "Coinbase Pro";
        coinbase.is_active = true;
        coinbase.endpoint = "api.pro.coinbase.com:443";
        coinbase.baseline_latency_us = 800.0;
        coinbase.maker_fee_bps = 0.0;
        coinbase.taker_fee_bps = 5.0;
        coinbase.min_order_size = 0.01;
        coinbase.max_order_size = 5000.0;
        coinbase.typical_bid_depth = 3000.0;
        coinbase.typical_ask_depth = 3000.0;
        coinbase.fill_rate = 0.90;
        add_venue(coinbase);

        VenueInfo kraken;
        kraken.venue_id = "KRAKEN";
        kraken.venue_name = "Kraken";
        kraken.is_active = true;
        kraken.endpoint = "api.kraken.com:443";
        kraken.baseline_latency_us = 1200.0;
        kraken.maker_fee_bps = 0.0;
        kraken.taker_fee_bps = 6.0;
        kraken.min_order_size = 0.01;
        kraken.max_order_size = 3000.0;
        kraken.typical_bid_depth = 2000.0;
        kraken.typical_ask_depth = 2000.0;
        kraken.fill_rate = 0.88;
        add_venue(kraken);
    }

    RoutingConfig config_;
    DynamicMMStrategy* as_model_;

    std::unordered_map<std::string, VenueInfo> venues_;
    std::unordered_map<std::string, VenueState> venue_states_;
};