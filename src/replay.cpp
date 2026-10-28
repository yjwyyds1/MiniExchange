#include "exchange/replay.hpp"

#include <istream>
#include <ostream>
#include <sstream>

namespace exchange {

namespace {

char side_char(Side side) {
    return side == Side::Buy ? 'B' : 'S';
}

std::optional<Side> parse_side(char value) {
    if (value == 'B') return Side::Buy;
    if (value == 'S') return Side::Sell;
    return std::nullopt;
}

char type_char(OrderType type) {
    return type == OrderType::Limit ? 'L' : 'M';
}

std::optional<OrderType> parse_type(char value) {
    if (value == 'L') return OrderType::Limit;
    if (value == 'M') return OrderType::Market;
    return std::nullopt;
}

}  // namespace

std::vector<Event> generate_events(OrderGenerator& generator, std::size_t count) {
    std::vector<Event> events;
    events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        events.push_back(generator.next());
    }
    return events;
}

std::string serialize_event(const Event& event) {
    std::ostringstream out;
    if (event.kind == Event::Kind::CancelOrder) {
        out << "C " << event.cancel_id;
    } else {
        out << "N " << event.order.id << ' ' << side_char(event.order.side) << ' '
            << type_char(event.order.type) << ' ' << event.order.price << ' '
            << event.order.qty;
    }
    return out.str();
}

std::optional<Event> parse_event(std::string_view line) {
    std::istringstream in{std::string(line)};
    char kind = 0;
    if (!(in >> kind)) return std::nullopt;

    Event event;
    if (kind == 'C') {
        event.kind = Event::Kind::CancelOrder;
        if (!(in >> event.cancel_id)) return std::nullopt;
        return event;
    }
    if (kind != 'N') return std::nullopt;

    char side = 0;
    char type = 0;
    event.kind = Event::Kind::NewOrder;
    if (!(in >> event.order.id >> side >> type >> event.order.price >>
          event.order.qty)) {
        return std::nullopt;
    }
    const auto parsed_side = parse_side(side);
    const auto parsed_type = parse_type(type);
    if (!parsed_side || !parsed_type) return std::nullopt;
    event.order.side = *parsed_side;
    event.order.type = *parsed_type;
    return event;
}

void write_events(const std::vector<Event>& events, std::ostream& out) {
    for (const auto& event : events) {
        out << serialize_event(event) << '\n';
    }
}

std::optional<std::vector<Event>> read_events(std::istream& in) {
    std::vector<Event> events;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto event = parse_event(line);
        if (!event) return std::nullopt;
        events.push_back(*event);
    }
    return events;
}

void replay(const std::vector<Event>& events, MatchingEngine& engine,
            std::ostream& out) {
    for (const auto& event : events) {
        if (event.kind == Event::Kind::CancelOrder) {
            engine.cancel(event.cancel_id);
            continue;
        }
        const auto result = engine.submit(event.order);
        for (const auto& trade : result.trades) {
            out << "T " << trade.buy_id << ' ' << trade.sell_id << ' '
                << trade.price << ' ' << trade.qty << ' ' << trade.seq << '\n';
        }
    }
    const auto snapshot = engine.snapshot();
    out << "S " << snapshot.best_bid << ' ' << snapshot.bid_qty << ' '
        << snapshot.best_ask << ' ' << snapshot.ask_qty << ' ' << snapshot.seq
        << '\n';
}

}  // namespace exchange
