#pragma once

#include "common_types.hpp"
#include <type_traits>
#include <cmath>

 namespace hft {
 namespace compile_time {

 struct AvellanedaStoikovStrategy {};
 struct GueantLehalleTavinStrategy {};
 struct SimpleMarketMakingStrategy {};

 struct StrictRiskPolicy {};
 struct ModerateRiskPolicy {};
struct AggressiveRiskPolicy {};

namespace math {

constexpr double sqrt_impl(double data, double curr, double prev) {
return curr == prev ? curr : sqrt_impl(data, 0.5 * (curr + data / curr), curr);
}

constexpr double sqrt(double data) {
return data >= 0.0 ? sqrt_impl(data, data, 0.0) : std::numeric_limits<double>::quiet_NaN();
}

constexpr double pow(double base, int exp) {
return exp == 0 ? 1.0 :
exp == 1 ? base :
exp > 0 ? base * pow(base, exp - 1) :
1.0 / pow(base, -exp);
}

constexpr double abs(double data) {
return data >= 0.0 ? data : -data;
}

constexpr double minimum(double a, double b) {
return a < b ? a : b;
}

           constexpr double maximum(double a, double b) {
           return a > b ? a : b;
           }

constexpr double clamp(double data, double min_val, double max_val) {
return data < min_val ? min_val : (data > max_val ? max_val : data);
    }

}

template<typename RiskPolicy>
    struct RiskParameters {

};

template<>
    struct RiskParameters<StrictRiskPolicy> {
static constexpr double MAX_POSITION_SIZE = 100.0;
static constexpr double MAX_ORDER_SIZE = 10.0;
static constexpr double MAX_DAILY_LOSS = 10000.0;
static constexpr double MIN_SPREAD_BPS = 5.0;
static constexpr bool ALLOW_NAKED_SHORTS = false;
};

template<>
struct RiskParameters<ModerateRiskPolicy> {
static constexpr double MAX_POSITION_SIZE = 500.0;
    static constexpr double MAX_ORDER_SIZE = 50.0;
static constexpr double MAX_DAILY_LOSS = 50000.0;
static constexpr double MIN_SPREAD_BPS = 2.0;
static constexpr bool ALLOW_NAKED_SHORTS = false;
};

    template<>
    struct RiskParameters<AggressiveRiskPolicy> {
    static constexpr double MAX_POSITION_SIZE = 1000.0;
    static constexpr double MAX_ORDER_SIZE = 100.0;
static constexpr double MAX_DAILY_LOSS = 100000.0;
static constexpr double MIN_SPREAD_BPS = 1.0;
static constexpr bool ALLOW_NAKED_SHORTS = true;
};

template<typename RiskPolicy>
class CompileTimeRiskChecker {
public:
using Params = RiskParameters<RiskPolicy>;

    static inline bool check_order(
    double current_position,
    double order_size,
Side side,
double daily_pnl,
double spread_bps
) {

double new_position = current_position + (side == Side::BUY ? order_size : -order_size);
    if (math::abs(new_position) > Params::MAX_POSITION_SIZE) {
    return false;
    }

     if (order_size > Params::MAX_ORDER_SIZE) {
     return false;
     }

        if (daily_pnl < -Params::MAX_DAILY_LOSS) {
        return false;
        }

        if (spread_bps < Params::MIN_SPREAD_BPS) {
        return false;
        }

        if constexpr (!Params::ALLOW_NAKED_SHORTS) {
            if (side == Side::SELL && current_position <= 0.0) {
        return false;
        }
        }

            return true;
        }

        static constexpr inline bool check_position_limit(double position) {
            return math::abs(position) <= Params::MAX_POSITION_SIZE;
        }

        static constexpr inline bool check_order_size(double size) {
        return size <= Params::MAX_ORDER_SIZE;
            }

        static constexpr inline bool check_daily_loss(double pnl) {
        return pnl >= -Params::MAX_DAILY_LOSS;
        }

                static constexpr inline bool check_min_spread(double spread_bps) {
            return spread_bps >= Params::MIN_SPREAD_BPS;
        }
        };

        template<typename Strategy>
    struct StrategyParameters {

    };

    template<>
    struct StrategyParameters<AvellanedaStoikovStrategy> {
    static constexpr double RISK_AVERSION = 0.1;
        static constexpr double VOLATILITY = 0.02;
    static constexpr double TIME_HORIZON = 1.0;
    static constexpr double INVENTORY_PENALTY = 0.01;
    static constexpr double MIN_SPREAD = 0.0001;
        static constexpr double MAX_SPREAD = 0.01;
    };

template<>
struct StrategyParameters<SimpleMarketMakingStrategy> {
static constexpr double BASE_SPREAD_BPS = 5.0;
static constexpr double INVENTORY_SKEW_FACTOR = 0.1;
static constexpr double MIN_SPREAD_BPS = 2.0;
static constexpr double MAX_SPREAD_BPS = 20.0;
};

    template<typename Strategy>
    class CompileTimeStrategyEngine {
    public:
    using Params = StrategyParameters<Strategy>;

struct Quote {
double bid_price;
double ask_price;
double bid_size;
    double ask_size;
    };

    static inline Quote compute_quotes(
double mid_price,
double inventory,
double volatility,
double time_remaining,
double risk_multiplier = 1.0
) {
if constexpr (std::is_same_v<Strategy, AvellanedaStoikovStrategy>) {
return compute_avellaneda_stoikov(
mid_price, inventory, volatility, time_remaining, risk_multiplier
    );
    } else if constexpr (std::is_same_v<Strategy, SimpleMarketMakingStrategy>) {
    return compute_simple_mm(
     mid_price, inventory, volatility, risk_multiplier
     );
     } else {

     static_assert(
    std::is_same_v<Strategy, AvellanedaStoikovStrategy> ||
        std::is_same_v<Strategy, SimpleMarketMakingStrategy>,
        "Unknown strategy type"
        );
        }
    }

    private:

        static inline Quote compute_avellaneda_stoikov(
        double mid_price,
        double inventory,
        double volatility,
    double time_remaining,
        double risk_multiplier
            ) {

            constexpr double gamma = Params::RISK_AVERSION;
        constexpr double inventory_penalty = Params::INVENTORY_PENALTY;

            double reservation_price = mid_price - gamma * volatility * volatility *
        time_remaining * inventory;

                double optimal_spread = gamma * volatility * volatility * time_remaining;
                optimal_spread *= risk_multiplier;

    optimal_spread = math::clamp(optimal_spread, Params::MIN_SPREAD, Params::MAX_SPREAD);

    double inventory_skew = inventory_penalty * inventory;

     double bid_offset = 0.5 * optimal_spread + inventory_skew;
     double ask_offset = 0.5 * optimal_spread - inventory_skew;

     Quote quote;
     quote.bid_price = reservation_price - bid_offset;
     quote.ask_price = reservation_price + ask_offset;
    quote.bid_size = 10.0;
        quote.ask_size = 10.0;

        return quote;
        }

        static inline Quote compute_simple_mm(
        double mid_price,
        double inventory,
        double volatility,
        double risk_multiplier
        ) {
                                   constexpr double base_spread = Params::BASE_SPREAD_BPS / 10000.0;
        constexpr double skew_factor = Params::INVENTORY_SKEW_FACTOR;

        double spread = mid_price * base_spread * risk_multiplier;
        spread = math::clamp(
        spread,
        mid_price * Params::MIN_SPREAD_BPS / 10000.0,
        mid_price * Params::MAX_SPREAD_BPS / 10000.0
        );

        double skew = inventory * skew_factor * spread;

        Quote quote;
        quote.bid_price = mid_price - 0.5 * spread + skew;
        quote.ask_price = mid_price + 0.5 * spread + skew;
        quote.bid_size = 10.0;
        quote.ask_size = 10.0;

        return quote;
        }
        };

    using DefaultStrategyEngine = CompileTimeStrategyEngine<AvellanedaStoikovStrategy>;
        using DefaultRiskChecker = CompileTimeRiskChecker<ModerateRiskPolicy>;

        using AggressiveStrategyEngine = CompileTimeStrategyEngine<AvellanedaStoikovStrategy>;
    using AggressiveRiskChecker = CompileTimeRiskChecker<AggressiveRiskPolicy>;

        using ConservativeStrategyEngine = CompileTimeStrategyEngine<SimpleMarketMakingStrategy>;
        using ConservativeRiskChecker = CompileTimeRiskChecker<StrictRiskPolicy>;

        inline void example_usage() {

        using Strategy = DefaultStrategyEngine;
        using Risk = DefaultRiskChecker;

        auto quote = Strategy::compute_quotes(100.0, 50.0, 0.02, 1.0, 1.0);

        bool ok = Risk::check_order(50.0, 10.0, Side::BUY, -5000.0, 5.0);

}

}
}