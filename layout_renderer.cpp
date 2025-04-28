#include "extern/iomanip.h"
#include "layout_renderer.h"
#include "layout_canvas.h"
#include "main_loop.h"
#include "util.h"
#include "layout_generator.h"
#include "random_layout_id.h"
#include "content_factory.h"

string getColorCode(const string& color, const string& c) {
  auto number = [&] () -> string {
    if (color == "black")
      return "30";
    if (color == "red")
      return "31";
    if (color == "green")
      return "32";
    if (color == "brown")
      return "33";
    if (color == "yellow")
      return "93";
    if (color == "blue")
      return "34";
    if (color == "magenta")
      return "35";
    if (color == "cyan")
      return "36";
    if (color == "white")
      return "37;1";
    if (color == "gray")
      return "90";
    std::cerr << "Unknown color: " << color << "\n";
    return "37";
  }();
  return "\e[" + number + "m" + c + "\e[0m";
}

template <typename Container, typename Fun>
auto chooseBest(const Container& c, Fun f) {
  auto ret = *c.begin();
  int p = f(ret);
  for (auto& elem : c) {
    int np = f(elem);
    if (np < p) {
      p = np;
      ret = elem;
    }
  }
  return ret;
}

void renderAscii(const LayoutCanvas::Map& map1, istream& file) {
  unordered_map<string, string> tokens;
  unordered_map<string, int> priority;
  int cnt = 0;
  while (1) {
    string token, character, color;
    file >> std::quoted(token) >> character >> color;
    if (!file)
      break;
    tokens[token] = getColorCode(color, character);
    priority[token] = cnt++;
  }
  std::cerr << std::endl;
  for (auto y : map1.elems.getBounds().getYRange()) {
    for (auto x : map1.elems.getBounds().getXRange()) {
      auto& elems = map1.elems[x][y];
      if (!elems.empty()) {
        auto glyph = chooseBest(elems, [&](const Token& t) {
          if (priority.count(t))
            return -priority.at(t);
          else
            return 10000;
        });
        if (tokens.count(glyph)) {
          std::cerr << tokens.at(glyph);
          continue;
        }
      }
      std::cerr << " ";
    }
    std::cerr << std::endl;
  }
}

void generateMapLayout(const MainLoop& mainLoop, const string& layoutName, FilePath glyphPath,
    const string& layoutSizeString) {
  auto layoutSizeSplit = split(layoutSizeString, {':'});
  USER_CHECK(layoutSizeSplit.size() == 2) << "Bad layout size " << layoutSizeString;
  auto layoutSize = Vec2(fromString<int>(layoutSizeSplit[0]), fromString<int>(layoutSizeSplit[1]));
  USER_CHECK(layoutSize.x >= 1 && layoutSize.y >= 1) << "Bad layout size " << layoutSize;
  auto glyphFile = ifstream(glyphPath.getPath());
  USER_CHECK(!!glyphFile) << "Failed to open glyph file, check the layout_glyphs flag";
  auto factory = mainLoop.createContentFactory(false);
  USER_CHECK(factory.randomLayouts.count(RandomLayoutId(layoutName.data()))) << "Layout not found: " << layoutName;
  auto generator = factory.randomLayouts.at(RandomLayoutId(layoutName.data()));
  LayoutCanvas::Map map{ Table<vector<Token>>(layoutSize) };
  USER_CHECK(!!generator.make(LayoutCanvas{map.elems.getBounds(), &map}, Random)) << "Generation failed";
  renderAscii(map, glyphFile);
}
