#pragma once

#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

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
    explicit Entry(auto&&... args) : object(std::forward<decltype(args)>(args)...) {}

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

template <typename T>
void destroy(T data);

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
    context->destroy(handle);
    handle = std::move(std::exchange(other.handle, T{}));
    context = std::exchange(other.context, nullptr);
    return *this;
  }

  ~Holder() { destroy(handle); }

  T handle{};
  ContextT* context{};
};
