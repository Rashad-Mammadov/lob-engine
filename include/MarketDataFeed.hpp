#pragma once

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <set>
#include <shared_mutex>
#include "OrderBook.hpp"

using json = nlohmann::json;
typedef websocketpp::server<websocketpp::config::asio> WsServer;
typedef websocketpp::connection_hdl connection_hdl;

class MarketDataFeed {
private:
  WsServer server;
  OrderBook& lob;
  
  std::set<connection_hdl, std::owner_less<connection_hdl>> connections;
  std::shared_mutex connection_mutex;
  
  std::thread ws_thread;
  std::thread publisher_thread;
  std::atomic<bool> running{true};

  // Broadcast loop: Polls the engine's Outbound Ring Buffer and sends to all clients
  void publisherLoop() {
    Trade trade;
    while (running.load(std::memory_order_relaxed)) {
      if (lob.pollTrade(trade)) {
        // Serialize the trade to JSON once
        json trade_json = {
          {"type", "trade"},
          {"maker_order_id", trade.maker_order_id},
          {"taker_order_id", trade.taker_order_id},
          {"price", trade.price},
          {"quantity", trade.quantity}
        };
        std::string payload = trade_json.dump();

        // Broadcast to all active connections
        std::shared_lock<std::shared_mutex> lock(connection_mutex);
        for (auto hdl : connections) {
          try {
            server.send(hdl, payload, websocketpp::frame::opcode::text);
          } catch (...) {
            // Ignore dead connections, they will be cleaned up by the onClose handler
          }
        }
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
  MarketDataFeed(OrderBook& order_book) : lob(order_book) {
    // Suppress verbose WebSocket logging for maximum performance
    server.clear_access_channels(websocketpp::log::alevel::all);
    
    server.init_asio();

    // Connection Handlers
    server.set_open_handler([this](connection_hdl hdl) {
      std::unique_lock<std::shared_mutex> lock(connection_mutex);
      connections.insert(hdl);
    });

    server.set_close_handler([this](connection_hdl hdl) {
      std::unique_lock<std::shared_mutex> lock(connection_mutex);
      connections.erase(hdl);
    });

    server.listen(8080);
    server.start_accept();

    // Boot the threads
    ws_thread = std::thread([this]() { server.run(); });
    publisher_thread = std::thread(&MarketDataFeed::publisherLoop, this);
    
    std::cout << "WebSocket Market Data feed live on port 8080..." << std::endl;
  }

  ~MarketDataFeed() {
    running.store(false, std::memory_order_relaxed);
    server.stop_listening();
    if (ws_thread.joinable()) ws_thread.join();
    if (publisher_thread.joinable()) publisher_thread.join();
  }
};
