#pragma once
// A cache that forgets. The app's memo caches are keyed by something the UI varies at
// will (an accent cycled on hover, a project's id+version), so an unbounded QHash grows
// for the process life. Keeps the N most recently used, drops the rest (QCache, cost 1 each).
// Header-only, Q_OBJECT-free, main-thread only (no locking).
#include <QCache>

namespace stencil::gui {

  template <typename Key, typename Value>
  class LruCache {
   public:
    explicit LruCache(int capacity) : cache(capacity > 0 ? capacity : 1) {}

    // A hit becomes the most recently used entry. The pointer lives until the next insert.
    const Value* find(const Key& key) { return cache.object(key); }

    // Replaces any value already there, evicting the least recently used past the bound.
    const Value& insert(const Key& key, const Value& value) {
      cache.insert(key, new Value(value));
      return *cache.object(key);
    }

    void remove(const Key& key) { cache.remove(key); }
    void clear() { cache.clear(); }
    int size() const { return static_cast<int>(cache.size()); }
    int capacity() const { return static_cast<int>(cache.maxCost()); }

   private:
    QCache<Key, Value> cache;
  };

}  // namespace stencil::gui
