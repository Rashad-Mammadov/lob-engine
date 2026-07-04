#include <iostream>
#include <string>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "OrderBook.hpp"
#include "MarketDataFeed.hpp"

using json = nlohmann::json;

int main() {
  OrderBook lob;
  MarketDataFeed feed(lob);
  httplib::Server svr;

  // Endpoint 1: Ingest Limit Orders Asynchronously
  svr.Post("/order", [&lob](const httplib::Request& req, httplib::Response& res) {
    try {
      auto body = json::parse(req.body);
      
      Order new_order{
        body["order_id"].get<uint64_t>(),
        body["price"].get<double>(),
        body["quantity"].get<uint32_t>()
      };
      bool is_bid = body["is_bid"].get<bool>();

      // Attempt to push to the Ring Buffer
      bool accepted = lob.enqueueOrder(new_order, is_bid);

      if (accepted) {
        res.status = 200;
        res.set_content("{\"status\": \"queued\"}\n", "application/json");
      } else {
        // LOAD SHEDDING: The engine is choking. Instantly drop the request gracefully.
        res.status = 429;
        res.set_content("{\"error\": \"Engine at capacity. Slow down.\"}\n", "application/json");
      }

    } catch (const json::exception& e) {
      res.status = 400;
      res.set_content("{\"error\": \"Invalid JSON payload\"}\n", "application/json");
    }
  });

  // Endpoint 2: Cancel Existing Orders Asynchronously
  svr.Delete("/order", [&lob](const httplib::Request& req, httplib::Response& res) {
    try {
      auto body = json::parse(req.body);
      uint64_t target_id = body["order_id"].get<uint64_t>();

      // Push the cancellation request into the lock-free pipeline
      bool accepted = lob.cancelOrder(target_id);

      if (accepted) {
        res.status = 200;
        res.set_content("{\"status\": \"cancel_queued\"}\n", "application/json");
      } else {
        res.status = 429;
        res.set_content("{\"error\": \"Engine at capacity. Slow down.\"}\n", "application/json");
      }

    } catch (const json::exception& e) {
      res.status = 400;
      res.set_content("{\"error\": \"Invalid JSON payload\"}\n", "application/json");
    }
  });

  // Endpoint 3: Query the Limit Order Book Spread
  svr.Get("/spread", [&lob](const httplib::Request& req, httplib::Response& res) {
    auto [best_bid, best_ask] = lob.getSpread();
    
    json response_json = {
      {"best_bid", best_bid},
      {"best_ask", best_ask}
    };
    res.set_content(response_json.dump() + "\n", "application/json");
  });
    
  svr.set_tcp_nodelay(true);
  svr.new_task_queue = [] { return new httplib::ThreadPool(32); };
  
  std::cout << "HTTP Gateway listening on port 8000..." << std::endl;
  svr.listen("0.0.0.0", 8000);

  return 0;
}