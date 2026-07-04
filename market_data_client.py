import asyncio
import websockets
import json

async def listen_to_feed():
    uri = "ws://localhost:8080"
    
    print(f"[*] Connecting to High-Frequency Data Plane at {uri}...")
    
    try:
        async with websockets.connect(uri) as websocket:
            print("[*] Connection established. Listening for executions...\n")
            print("-" * 60)
            
            # Infinite non-blocking loop waiting for C++ broadcasts
            while True:
                message = await websocket.recv()
                trade = json.loads(message)
                
                price = trade['price']
                qty = trade['quantity']
                maker = trade['maker_order_id']
                taker = trade['taker_order_id']
                
                print(f"[TRADE MATCH] {qty} units @ ${price:.2f} | Maker: {maker} <-> Taker: {taker}")
                
    except websockets.exceptions.ConnectionClosed:
        print("\n[*] Connection completely closed by the exchange.")
    except ConnectionRefusedError:
        print("\n[!] Connection refused. Is the C++ Docker engine running on port 8080?")

if __name__ == "__main__":
    try:
        asyncio.run(listen_to_feed())
    except KeyboardInterrupt:
        print("\n[*] Disconnected from Market Data Feed.")
