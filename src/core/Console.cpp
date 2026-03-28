#include "Console.hpp"

#include <algorithm>
#include <utility>

#include "imgui.h"
#include "imgui_stdlib.h"

namespace TENG_NAMESPACE {

namespace {

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string_view trim(std::string_view sv) {
  while (!sv.empty() && is_space(sv.front())) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && is_space(sv.back())) {
    sv.remove_suffix(1);
  }
  return sv;
}

}  // namespace

void Console::open() {
  open_ = true;
  focus_next_frame_ = true;
}

void Console::close() {
  open_ = false;
  focus_next_frame_ = false;
}

void Console::register_command(const std::string& name, const std::string& description,
                               CommandHandler handler) {
  commands_.push_back(
      Command{.name = name, .description = description, .handler = std::move(handler)});
}

void Console::set_fallback_handler(FallbackHandler handler) {
  fallback_handler_ = std::move(handler);
}

void Console::set_completion_provider(CompletionProvider provider) {
  completion_provider_ = std::move(provider);
  last_completion_input_.clear();
}

void Console::clear_input() {
  input_.clear();
  last_error_.clear();
  suggestions_.clear();
  selected_suggestion_ = -1;
  cursor_to_end_after_suggestion_ = false;
}

void Console::refresh_suggestions() {
  if (!completion_provider_) {
    suggestions_.clear();
    selected_suggestion_ = -1;
    return;
  }
  if (input_ == last_completion_input_) {
    return;
  }
  last_completion_input_ = input_;
  suggestions_.clear();
  replace_start_ = 0;
  replace_len_ = 0;
  completion_provider_(input_, suggestions_, replace_start_, replace_len_);
  if (suggestions_.empty()) {
    selected_suggestion_ = -1;
  } else {
    selected_suggestion_ = 0;
  }
}

void Console::apply_suggestion(size_t idx) {
  if (idx >= suggestions_.size()) {
    return;
  }
  const ConsoleSuggestion& s = suggestions_[idx];
  if (replace_start_ > input_.size()) {
    return;
  }
  size_t end = std::min(input_.size(), replace_start_ + replace_len_);
  input_.replace(replace_start_, end - replace_start_, s.insert_text);
  // If this was the first token and there's no value yet, add a space for convenience.
  bool has_space = input_.find(' ') != std::string::npos;
  bool has_equals = input_.find('=') != std::string::npos;
  if (!has_space && !has_equals) {
    input_.push_back(' ');
  }
  focus_next_frame_ = true;
  cursor_to_end_after_suggestion_ = true;
  last_completion_input_.clear();
}

int Console::input_text_callback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways) {
    return 0;
  }
  auto* self = static_cast<Console*>(data->UserData);
  if (self->cursor_to_end_after_suggestion_) {
    data->ClearSelection();
    self->cursor_to_end_after_suggestion_ = false;
  }
  return 0;
}

void Console::submit_line(const std::string& line) {
  last_error_.clear();
  std::string_view trimmed = trim(line);
  if (trimmed.empty()) {
    return;
  }

  size_t first_space = trimmed.find_first_of(" \t");
  std::string_view cmd = trimmed.substr(0, first_space);
  std::string_view args;
  if (first_space != std::string_view::npos) {
    args = trim(trimmed.substr(first_space + 1));
  }

  for (const auto& command : commands_) {
    if (command.name == cmd) {
      std::string err;
      if (!command.handler(args, &err)) {
        last_error_ = err.empty() ? "Command failed" : err;
      } else {
        clear_input();
      }
      return;
    }
  }

  if (fallback_handler_) {
    std::string err;
    if (!fallback_handler_(trimmed, &err)) {
      last_error_ = err.empty() ? "Invalid command" : err;
    } else {
      clear_input();
    }
  } else {
    last_error_ = "Unknown command";
  }
}

void Console::draw_imgui() {
  if (!open_) {
    return;
  }

  ImGuiViewport* vp = ImGui::GetMainViewport();
  const float padding = 18.0f;
  const float height = 180.0f;
  ImVec2 pos{vp->Pos.x + padding, vp->Pos.y + vp->Size.y - height - padding};
  ImVec2 size{vp->Size.x - padding * 2.0f, height};

  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowBgAlpha(0.85f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavInputs;

  if (ImGui::Begin("Console##RuntimeConsole", nullptr, flags)) {
    if (focus_next_frame_) {
      ImGui::SetKeyboardFocusHere();
      focus_next_frame_ = false;
    }

    const ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_CallbackAlways;
    bool console_focus = ImGui::IsItemFocused();
    ImGui::InputText("##console_input", &input_, input_flags, &Console::input_text_callback, this);
    bool input_focused = ImGui::IsItemFocused();
    bool submit = console_focus || (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                                    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));

    refresh_suggestions();

    if (input_focused) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        close();
        ImGui::End();
        return;
      }

      if (!suggestions_.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
          selected_suggestion_ = (selected_suggestion_ + 1) % (int)suggestions_.size();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
          selected_suggestion_ =
              selected_suggestion_ <= 0 ? (int)suggestions_.size() - 1 : selected_suggestion_ - 1;
        }
      }
    }

    if (submit) {
      bool apply_only = false;
      if (!suggestions_.empty() && selected_suggestion_ >= 0) {
        bool has_space = input_.find(' ') != std::string::npos;
        bool has_equals = input_.find('=') != std::string::npos;
        if (!has_space && !has_equals) {
          const auto& s = suggestions_[selected_suggestion_];
          if (s.insert_text != input_) {
            apply_suggestion(static_cast<size_t>(selected_suggestion_));
            apply_only = true;
          }
        }
      }
      if (!apply_only) {
        submit_line(input_);
      }
    }

    if (!suggestions_.empty()) {
      const float list_height = height - ImGui::GetTextLineHeight() * 2.5f;
      ImGui::BeginChild("##console_suggestions", ImVec2(0, list_height), true);
      for (size_t i = 0; i < suggestions_.size(); i++) {
        const auto& s = suggestions_[i];
        bool selected = (int)i == selected_suggestion_;
        std::string label = s.label;
        if (!s.detail.empty()) {
          label += "  [" + s.detail + "]";
        }
        if (ImGui::Selectable(label.c_str(), selected)) {
          selected_suggestion_ = static_cast<int>(i);
          apply_suggestion(i);
        }
        if (ImGui::IsItemHovered() && !s.detail.empty()) {
          ImGui::SetTooltip("%s", s.detail.c_str());
        }
      }
      ImGui::EndChild();
    }

    if (!last_error_.empty()) {
      ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.4f, 1.0f}, "%s", last_error_.c_str());
    }
  }
  ImGui::End();
}

}  // namespace TENG_NAMESPACE
