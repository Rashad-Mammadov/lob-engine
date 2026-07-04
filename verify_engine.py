import requests
import time
import random

BASE_URL = "http://localhost:8000"

def get_spread():
    response = requests.get(f"{BASE_URL}/spread")
    data = response.json()
    print(f"[SPREAD] Best Bid: ${data['best_bid']:.2f} | Best Ask: ${data['best_ask']:.2f}\n")

def send_order(price, qty, is_bid, order_id=None):
    side = "BUY" if is_bid else "SELL"
    # Use provided ID, or generate a random one
    oid = order_id if order_id is not None else random.randint(1000, 9999)
    
    print(f"Sending Order [{oid}]: {side} {qty} @ ${price:.2f}")
    
    payload = {
        "order_id": oid,
        "price": float(price),
        "quantity": int(qty),
        "is_bid": bool(is_bid)
    }
    requests.post(f"{BASE_URL}/order", json=payload)
    time.sleep(0.1) # Brief pause to let the background thread process it
    return oid

def cancel_order(order_id):
    print(f"Canceling Order [{order_id}]...")
    payload = {"order_id": int(order_id)}
    requests.delete(f"{BASE_URL}/order", json=payload)
    time.sleep(0.1)

# --- THE TEST SCENARIO ---

print("--- INITIAL STATE ---")
get_spread()

print("--- 1. BUILDING THE BOOK ---")
send_order(price=100.00, qty=10, is_bid=True)
get_spread()

send_order(price=105.00, qty=10, is_bid=False)
get_spread()

print("--- 2. TESTING PRICE PRIORITY ---")
send_order(price=101.00, qty=5, is_bid=True)
get_spread()

send_order(price=99.00, qty=5, is_bid=True)
get_spread()

print("--- 3. TESTING EXECUTION (CROSSING THE SPREAD) ---")
send_order(price=101.00, qty=5, is_bid=False)   
get_spread()

print("--- 4. TESTING PARTIAL FILLS ---")
send_order(price=105.00, qty=15, is_bid=True)
get_spread()

print("--- 5. TESTING O(1) CANCELLATION ---")
# Currently, the Best Bid is $105.00 (from the leftovers of Phase 4).
# Let's add a massive bid to jump to the top of the book.
target_id = send_order(price=110.00, qty=50, is_bid=True, order_id=7777)
get_spread()

# Now, we cancel that exact order. 
# The spread should instantly fall back to $105.00.
cancel_order(target_id)
get_spread()
