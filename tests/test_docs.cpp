// docs/OPTIONS.md must match the schema: regenerate with
//   build/undershell --doc > docs/OPTIONS.md
#include "check.hpp"
#include "common.hpp"
#include "docgen.hpp"
#include "schema.hpp"

using namespace undershell;

int main() {
  const std::string committed = readFile(std::string(US_SOURCE_DIR) + "/docs/OPTIONS.md");
  const std::string generated = optionsMarkdown();
  if (committed != generated) std::fputs("docs/OPTIONS.md is stale: run `build/undershell --doc > docs/OPTIONS.md`\n", stderr);
  CHECK(committed == generated);
  // every option has both labels and a sane range
  for (const char* t : {"visualizer", "clock", "now_playing"})
    for (const auto& p : schemaFor(t)) {
      CHECK(!p.labelEn.empty() && !p.labelEs.empty());
      if (p.kind == PropSpec::Number) CHECK(p.min < p.max && p.step > 0 && p.def >= p.min && p.def <= p.max);
      if (p.kind == PropSpec::Enum) {
        bool found = false;
        for (auto& o : p.options) found = found || o == p.defText;
        CHECK(found);  // the default is one of the options
      }
    }
  return TEST_RESULT();
}
