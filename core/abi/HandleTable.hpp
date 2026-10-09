#pragma once
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

// Handle table for the core's STATEFUL classes crossing an extern "C" ABI.
// The host holds an opaque int, never a pointer: a stale or forged handle looks
// up to nullptr and is rejected, instead of being dereferenced.
namespace stencil::core::abi {

  template <class T>
  class HandleTable {
   public:
    // `issued` is how many ids came before; a test starts near the wrap with it.
    explicit HandleTable(std::uint64_t issued = 0) : next(issued) {}

    template <class... Args>
    int create(Args&&... args) {
      // Ids stay in [1, INT_MAX]: past it they wrap, skipping any still held.
      int id = 0;
      do {
        id = static_cast<int>(next++ % INT_MAX) + 1;
      } while (items.count(id) != 0);
      items.emplace(id, std::make_unique<T>(std::forward<Args>(args)...));
      return id;
    }

    T* get(int id) {
      if (id <= 0) return nullptr;
      const auto it = items.find(id);
      return it == items.end() ? nullptr : it->second.get();
    }

    void destroy(int id) { items.erase(id); }
    std::size_t size() const { return items.size(); }

   private:
    std::unordered_map<int, std::unique_ptr<T>> items;
    std::uint64_t next;
  };

}
