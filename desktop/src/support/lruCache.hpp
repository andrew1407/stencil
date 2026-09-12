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
    explicit LruCache(int capacity) : cap_(capacity > 0 ? capacity : 1) {}

    // A hit becomes the most recently used entry.
    const Value* find(const Key& key) {
      const auto it = map_.find(key);
      if (it == map_.end()) return nullptr;
      it->used = ++clock_;
      return &it->value;
    }

    // Replaces any value already there, then evicts down to the bound.
    const Value& insert(const Key& key, const Value& value) {
      auto it = map_.find(key);
      if (it == map_.end()) it = map_.insert(key, Entry{value, 0});
      else it->value = value;
      it->used = ++clock_;
      while (map_.size() > cap_) evictOldest();
      return map_.find(key)->value;
    }

    void remove(const Key& key) { map_.remove(key); }
    void clear() { map_.clear(); }
    int size() const { return map_.size(); }
    int capacity() const { return cap_; }

   private:
    struct Entry {
      Value value;
      quint64 used = 0;
    };
    void evictOldest() {
      auto oldest = map_.begin();
      for (auto it = map_.begin(); it != map_.end(); ++it)
        if (it->used < oldest->used) oldest = it;
      map_.erase(oldest);
    }

    int cap_;
    quint64 clock_ = 0;
    QHash<Key, Entry> map_;
  };

}  // namespace stencil::gui
