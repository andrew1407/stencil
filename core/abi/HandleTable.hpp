#pragma once
#include <cstddef>
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
    template <class... Args>
    int create(Args&&... args) {
      const int id = next++;
      items.emplace(id, std::make_unique<T>(std::forward<Args>(args)...));
      return id;
    }

    T* get(int id) {
      const auto it = items.find(id);
      return it == items.end() ? nullptr : it->second.get();
    }

    void destroy(int id) { items.erase(id); }
    std::size_t size() const { return items.size(); }

   private:
    std::unordered_map<int, std::unique_ptr<T>> items;
    int next = 1;
  };

}
