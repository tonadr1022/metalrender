#pragma once

#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "EAssert.hpp"

template <typename, typename>
struct Pool;

// ObjectT should be default constructible and have sane default constructed state
template <typename HandleT, typename ObjectT>
struct Pool {
  static_assert(std::is_default_constructible_v<ObjectT>, "ObjectT must be default constructible");

  using IndexT = uint32_t;
  Pool() { entries_.reserve(20); }
  Pool& operator=(Pool&& other) = delete;
  Pool& operator=(const Pool& other) = delete;
  Pool(const Pool& other) = delete;
  Pool(Pool&& other) = delete;

  explicit Pool(IndexT size) : entries_(size) {}

  void clear() {
    entries_.clear();
    size_ = 0;
  }

  struct Entry {
    template <class... Args>
    explicit Entry(Args&&... args) : object(std::forward<Args>(args)...) {}

    ObjectT object{};
    uint32_t gen_{1};
    bool live_{false};
  };

  template <typename... Args>
  HandleT alloc(Args&&... args) {
    std::unique_lock lock(mtx_);
    HandleT handle;
    if (!free_list_.empty()) {
      handle.idx_ = free_list_.back();
      free_list_.pop_back();
      ::new (std::addressof(entries_[handle.idx_].object)) ObjectT{std::forward<Args>(args)...};
    } else {
      handle.idx_ = entries_.size();
      entries_.emplace_back(std::forward<Args>(args)...);
    }
    handle.gen_ = entries_[handle.idx_].gen_;
    num_created_++;
    size_++;
    entries_[handle.idx_].live_ = true;
    return handle;
  }

  [[nodiscard]] IndexT size() const { return size_; }
  [[nodiscard]] bool empty() const { return size() == 0; }
  [[nodiscard]] size_t get_num_created() const { return num_created_; }
  [[nodiscard]] size_t get_num_destroyed() const { return num_destroyed_; }

  void destroy(HandleT handle) {
    std::unique_lock lock(mtx_);
    if (handle.idx_ >= entries_.size()) {
      return;
    }
    if (entries_[handle.idx_].gen_ != handle.gen_) {
      return;
    }
    entries_[handle.idx_].gen_++;
    entries_[handle.idx_].object = {};
    entries_.back().live_ = false;
    free_list_.emplace_back(handle.idx_);
    size_--;
    num_destroyed_++;
  }

  ObjectT* get(HandleT handle) {
    std::shared_lock lock(mtx_);
    if (!handle.gen_) return nullptr;
    if (handle.idx_ >= entries_.size()) {
      return nullptr;
    }
    if (entries_[handle.idx_].gen_ != handle.gen_) {
      return nullptr;
    }
    return &entries_[handle.idx_].object;
  }
  std::vector<Entry>& get_entries() { return entries_; }

 private:
  std::shared_mutex mtx_;
  std::vector<IndexT> free_list_;
  std::vector<Entry> entries_;
  IndexT size_{};
  size_t num_created_{};
  size_t num_destroyed_{};
};

template <typename HandleT, typename ObjectT>
struct BlockPool {
  static_assert(std::is_default_constructible_v<ObjectT>, "ObjectT must be default constructible");

  explicit BlockPool(bool do_destroy) : element_count_per_block_(64), do_destroy_(do_destroy) {
    init(1);
  }

  BlockPool(size_t element_count_per_block, size_t initial_blocks, bool do_destroy)
      : element_count_per_block_(element_count_per_block), do_destroy_(do_destroy) {
    init(initial_blocks);
  }

  BlockPool& operator=(BlockPool&& other) = delete;
  BlockPool& operator=(const BlockPool& other) = delete;
  BlockPool(const BlockPool& other) = delete;
  BlockPool(BlockPool&& other) = delete;

  void iterate_entries(auto&& f) {
    std::unique_lock lock(mtx_);
    for (size_t i = 0; i < num_blocks(); i++) {
      for (const auto& entry : get_block_entries(i)) {
        if (entry.live_) {
          f(entry.object);
        }
      }
    }
  }

  void clear() {
    std::unique_lock lock(mtx_);
    all_entries_.clear();
    free_list_.clear();
    size_ = 0;
    num_created_ = 0;
    num_destroyed_ = 0;
  }

  struct Entry {
    template <class... Args>
    explicit Entry(Args&&... args) : object(std::forward<Args>(args)...) {}

    ObjectT object{};
    uint32_t gen_{1};
    bool live_{false};
  };

  struct EntryKey {
    uint16_t block;
    uint16_t idx;
  };

  constexpr static EntryKey get_entry_key(uint32_t handle_idx) {
    return EntryKey{.block = static_cast<uint16_t>(handle_idx >> 16),
                    .idx = static_cast<uint16_t>(handle_idx & 0xFFFFu)};
  }

  template <typename... Args>
  HandleT alloc(Args&&... args) {
    std::unique_lock lock(mtx_);
    HandleT handle;
    if (free_list_.empty()) {
      add_block();
      ASSERT(!free_list_.empty());
    }

    EntryKey entry_key = free_list_.back();
    free_list_.pop_back();
    auto combined_key =
        static_cast<uint32_t>(entry_key.block << 16) | static_cast<uint32_t>(entry_key.idx);
    handle.idx_ = combined_key;
    ::new (std::addressof(all_entries_[entry_key.block][entry_key.idx].object))
        ObjectT{std::forward<Args>(args)...};

    handle.gen_ = all_entries_[entry_key.block][entry_key.idx].gen_;
    num_created_++;
    size_++;
    all_entries_[entry_key.block][entry_key.idx].live_ = true;
    return handle;
  }

  [[nodiscard]] size_t size() const { return size_; }
  [[nodiscard]] bool empty() const { return size() == 0; }
  [[nodiscard]] size_t get_num_created() const { return num_created_; }
  [[nodiscard]] size_t get_num_destroyed() const { return num_destroyed_; }

  void destroy(HandleT handle) {
    std::unique_lock lock(mtx_);
    const EntryKey entry_key = get_entry_key(handle.idx_);
    if (!entry_key_valid(entry_key)) {
      return;
    }
    Entry& entry = get_entry(entry_key);
    if (entry.gen_ != handle.gen_) {
      return;
    }
    entry.gen_++;
    if (do_destroy_) {
      if (do_destroy_) entry.object = {};
    }
    entry.live_ = false;
    free_list_.emplace_back(entry_key);
    size_--;
    num_destroyed_++;
  }

  ObjectT* get(HandleT handle) {
    std::shared_lock lock(mtx_);
    if (!handle.gen_) return nullptr;
    const EntryKey entry_key = get_entry_key(handle.idx_);
    if (!entry_key_valid(entry_key)) {
      return nullptr;
    }
    if (get_entry(entry_key).gen_ != handle.gen_) {
      return nullptr;
    }
    return &all_entries_[entry_key.block][entry_key.idx].object;
  }

  const ObjectT* get(HandleT handle) const {
    std::shared_lock lock(mtx_);
    if (!handle.gen_) return nullptr;
    const EntryKey entry_key = get_entry_key(handle.idx_);
    if (!entry_key_valid(entry_key)) {
      return nullptr;
    }
    if (get_entry(entry_key).gen_ != handle.gen_) {
      return nullptr;
    }
    return &all_entries_[entry_key.block][entry_key.idx].object;
  }

  [[nodiscard]] size_t num_blocks() const { return all_entries_.size(); }
  std::vector<Entry>& get_block_entries(uint32_t block) {
    ASSERT(block < all_entries_.size());
    return all_entries_[block];
  }

 private:
  mutable std::shared_mutex mtx_;
  std::vector<EntryKey> free_list_;
  std::vector<std::vector<Entry>> all_entries_;
  size_t size_{};
  size_t num_created_{};
  size_t num_destroyed_{};
  uint16_t element_count_per_block_{};
  bool do_destroy_{};

  Entry& get_entry(EntryKey key) {
    ASSERT(key.block < all_entries_.size());
    ASSERT(key.idx < all_entries_[key.block].size());
    return all_entries_[key.block][key.idx];
  }
  const Entry& get_entry(EntryKey key) const {
    ASSERT(key.block < all_entries_.size());
    ASSERT(key.idx < all_entries_[key.block].size());
    return all_entries_[key.block][key.idx];
  }
  bool entry_key_valid(EntryKey key) const {
    return key.block < all_entries_.size() && key.idx < element_count_per_block_;
  }

  void init(uint16_t initial_blocks) {
    free_list_.reserve(element_count_per_block_ * initial_blocks);
    all_entries_.reserve(initial_blocks);
    for (uint16_t i = 0; i < initial_blocks; i++) {
      auto& b = all_entries_.emplace_back();
      b.resize(element_count_per_block_);
      for (uint16_t j = 0; j < element_count_per_block_; j++) {
        free_list_.emplace_back(EntryKey{.block = i, .idx = j});
      }
    }
  }

  void add_block() {
    if (all_entries_.size() >= std::numeric_limits<uint16_t>::max()) {
      throw std::runtime_error("max number of blocks reached");
    }
    auto block = static_cast<uint16_t>(all_entries_.size());
    for (uint16_t i = 0; i < element_count_per_block_; i++) {
      free_list_.emplace_back(EntryKey{.block = block, .idx = i});
    }
    all_entries_.emplace_back(std::vector<Entry>(element_count_per_block_));
  }
};

template <typename T, typename ContextT>
struct Holder {
  Holder() = default;
  Holder(T&& data, ContextT* context) : handle(std::move(data)), context(context) {}

  Holder(const Holder& other) = delete;
  Holder& operator=(const Holder& other) = delete;

  Holder(Holder&& other) noexcept
      : handle(std::exchange(other.handle, T{})), context(std::exchange(other.context, nullptr)) {}

  Holder& operator=(Holder&& other) noexcept {
    if (&other == this) {
      return *this;
    }
    destroy();
    handle = std::move(std::exchange(other.handle, T{}));
    context = std::exchange(other.context, nullptr);
    return *this;
  }

  [[nodiscard]] bool is_valid() const { return handle.is_valid(); }

  ~Holder() { destroy(); }

  T handle{};
  ContextT* context{};

 private:
  void destroy() {
    if (context) {
      context->destroy(handle);
    }
  }
};
