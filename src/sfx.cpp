// SPDX-License-Identifier: GPL-3.0-or-later
#include "sfx.hpp"

#include "widget.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace undershell {

namespace {

// The effects each tradition letters a hit with, from the softest kick to the
// hardest: a thud, a hit, an explosion.
struct Tiers {
  std::vector<const char*> soft, mid, strong;
};

const Tiers& tiersOf(SfxLayer::Lang lang) {
  // comics: the classic onomatopoeia
  static const Tiers en{{"THUD", "BUMP", "DUM"}, {"BAM!", "POW!", "WHAM!", "BANG!", "KAPOW!"}, {"BOOM!", "KABOOM!", "BLAM!", "KRAKOOM!"}};
  // manga and anime: ton (a tap), don (a hit), dokaan (an explosion)
  static const Tiers ja{{"トン", "ズン", "ドン"}, {"ドン!", "バン!", "ガン!", "ドッ!"}, {"ドカーン!!", "ドドン!!", "ズドン!", "ドーン!!"}};
  // manhwa: tong, kung (a thud), kwang (a crash), kwagwang, dudung
  static const Tiers ko{{"통", "둥", "쿵"}, {"쾅!", "펑!", "쿵!", "탕!"}, {"콰광!!", "쾅!!", "콰앙!", "두둥!!"}};
  // manhua and donghua: dong (a drum), peng (a bang), hong (a boom), honglong
  static const Tiers zh{{"咚", "噔", "嘭"}, {"砰!", "轰!", "咚!", "啪!"}, {"轰隆!!", "轰!!", "砰砰!", "嘭!!"}};
  switch (lang) {
    case SfxLayer::Japanese: return ja;
    case SfxLayer::Korean: return ko;
    case SfxLayer::Chinese: return zh;
    default: return en;
  }
}

// the faces they are lettered in: a comic face for English, the heaviest
// Noto CJK for the rest (each in its own region's forms)
const char* familyOf(SfxLayer::Lang lang) {
  switch (lang) {
    case SfxLayer::Japanese: return "Noto Sans CJK JP";
    case SfxLayer::Korean: return "Noto Sans CJK KR";
    case SfxLayer::Chinese: return "Noto Sans CJK SC";
    default: return "Bangers";
  }
}

// comic colours: yellow, orange, red, cyan, magenta
constexpr Color kClassic[] = {{1.00F, 0.85F, 0.23F, 1}, {1.00F, 0.62F, 0.11F, 1}, {1.00F, 0.29F, 0.24F, 1},
                              {0.24F, 0.78F, 1.00F, 1}, {1.00F, 0.31F, 0.76F, 1}};

Color scaled(Color c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
Color faded(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }

}  // namespace

const std::vector<const char*>& SfxLayer::words(Lang lang, double strength) {
  const Tiers& t = tiersOf(lang);
  return strength >= 0.8 ? t.strong : strength >= 0.55 ? t.mid : t.soft;
}

std::string SfxLayer::vertical(const std::string& word) {
  std::string out;
  for (size_t i = 0; i < word.size();) {
    // one UTF-8 character
    const auto c = static_cast<unsigned char>(word[i]);
    const size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    std::string ch = word.substr(i, n);
    i += n;
    if (ch == "ー") ch = "｜";  // a long vowel runs down the column
    if (ch == "!") {
      // marks gather into one: "!!" -> "‼"
      if (i < word.size() && word[i] == '!') {
        ++i;
        ch = "‼";
      } else {
        ch = "！";
      }
    }
    if (!out.empty()) out += '\n';
    out += ch;
  }
  return out;
}

// How long a word lives: it holds longer the harder it was hit, then fades.
static double lifeOf(double strength) { return 0.55 + 0.25 * strength; }
constexpr double kFade = 0.22;

float SfxLayer::rnd() {
  m_rng ^= m_rng << 13;
  m_rng ^= m_rng >> 17;
  m_rng ^= m_rng << 5;
  return static_cast<float>(m_rng >> 8) / 16777216.0F;
}

void SfxLayer::configure(const toml::table& t, const NoctaliaState& noct) {
  m_lang = t["sfx_language"].value_or(std::string("mix"));
  m_style = t["sfx_style"].value_or(std::string("comic"));
  m_colors = t["sfx_colors"].value_or(std::string("classic"));
  m_font = t["sfx_font"].value_or(std::string());
  if (m_font == "auto") m_font.clear();  // each language its own face
  m_burst = t["sfx_burst"].value_or(true);
  m_size = std::clamp(t["sfx_size"].value_or(1.0), 0.3, 2.5);
  m_amount = std::clamp(t["sfx_amount"].value_or(0.6), 0.0, 1.0);
  m_theme = noct.color("primary");
  // your own words, comma separated (they replace the language's)
  m_custom.clear();
  const std::string own = t["sfx_words"].value_or(std::string());
  for (size_t from = 0; from <= own.size();) {
    size_t to = own.find(',', from);
    if (to == std::string::npos) to = own.size();
    std::string w = own.substr(from, to - from);
    while (!w.empty() && w.front() == ' ') w.erase(w.begin());
    while (!w.empty() && w.back() == ' ') w.pop_back();
    if (!w.empty()) m_custom.push_back(w);
    from = to + 1;
  }
  m_preview = t["sfx_preview"].value_or(-1.0);
  if (m_preview >= 0) {  // one word, frozen this long after its kick
    Word& w = m_words[0];
    w = Word{};
    w.age = m_preview;
    w.strength = 1;
    w.lang = m_lang == "japanese" ? Japanese : m_lang == "korean" ? Korean : m_lang == "chinese" ? Chinese : English;
    w.text = m_custom.empty() ? words(w.lang, 1)[0] : m_custom[0];
    w.angle = -0.14F;
    w.seed = 3;
  }
}

SfxLayer::Lang SfxLayer::pickLang() {
  if (m_lang == "english") return English;
  if (m_lang == "japanese") return Japanese;
  if (m_lang == "korean") return Korean;
  if (m_lang == "chinese") return Chinese;
  return static_cast<Lang>(std::min(3, static_cast<int>(rnd() * 4)));
}

void SfxLayer::spawn(double strength) {
  size_t slot = 0;  // a free one, else the oldest word
  for (size_t i = 0; i < m_words.size(); ++i) {
    if (m_words[i].age < 0) {
      slot = i;
      break;
    }
    if (m_words[i].age > m_words[slot].age) slot = i;
  }
  Word w;
  w.age = 0;
  w.strength = strength;
  w.lang = pickLang();
  // a word of its strength, not the one just shown
  const auto& list = words(w.lang, strength);
  for (int tries = 0; tries < 4; ++tries) {
    w.text = m_custom.empty() ? list[static_cast<size_t>(rnd() * static_cast<float>(list.size())) % list.size()]
                              : m_custom[static_cast<size_t>(rnd() * static_cast<float>(m_custom.size())) % m_custom.size()];
    if (w.text != m_last) break;
  }
  m_last = w.text;
  // somewhere the words still on screen are not: the farthest of a few tries
  float best = -1;
  for (int tries = 0; tries < 8; ++tries) {
    const float x = 0.2F + 0.6F * rnd(), y = 0.2F + 0.6F * rnd();
    float near = 9;
    for (const Word& o : m_words)
      if (o.age >= 0) near = std::min(near, std::hypot(o.x - x, o.y - y));
    if (near > best) {
      best = near;
      w.x = x;
      w.y = y;
    }
  }
  w.angle = (rnd() < 0.5F ? -1.0F : 1.0F) * (0.06F + 0.24F * rnd());
  w.colour = static_cast<int>(rnd() * 5) % 5;
  w.seed = static_cast<uint32_t>(rnd() * 1e6F);
  // one hit at a time, the way a panel letters it: whatever is still up
  // starts to go as the new word lands
  for (Word& o : m_words)
    if (o.age >= 0) o.age = std::max(o.age, lifeOf(o.strength));
  m_words[slot] = w;
}

void SfxLayer::tick(double dt, bool kicked, double strength) {
  if (m_preview >= 0) return;
  m_since += dt;
  for (Word& w : m_words)
    if (w.age >= 0) {
      w.age += dt;
      if (w.age > lifeOf(w.strength) + kFade) w.age = -1;
    }
  // Amount: every kick at 1, only the hardest toward 0; never closer than
  // 0.16 s, so a roll is one word rather than a pile of them
  if (kicked && strength >= (1 - m_amount) * 0.9 && m_since > 0.16) {
    spawn(strength);
    m_since = 0;
  }
}

bool SfxLayer::alive() const {
  for (const Word& w : m_words)
    if (w.age >= 0) return true;
  return false;
}

void SfxLayer::draw(const DrawContext& ctx, float opacity) {
  if (!ctx.text) return;
  const float W = ctx.w, H = ctx.h;
  Canvas& c = m_canvas;
  c.begin(W, H, ctx.scale, ctx.text);
  c.setTransform(1, 0, 0);
  const bool manga = m_style == "manga";
  // the oldest first, so the newest hit lands on top
  std::array<const Word*, 4> order{};
  for (size_t i = 0; i < m_words.size(); ++i) order[i] = &m_words[i];
  std::sort(order.begin(), order.end(), [](const Word* a, const Word* b) { return a->age > b->age; });
  for (const Word* wp : order) {
    const Word& w = *wp;
    if (w.age < 0) continue;
    const double t = w.age, life = lifeOf(w.strength);
    const bool cjk = w.lang != English && (m_custom.empty() || static_cast<unsigned char>(w.text[0]) >= 0x80);
    // CJK effects run down a tall box
    const std::string text = cjk && H > W * 1.15F ? vertical(w.text) : w.text;

    TextStyle st;
    st.family = m_font.empty() ? familyOf(cjk ? w.lang : English) : m_font;
    st.weight = st.family == std::string("Bangers") ? 400 : 900;
    st.size = std::min(W, H) * 0.30F * static_cast<float>(m_size) * (0.72F + 0.45F * static_cast<float>(w.strength));
    // it has to fit the box even at the top of its pop (x1.25)
    Canvas::Size z = Canvas::measure(text, st);
    const float fit = std::min({1.0F, W * 0.9F / std::max(1.0F, z.w * 1.25F), H * 0.9F / std::max(1.0F, z.h * 1.25F)});
    if (fit < 1) {
      st.size *= fit;
      z = Canvas::measure(text, st);
    }
    const float px = st.size;

    // the slam: in from a third of its size past full in 70 ms, ringing back;
    // a shake that dies in a tenth of a second; at the end it lifts and fades
    float s;
    if (t < 0.07) {
      const float u = static_cast<float>(t / 0.07);
      s = 0.35F + (1.22F - 0.35F) * (1 - (1 - u) * (1 - u));
    } else {
      s = 1 + 0.22F * static_cast<float>(std::exp(-(t - 0.07) / 0.05) * std::cos((t - 0.07) * 40));
    }
    float alpha = 1, lift = 0;
    if (t > life) {  // it shrinks and lifts away (fading alone would show its shadow through it)
      const float f = std::min(1.0F, static_cast<float>((t - life) / kFade));
      alpha = 1 - f * f;
      lift = f * px * 0.3F;
      s *= 1 - 0.4F * f;
    }
    // the burst's reach (its spikes go out to 1.46 of it), kept inside the box,
    // and the word placed so that it and its burst fit
    float brx = z.w * 0.62F + px * 0.25F, bry = z.h * 0.62F + px * 0.25F;
    brx = std::min(brx, W * 0.5F / 1.46F * 0.96F);
    bry = std::min(bry, H * 0.5F / 1.46F * 0.96F);
    const float ex = m_burst ? std::max(z.w * 0.55F, brx * 1.46F) : z.w * 0.6F;
    const float ey = m_burst ? std::max(z.h * 0.55F, bry * 1.46F) : z.h * 0.6F;
    const float shakeA = px * 0.05F * static_cast<float>(w.strength * std::exp(-t / 0.1));
    const float sx = shakeA * static_cast<float>(std::sin(t * 93 + w.seed)), sy = shakeA * static_cast<float>(std::cos(t * 71 + w.seed * 0.7));
    const float cx = std::clamp(w.x * W, std::min(W / 2, ex), std::max(W / 2, W - ex)) + sx;
    const float cy = std::clamp(w.y * H, std::min(H / 2, ey), std::max(H / 2, H - ey)) + sy - lift;
    const float ang = w.angle;
    const float ca = std::cos(ang), sa = std::sin(ang);

    // the burst: a spiky star behind the word, opaque, shrinking away at the
    // end (its triangles overlap their neighbours a little, so no seam shows)
    if (m_burst) {
      const float shrink = t > life ? std::max(0.0F, 1 - static_cast<float>((t - life) / (kFade * 0.6))) : 1.0F;
      const float rx = brx * s * shrink, ry = bry * s * shrink;
      if (rx > 2) {
        const int spikes = 14;
        uint32_t r = w.seed * 2654435761U + 1;
        auto jitter = [&r] {
          r ^= r << 13;
          r ^= r >> 17;
          r ^= r << 5;
          return static_cast<float>(r >> 8) / 16777216.0F;
        };
        std::vector<std::pair<float, float>> pts;  // unit-ellipse radii scale, angle
        for (int k = 0; k < spikes * 2; ++k) {
          const float a = 2 * std::numbers::pi_v<float> * static_cast<float>(k) / (spikes * 2);
          const float rr = k % 2 == 0 ? 1.18F + 0.28F * jitter() : 0.84F + 0.06F * jitter();
          pts.push_back({rr, a});
        }
        auto star = [&](float grow, Color col) {
          const float eps = 0.02F;  // the overlap, radians
          for (size_t k = 0; k < pts.size(); ++k) {
            const auto [r0, a0] = pts[k];
            const auto [r1, a1raw] = pts[(k + 1) % pts.size()];
            const float a1 = k + 1 == pts.size() ? a1raw + 2 * std::numbers::pi_v<float> : a1raw;
            auto at = [&](float rr, float a) {
              const float lx = std::cos(a) * rx * rr * grow, ly = std::sin(a) * ry * rr * grow;
              return std::pair<float, float>{cx + ca * lx - sa * ly, cy + sa * lx + ca * ly};
            };
            const auto p0 = at(r0, a0 - eps), p1 = at(r1, a1 + eps);
            // its apex pushed back past the centre, so near the centre too
            // each triangle runs under its neighbours' edges
            const float mx = (p0.first + p1.first) / 2 - cx, my = (p0.second + p1.second) / 2 - cy;
            const float ml = std::max(1e-3F, std::hypot(mx, my));
            c.triangle(cx - mx / ml * 4, cy - my / ml * 4, p0.first, p0.second, p1.first, p1.second, faded(col, opacity));
          }
        };
        star(1 + px * 0.10F / std::max(rx, ry), Color{0, 0, 0, 1});
        star(1, manga ? Color{1, 1, 1, 1} : Color{1.0F, 0.97F, 0.86F, 1});
      }
    }

    // the word: a solid shadow (comics), then its ink outline, then its fill
    const Color fill = manga ? Color{1, 1, 1, 1} : m_colors == "theme" ? m_theme : kClassic[w.colour];
    TextStyle outline = st;
    outline.stroke = px * 0.11F;
    const float a = alpha * opacity;
    if (!manga) {
      const float off = px * 0.07F;
      const Color shadow = m_colors == "theme" ? scaled(m_theme, 0.35F) : Color{0.08F, 0.05F, 0.12F, 1};
      c.textAt(text, outline, cx + off, cy + off, shadow, a * a * a, s, ang);
      c.textAt(text, st, cx + off, cy + off, shadow, a * a * a, s, ang);
    }
    c.textAt(text, outline, cx, cy, Color{0, 0, 0, 1}, a, s, ang);
    c.textAt(text, st, cx, cy, fill, a, s, ang);
  }
}

}  // namespace undershell
