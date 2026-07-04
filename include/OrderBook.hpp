#pragma once

#include <iostream>
#include <map>
#include <unordered_map>
#include <deque>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <thread>
#include "RingBuffer.hpp"

struct Order {
  uint64_t order_id;
  double price;
  uint32_t quantity;
};

struct Trade {
  uint64_t maker_order_id;
  uint64_t taker_order_id;
  double price;
  uint32_t quantity;
};

enum class OrderAction { ADD, CANCEL };

struct OrderPayload {
  OrderAction action;
  Order order;
  bool is_bid;
};

class OrderBook {
private:
  std::map<double, std::deque<Order>, std::greater<double>> bids;
  std::map<double, std::deque<Order>> asks; 
  std::unordered_map<uint64_t, std::pair<bool, double>> active_orders;  

  // The Lock-Free Queue (Capacity must be a power of 2, 65536 is a safe buffer)
  RingBuffer<OrderPayload, 65536> ingestion_queue;
  RingBuffer<Trade, 65536> outbound_queue;

  std::atomic<bool> engine_running{true};
  std::thread matching_thread;
  std::atomic<double> best_bid_price{0.0};
  std::atomic<double> best_ask_price{0.0};

  // Internal unlocked matching logic
  template <typename MapType>
  void matchOrder(Order& taker, MapType& resting_book, std::vector<Trade>& trades, bool is_buy) {
    auto it = resting_book.begin();
    while (it != resting_book.end() && taker.quantity > 0) {
      double resting_price = it->first;
      if (is_buy && taker.price < resting_price) break;
      if (!is_buy && taker.price > resting_price) break;

      auto& queue = it->second;
      while (!queue.empty() && taker.quantity > 0) {
        Order& maker = queue.front();
        uint32_t traded_qty = std::min(taker.quantity, maker.quantity);
        trades.push_back({maker.order_id, taker.order_id, resting_price, traded_qty});
        taker.quantity -= traded_qty;
        maker.quantity -= traded_qty;
        if (maker.quantity == 0) {
          active_orders.erase(maker.order_id);
	  queue.pop_front();
        }
      }

      if (queue.empty()) {
        it = resting_book.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Templated helper to handle the different map sorting types safely
  template <typename MapType>
  void removeOrderFromTree(MapType& tree, double price, uint64_t id) {
    auto price_it = tree.find(price);
    if (price_it != tree.end()) {
      auto& queue = price_it->second;
      
      // Erase-Remove idiom to delete the specific order from the deque
      queue.erase(std::remove_if(queue.begin(), queue.end(), 
                  [id](const Order& o) { return o.order_id == id; }), queue.end());
      
      // If that was the last order at this price level, destroy the price node
      if (queue.empty()) {
        tree.erase(price_it);
      }
    }
  }

  // The infinite loop that runs on the dedicated background CPU core
  void engineLoop() {
    OrderPayload payload;
    while (engine_running.load(std::memory_order_relaxed)) {
      if (ingestion_queue.pop(payload)) {
        // We popped an order! Match it instantly with ZERO kernel locks.
        
	if (payload.action == OrderAction::ADD) {
	  std::vector<Trade> executed_trades;
          if (payload.is_bid) {
            matchOrder(payload.order, asks, executed_trades, true);
            if (payload.order.quantity > 0) {
              bids[payload.order.price].push_back(payload.order);
              active_orders[payload.order.order_id] = {true, payload.order.price};
            }
          } else {
            matchOrder(payload.order, bids, executed_trades, false);
            if (payload.order.quantity > 0) {
              asks[payload.order.price].push_back(payload.order);
              active_orders[payload.order.order_id] = {false, payload.order.price};
            }
          }

          // Broadcast executions
          for (const auto& trade : executed_trades) {
            outbound_queue.try_push(trade);
          }

        } else if (payload.action == OrderAction::CANCEL) {
          uint64_t id = payload.order.order_id;
          auto it = active_orders.find(id);
          
          if (it != active_orders.end()) {
            bool is_bid = it->second.first;
            double price = it->second.second;

            if (is_bid) {
              removeOrderFromTree(bids, price, id);
            } else {
              removeOrderFromTree(asks, price, id);
            }

            active_orders.erase(it); // Remove from tracker
          }
        }

	// Publish the new top of book safely
        double current_best_bid = bids.empty() ? 0.0 : bids.begin()->first;
        double current_best_ask = asks.empty() ? 0.0 : asks.begin()->first;
        
        best_bid_price.store(current_best_bid, std::memory_order_relaxed);
        best_ask_price.store(current_best_ask, std::memory_order_relaxed);
      } else {
        #if defined(__aarch64__) || defined(_M_ARM64)
          asm volatile("yield");
        #else
          asm volatile("pause");
        #endif
      }
    }
  }

public:
  OrderBook() {
    // Boot up the matching engine on a background thread instantly
    matching_thread = std::thread(&OrderBook::engineLoop, this);
  }

  ~OrderBook() {
    engine_running.store(false, std::memory_order_relaxed);
    if (matching_thread.joinable()) {
      matching_thread.join();
    }
  }

  // Returns true if queued, false if the engine is overwhelmed
  bool enqueueOrder(Order taker_order, bool is_bid) {
    return ingestion_queue.try_push({OrderAction::ADD, taker_order, is_bid});
  }

  bool cancelOrder(uint64_t order_id) {
    Order dummy{order_id, 0.0, 0}; // Price and quantity don't matter for a cancel
    return ingestion_queue.try_push({OrderAction::CANCEL, dummy, false});
  }
  
  // Non-blocking poll for the WebSocket Publisher Thread
  bool pollTrade(Trade& out_trade) {
    return outbound_queue.pop(out_trade);
  }

  std::pair<double, double> getSpread() const {
    return {
      best_bid_price.load(std::memory_order_relaxed),
      best_ask_price.load(std::memory_order_relaxed)
    };
  }
};
