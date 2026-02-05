#include <tracy/Tracy.hpp>

#include "App.hpp"

int main() {
  ZoneScoped;
  App app;
  app.run();
}
