#include "FuzzyMatch.hpp"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"

namespace TENG_NAMESPACE {

bool util::fuzzy_match(const char* pattern, const char* str) {
  int score;
  return fts::fuzzy_match(pattern, str, score);
}

}  // namespace TENG_NAMESPACE
