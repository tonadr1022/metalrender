#include <cstdio>

int main(int argc, char** argv) {
  (void)argv;
  if (argc < 2) {
    std::fprintf(stderr, "usage: teng-scene-tool <validate|migrate|cook|dump> ...\n");
    return 1;
  }
  std::fprintf(stderr, "teng-scene-tool: subcommands not implemented yet\n");
  return 1;
}
