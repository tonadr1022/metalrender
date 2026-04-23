#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "TestApp.hpp"

namespace {

void usage(const char* argv0) {
  std::cout << "usage: " << argv0 << " [--quit-after-frames <n>]\n"
            << "  --quit-after-frames  Exit after completing n frames (n >= 1)\n"
            << "  -h, --help            Show this help\n";
}

bool parse_u32(std::string_view s, std::uint32_t& out) {
  if (s.empty()) return false;
  const char* const begin = s.data();
  const char* const endp = s.data() + s.size();
  std::uint32_t v = 0;
  const auto r = std::from_chars(begin, endp, v, 10);
  if (r.ptr == begin || r.ptr != endp || r.ec != std::errc() || v == 0) {
    return false;
  }
  out = v;
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  TestAppOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--quit-after-frames" && i + 1 < argc) {
      std::uint32_t n = 0;
      if (!parse_u32(std::string_view(argv[++i]), n)) {
        std::cerr << argv[0] << ": --quit-after-frames requires a positive 32-bit integer value\n";
        return 1;
      }
      options.quit_after_frames = n;
    } else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      std::cerr << argv[0] << ": unknown option: " << a << '\n';
      usage(argv[0]);
      return 1;
    }
  }

  TestApp app(options);
  app.run();
  return 0;
}
