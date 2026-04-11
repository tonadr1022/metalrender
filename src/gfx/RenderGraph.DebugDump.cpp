#include "RenderGraph.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/Logger.hpp"
#include "gfx/RenderGraph.Format.hpp"
#include "gfx/renderer/RendererCVars.hpp"

namespace TENG_NAMESPACE {
namespace gfx {

RenderGraph::DebugDumpOnceRequestScope::~DebugDumpOnceRequestScope() {
  if (rg != nullptr) {
    rg->debug_dump_once_requested_ = false;
  }
}

namespace {

void write_json_str(std::ostream& o, std::string_view s) {
  o << '"';
  for (unsigned char uc : s) {
    const char c = static_cast<char>(uc);
    switch (c) {
      case '"':
        o << "\\\"";
        break;
      case '\\':
        o << "\\\\";
        break;
      case '\b':
        o << "\\b";
        break;
      case '\f':
        o << "\\f";
        break;
      case '\n':
        o << "\\n";
        break;
      case '\r':
        o << "\\r";
        break;
      case '\t':
        o << "\\t";
        break;
      default:
        if (uc < 0x20u) {
          o << std::format("\\u{:04x}", static_cast<unsigned>(uc));
        } else {
          o << c;
        }
        break;
    }
  }
  o << '"';
}

std::string sanitize_one_line(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\n' || c == '\r') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

void write_dot_label_text(std::ostream& o, std::string_view s) {
  for (char c : s) {
    if (c == '\\' || c == '"') {
      o << '\\';
    }
    o << c;
  }
}

std::string join_sorted_unique(const std::set<std::string>& names, size_t max_chars) {
  std::string out;
  for (const auto& n : names) {
    if (!out.empty()) {
      out += ", ";
    }
    if (out.size() + n.size() > max_chars) {
      out += "…";
      break;
    }
    out += n;
  }
  return out;
}

}  // namespace

void RenderGraph::bake_write_debug_dump_if_requested_(glm::uvec2 fb_size) {
  const int mode = renderer_cv::developer_render_graph_dump_mode.get();
  if (mode == 0) {
    return;
  }
  if (mode == 3 && !debug_dump_once_requested_) {
    return;
  }
  DebugDumpOnceRequestScope clear_mode3_request{mode == 3 ? this : nullptr};

  const char* dir_c = renderer_cv::developer_render_graph_dump_dir.get();
  const std::string dir = dir_c ? std::string(dir_c) : std::string();
  if (dir.empty()) {
    LWARN(
        "renderer.developer.render_graph_dump_mode is non-zero but "
        "renderer.developer.render_graph_dump_dir is empty; skipping render graph dump.");
    return;
  }

  static uint64_t dump_seq = 0;
  const uint64_t dump_index = dump_seq++;

  const bool want_json = (mode == 1 || mode == 3);
  const bool want_dot = (mode == 2 || mode == 3);

  std::filesystem::path base_dir(dir);
  std::error_code ec;
  std::filesystem::create_directories(base_dir, ec);
  if (ec) {
    LWARN("RenderGraph dump: could not create directory '{}': {}", dir, ec.message());
    return;
  }
  if (mode == 3) {
    LINFO("Render graph on-demand dump (JSON+DOT) under '{}'", dir);
  }

  const auto barrier_key = [](const BarrierInfo& b) {
    return std::format("{}:{}:m{}:s{}", to_string(b.resource.type), b.resource.idx,
                       b.subresource_mip, b.subresource_slice);
  };

  std::map<std::pair<uint32_t, uint32_t>, std::set<std::string>> edge_labels;
  auto record_read_dep = [&](uint32_t consumer_pass_i, const Pass::NameAndAccess& u) {
    auto it = resource_use_id_to_writer_pass_idx_.find(u.id);
    if (it == resource_use_id_to_writer_pass_idx_.end()) {
      return;
    }
    const uint32_t producer_pass_i = it->second;
    edge_labels[{producer_pass_i, consumer_pass_i}].insert(debug_name(u.id));
  };
  for (uint32_t c = 0; c < passes_.size(); ++c) {
    for (const auto& u : passes_[c].get_internal_reads()) {
      record_read_dep(c, u);
    }
    for (const auto& u : passes_[c].get_external_reads()) {
      record_read_dep(c, u);
    }
  }

  std::vector<int32_t> exec_order_by_pass(passes_.size(), -1);
  {
    uint32_t ord = 0;
    for (uint32_t pass_i : pass_stack_) {
      if (pass_i < exec_order_by_pass.size()) {
        exec_order_by_pass[pass_i] = static_cast<int32_t>(ord);
      }
      ++ord;
    }
  }

  std::map<std::string, uint32_t> barrier_counts_by_resource;
  size_t total_barrier_records = 0;
  for (uint32_t pass_i : pass_stack_) {
    for (const auto& b : pass_barrier_infos_[pass_i]) {
      ++barrier_counts_by_resource[barrier_key(b)];
      ++total_barrier_records;
    }
  }

  if (want_json) {
    const auto json_path = base_dir / std::format("rg_dump_{}.json", dump_index);
    std::ofstream jf(json_path, std::ios::out | std::ios::trunc);
    if (!jf) {
      LWARN("RenderGraph dump: could not open JSON file '{}'", json_path.string());
    } else {
      jf << std::format("{{\n  \"schema\": \"teng.render_graph_dump/v1\",\n");
      jf << std::format("  \"dump_index\": {},\n", dump_index);
      jf << std::format("  \"fb_size\": {{ \"w\": {}, \"h\": {} }},\n", fb_size.x, fb_size.y);
      jf << "  \"notes\": { \"multi_queue\": null },\n";

      jf << "  \"passes\": [\n";
      for (uint32_t pass_i = 0; pass_i < passes_.size(); ++pass_i) {
        const auto& pass = passes_[pass_i];
        if (pass_i > 0) {
          jf << ",\n";
        }
        jf << "    {\n";
        jf << std::format("      \"pass_i\": {},\n", pass_i);
        jf << "      \"name\": ";
        write_json_str(jf, pass.get_name());
        jf << ",\n";
        jf << "      \"type\": ";
        write_json_str(jf, rg_fmt::to_string(pass.type()));
        jf << ",\n";
        jf << std::format("      \"exec_order\": {}", exec_order_by_pass[pass_i]);
        jf << ",\n";
        jf << std::format("      \"barrier_count_at_pass\": {}",
                          pass_barrier_infos_[pass_i].size());
        jf << "\n    }";
      }
      jf << "\n  ],\n";

      jf << "  \"execution_order\": [\n";
      {
        bool first = true;
        for (uint32_t pass_i : pass_stack_) {
          if (!first) {
            jf << ",\n";
          }
          first = false;
          jf << std::format(R"rg(    {{ "exec_order": {}, "pass_i": {} }})rg",
                            exec_order_by_pass[pass_i], pass_i);
        }
        jf << "\n  ],\n";
      }

      jf << "  \"dependency_edges\": [\n";
      {
        bool first_edge = true;
        for (uint32_t c = 0; c < passes_.size(); ++c) {
          if (c >= pass_dependencies_.size()) {
            continue;
          }
          for (uint32_t p : pass_dependencies_[c]) {
            if (!first_edge) {
              jf << ",\n";
            }
            first_edge = false;
            jf << "    {\n";
            jf << std::format("      \"from_pass_i\": {},\n", p);
            jf << std::format("      \"to_pass_i\": {},\n", c);
            jf << "      \"kind\": \"producer_to_consumer\",\n";
            jf << "      \"resources\": [\n";
            const auto it_lab = edge_labels.find({p, c});
            if (it_lab != edge_labels.end()) {
              bool fr = true;
              for (const auto& nm : it_lab->second) {
                if (!fr) {
                  jf << ",\n";
                }
                fr = false;
                jf << "        ";
                write_json_str(jf, nm);
              }
            }
            jf << "\n      ]\n    }";
          }
        }
        jf << "\n  ],\n";
      }

      jf << "  \"barriers_by_pass\": [\n";
      {
        bool first_bp = true;
        uint32_t exec_ord = 0;
        for (uint32_t pass_i : pass_stack_) {
          if (!first_bp) {
            jf << ",\n";
          }
          first_bp = false;
          const auto& pass = passes_[pass_i];
          const auto& barriers = pass_barrier_infos_[pass_i];
          jf << "    {\n";
          jf << std::format("      \"exec_order\": {},\n", exec_ord);
          jf << std::format("      \"pass_i\": {},\n", pass_i);
          jf << "      \"name\": ";
          write_json_str(jf, pass.get_name());
          jf << ",\n";
          jf << std::format("      \"barrier_count\": {},\n", barriers.size());
          jf << "      \"items\": [\n";
          for (size_t bi = 0; bi < barriers.size(); ++bi) {
            const auto& barrier = barriers[bi];
            if (bi > 0) {
              jf << ",\n";
            }
            jf << "        {\n";
            jf << "          \"resource_key\": ";
            write_json_str(jf, barrier_key(barrier));
            jf << ",\n";
            jf << R"rg(          "physical": { "type": )rg";
            write_json_str(jf, to_string(barrier.resource.type));
            jf << R"rg(, "idx": )rg" << barrier.resource.idx << R"rg( },
)rg";
            jf << "          \"debug_name\": ";
            write_json_str(jf, barrier.debug_id.is_valid() ? debug_name(barrier.debug_id) : "");
            jf << ",\n";
            if (barrier.debug_id.is_valid()) {
              jf << std::format(
                  R"rg(          "debug_id": {{ "idx": {}, "type": "{}", "version": {} }},
)rg",
                  barrier.debug_id.idx, to_string(barrier.debug_id.type), barrier.debug_id.version);
            } else {
              jf << "          \"debug_id\": null,\n";
            }
            jf << std::format("          \"subresource_mip\": {},\n", barrier.subresource_mip);
            jf << std::format("          \"subresource_slice\": {},\n", barrier.subresource_slice);
            jf << std::format("          \"is_swapchain_write\": {},\n",
                              barrier.is_swapchain_write ? "true" : "false");
            jf << "          \"src\": {\n";
            jf << "            \"access\": ";
            write_json_str(jf, rg_fmt::to_string(barrier.src_state.access));
            jf << ",\n";
            jf << "            \"stage\": ";
            write_json_str(jf, rg_fmt::to_string(barrier.src_state.stage));
            jf << ",\n";
            jf << "            \"layout\": ";
            write_json_str(jf, rg_fmt::to_string(barrier.src_state.layout));
            jf << "\n          },\n";
            jf << "          \"dst\": {\n";
            jf << "            \"access\": ";
            write_json_str(jf, rg_fmt::to_string(barrier.dst_state.access));
            jf << ",\n";
            jf << "            \"stage\": ";
            write_json_str(jf, rg_fmt::to_string(barrier.dst_state.stage));
            jf << ",\n";
            jf << "            \"layout\": ";
            write_json_str(jf, rg_fmt::to_string(barrier.dst_state.layout));
            jf << "\n          }\n        }";
          }
          jf << "\n      ]\n    }";
          ++exec_ord;
        }
        jf << "\n  ],\n";
      }

      jf << "  \"barrier_counts_by_resource\": {\n";
      {
        bool first_bc = true;
        for (const auto& [key, cnt] : barrier_counts_by_resource) {
          if (!first_bc) {
            jf << ",\n";
          }
          first_bc = false;
          jf << "    ";
          write_json_str(jf, key);
          jf << ": " << cnt;
        }
        jf << "\n  },\n";

        jf << "  \"summary\": {\n";
        jf << std::format("    \"total_barrier_records\": {},\n", total_barrier_records);
        jf << std::format("    \"passes_declared\": {},\n", passes_.size());
        jf << std::format("    \"passes_executed\": {}\n", pass_stack_.size());
        jf << "  }\n}\n";
      }
    }
  }

  if (want_dot) {
    const auto dot_path = base_dir / std::format("rg_dump_{}.dot", dump_index);
    std::ofstream df(dot_path, std::ios::out | std::ios::trunc);
    if (!df) {
      LWARN("RenderGraph dump: could not open DOT file '{}'", dot_path.string());
    } else {
      df << "digraph RenderGraph {\n";
      df << "  rankdir=LR;\n";
      df << "  node [shape=box, fontname=\"Helvetica\"];\n";
      df << "  edge [fontname=\"Helvetica\", fontsize=10];\n";

      for (uint32_t pass_i = 0; pass_i < passes_.size(); ++pass_i) {
        const auto& pass = passes_[pass_i];
        const int32_t eo = exec_order_by_pass[pass_i];
        df << std::format("  p{} [label=\"", pass_i);
        write_dot_label_text(df, sanitize_one_line(pass.get_name()));
        df << "\\n";
        write_dot_label_text(df, rg_fmt::to_string(pass.type()));
        df << "\\nexec=" << eo;
        df << "\"];\n";
      }

      for (uint32_t c = 0; c < passes_.size(); ++c) {
        if (c >= pass_dependencies_.size()) {
          continue;
        }
        for (uint32_t p : pass_dependencies_[c]) {
          df << std::format("  p{} -> p{}", p, c);
          const auto it_lab = edge_labels.find({p, c});
          if (it_lab != edge_labels.end() && !it_lab->second.empty()) {
            std::string lab = join_sorted_unique(it_lab->second, 220);
            df << " [label=\"";
            write_dot_label_text(df, sanitize_one_line(lab));
            df << "\"]";
          }
          df << ";\n";
        }
      }
      df << "}\n";
    }
  }
}

}  // namespace gfx
}  // namespace TENG_NAMESPACE
