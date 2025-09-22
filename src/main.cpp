#include "App.hpp"
#include "tracy/Tracy.hpp"

int main() {
  ZoneScoped;
  App app;
  app.run();
}
