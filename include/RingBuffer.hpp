#pragma once

#include <atomic>
#include <vector>

template<typename T, size_t Capacity>
class RingBuffer {
private:
  struct alignas(64) Slot {
    T data;
    std::atomic<bool> ready{false};
  };

  std::vector<Slot> buffer;
  
  std::atomic<size_t> write_idx{0}; 
  
  std::atomic<size_t> read_idx{0}; 

public:
  RingBuffer() : buffer(Capacity) {}

  // NON-BLOCKING PUSH: Returns false if the queue is full
  bool try_push(const T& item) {
    size_t current_write = write_idx.load(std::memory_order_relaxed);
    size_t current_read = read_idx.load(std::memory_order_acquire);

    // If the buffer is 100% full. Abort.
    if (current_write - current_read >= Capacity) {
      return false; 
    }

    // Capacity exists. Claim the slot.
    size_t idx = write_idx.fetch_add(1, std::memory_order_relaxed);
    Slot& slot = buffer[idx % Capacity];

    // Brief safety spin just in case of a microscopic race condition at the boundary
    while (slot.ready.load(std::memory_order_acquire)) {
        #if defined(__aarch64__) || defined(_M_ARM64)
          asm volatile("yield");
        #else
          asm volatile("pause");
        #endif
    }

    slot.data = item;
    slot.ready.store(true, std::memory_order_release);
    return true;
  }

  // Consumer (Matching Engine) pops orders
  bool pop(T& out_item) {
    size_t current_read = read_idx.load(std::memory_order_relaxed);
    Slot& slot = buffer[current_read % Capacity];

    if (!slot.ready.load(std::memory_order_acquire)) {
      return false; // Queue is empty
    }

    out_item = slot.data;
    slot.ready.store(false, std::memory_order_release);
    
    // ATOMIC INCREMENT: Let the producers know a slot just opened up
    read_idx.fetch_add(1, std::memory_order_release);
    return true;
  }
};
