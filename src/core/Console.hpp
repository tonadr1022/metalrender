#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Config.hpp"

struct ImGuiInputTextCallbackData;

namespace TENG_NAMESPACE {

struct ConsoleSuggestion {
  std::string label;
  std::string detail;
  std::string insert_text;
};

class Console {
 public:
  using CommandHandler = std::function<bool(std::string_view args, std::string* error)>;
  using FallbackHandler = std::function<bool(std::string_view line, std::string* error)>;
  using CompletionProvider =
      std::function<void(std::string_view input, std::vector<ConsoleSuggestion>& out,
                         size_t& replace_start, size_t& replace_len)>;

  void open();
  void close();
  [[nodiscard]] bool is_open() const { return open_; }

  void register_command(const std::string& name, const std::string& description,
                        CommandHandler handler);
  void set_fallback_handler(FallbackHandler handler);
  void set_completion_provider(CompletionProvider provider);

  void clear_input();
  void draw_imgui();

 private:
  struct Command {
    std::string name;
    std::string description;
    CommandHandler handler;
  };

  void refresh_suggestions();
  void apply_suggestion(size_t idx);
  void submit_line(const std::string& line);
  static int input_text_callback(ImGuiInputTextCallbackData* data);

  std::vector<Command> commands_;
  FallbackHandler fallback_handler_;
  CompletionProvider completion_provider_;

  bool open_{false};
  bool focus_next_frame_{false};
  bool cursor_to_end_after_suggestion_{false};
  std::string input_;
  std::string last_error_;

  std::vector<ConsoleSuggestion> suggestions_;
  int selected_suggestion_{-1};
  size_t replace_start_{0};
  size_t replace_len_{0};
  std::string last_completion_input_;
};

}  // namespace TENG_NAMESPACE
