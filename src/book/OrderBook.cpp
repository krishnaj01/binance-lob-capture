#include "book/OrderBook.hpp"

#include <algorithm>
#include <iterator>

namespace blc {

OrderBook::OrderBook(std::size_t max_levels_per_side) : max_levels_(max_levels_per_side) {
    bids_.reserve(1024);
    asks_.reserve(1024);
}

void OrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
}

std::vector<Level>::iterator OrderBook::find_not_worse(Side side, std::int64_t price) {
    auto& v = levels(side);
    return std::lower_bound(v.begin(), v.end(), price, [side](const Level& l, std::int64_t p) {
        return is_better(side, p, l.price);  // l is strictly worse than p
    });
}

void OrderBook::set_level(Side side, std::int64_t price, std::int64_t qty) {
    auto& v = levels(side);
    const auto it = find_not_worse(side, price);
    const bool found = it != v.end() && it->price == price;
    if (qty == 0) {
        if (found) v.erase(it);
        return;
    }
    if (found) {
        it->qty = qty;
        return;
    }
    v.insert(it, Level{price, qty});
    if (v.size() > max_levels_) {
        v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() - max_levels_));
    }
}

void OrderBook::replace_top(Side side, const std::vector<Level>& best_first,
                            std::size_t requested_depth) {
    auto& v = levels(side);
    if (best_first.size() < requested_depth) {
        v.clear();
    } else {
        v.erase(find_not_worse(side, best_first.back().price), v.end());
    }
    v.insert(v.end(), best_first.rbegin(), best_first.rend());
}

std::size_t OrderBook::top(Side side, Level* out, std::size_t n) const noexcept {
    const auto& v = levels(side);
    const std::size_t count = std::min(n, v.size());
    std::copy_n(v.rbegin(), count, out);
    return count;
}

std::optional<Level> OrderBook::best(Side side) const noexcept {
    const auto& v = levels(side);
    if (v.empty()) return std::nullopt;
    return v.back();
}

bool OrderBook::is_valid_snapshot(Side side, const std::vector<Level>& best_first) noexcept {
    for (std::size_t i = 0; i < best_first.size(); ++i) {
        if (best_first[i].qty <= 0) return false;
        if (i > 0 && !is_better(side, best_first[i - 1].price, best_first[i].price)) return false;
    }
    return true;
}

}  // namespace blc
