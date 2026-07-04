math.randomseed(os.time())

local active_orders = {}
local order_id_counter = 1000000

-- This function is called for every single request wrk generates
request = function()
    -- 20% chance to generate a CANCEL request (if we have active orders to cancel)
    local is_cancel = math.random() < 0.20
    local has_active_orders = #active_orders > 0

    if is_cancel and has_active_orders then
        -- 1. Pick a random active order to cancel
        local idx = math.random(1, #active_orders)
        local target_id = active_orders[idx]

        -- 2. Remove it from our Lua tracker using O(1) swap-and-pop
        active_orders[idx] = active_orders[#active_orders]
        table.remove(active_orders)

        -- 3. Construct the DELETE request
        local method = "DELETE"
        local path = "/order"
        local headers = { ["Content-Type"] = "application/json" }
        local body = string.format('{"order_id": %d}', target_id)

        return wrk.format(method, path, headers, body)
    else
        -- 1. Generate a new order ID
        local id = order_id_counter
        order_id_counter = order_id_counter + 1

        -- 2. Add it to our active orders tracker (Capped at 50,000 to prevent Lua from running out of RAM)
        if #active_orders < 50000 then
            table.insert(active_orders, id)
        end

        -- 3. Generate random market parameters
        local price = math.random(95000, 100000) / 100.0
        local quantity = math.random(1, 20)
        local is_bid = tostring(math.random() > 0.5)

        -- 4. Construct the POST request
        local method = "POST"
        local path = "/order"
        local headers = { ["Content-Type"] = "application/json" }
        local body = string.format('{"order_id": %d, "price": %.2f, "quantity": %d, "is_bid": %s}', id, price, quantity, is_bid)

        return wrk.format(method, path, headers, body)
    end
end
