#pragma once
// A cache that forgets. The app's memo caches are keyed by something the UI varies at
// will (an accent cycled on hover, a project's id+version), so an unbounded QHash grows
// for the process life. Keeps the N most recently used, drops the rest.
// Header-only, Q_OBJECT-free, main-thread only (no locking).
#include <QHash>

namespace stencil::gui {

  template <typename Key, typename Value>
  class LruCache {
   public:
    explicit LruCache(int capacity) : cap(capacity > 0 ? capacity : 1) {}

    // A hit becomes the most recently used entry.
    const Value* find(const Key& key) {
      const auto it = map.find(key);
      if (it == map.end()) return nullptr;
      it->used = ++clock;
      return &it->value;
    }

    // Replaces any value already there, then evicts down to the bound.
    const Value& insert(const Key& key, const Value& value) {
      auto it = map.find(key);
      if (it == map.end()) it = map.insert(key, Entry{value, 0});
      else it->value = value;
      it->used = ++clock;
      while (map.size() > cap) evictOldest();
      return map.find(key)->value;
    }

    void remove(const Key& key) { map.remove(key); }
    void clear() { map.clear(); }
    int size() const { return map.size(); }
    int capacity() const { return cap; }

   private:
    struct Entry {
      Value value;
      quint64 used = 0;
    };
    void evictOldest() {
      auto oldest = map.begin();
      for (auto it = map.begin(); it != map.end(); ++it)
        if (it->used < oldest->used) oldest = it;
      map.erase(oldest);
    }

    int cap;
    quint64 clock = 0;
    QHash<Key, Entry> map;
  };

}  // namespace stencil::gui
