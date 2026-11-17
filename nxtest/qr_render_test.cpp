// SPDX-License-Identifier: AGPL-3.0
// QR rasterisation test: does the *picture* the login card draws still scan?
//
// nxtest/qrcode_test.cpp proves the matrix is right. That is necessary but not
// sufficient: a correct matrix rendered at the wrong scale, without a quiet
// zone, or with a fractional module size produces a QR that no phone can read.
// This test rasterises through the exact function the UI calls (drawQrCode),
// asserts on the pixels, and leaves PGMs behind for zbar to adjudicate:
//
//   zbarimg -q --raw /tmp/qr_render_a.pgm      # must print "A"
//   zbarimg -q --raw /tmp/qr_render_login.pgm  # must print the URL
//
// The UI sources need host stubs for AudioEngine and svcSleepThread because
// libnx is not available off-device; see nxtest/run.sh for the build line.
#include <cstdio>
#include <string>
#include <vector>

#include "spn/canvas.hpp"
#include "spn/qrcode.hpp"
#include "spn/types.hpp"
#include "spn/ui.hpp"

using namespace spn;

namespace {

int gPass = 0;
int gFail = 0;

void check(const char* what, bool ok, const std::string& detail = std::string()) {
  if (ok) {
    ++gPass;
    std::printf("  ok   %s\n", what);
  } else {
    ++gFail;
    std::printf("  FAIL %s%s%s\n", what, detail.empty() ? "" : "  (",
                detail.empty() ? "" : (detail + ")").c_str());
  }
}

constexpr int kBox = 160;  // size-40, the desktop panel's QR area

// The canvas stores 0xAABBGGRR; lucide-style luminance for the zbar input.
unsigned char luma(uint32_t v) {
  const unsigned r = v & 0xFFu;
  const unsigned g = (v >> 8) & 0xFFu;
  const unsigned b = (v >> 16) & 0xFFu;
  return static_cast<unsigned char>((r * 299u + g * 587u + b * 114u) / 1000u);
}

bool writePgm(const char* path, const std::vector<uint32_t>& px, int w, int h) {
  FILE* fp = std::fopen(path, "wb");
  if (!fp) return false;
  std::fprintf(fp, "P5\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; ++i) {
    const unsigned char l = luma(px[size_t(i)]);
    std::fwrite(&l, 1, 1, fp);
  }
  std::fclose(fp);
  return true;
}

// Renders one payload into a kBox square with a 32 px margin around it.
bool render(const std::string& text, std::vector<uint32_t>& px, int& w, int& h,
            qr::Code& code) {
  if (!qr::encode(text, code)) return false;
  w = kBox + 64;
  h = kBox + 64;
  px.assign(size_t(w) * size_t(h), 0xFFFFFFFFu);
  Canvas c(px.data(), w, h, w);
  drawQrCode(c, code, Rect(32, 32, kBox, kBox));
  return true;
}

}  // namespace

int main() {
  std::printf("qr rasterisation\n");

  // --- 1. a short payload ---------------------------------------------------
  {
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    qr::Code code;
    check("encode A", render("A", px, w, h, code));

    // The margin outside the drawn square must stay white: that is the quiet
    // zone a scanner needs in order to locate the finder patterns.
    bool marginWhite = true;
    for (int y = 0; y < h && marginWhite; ++y) {
      for (int x = 0; x < w; ++x) {
        if (x >= 32 && x < 32 + kBox && y >= 32 && y < 32 + kBox) continue;
        if (luma(px[size_t(y) * size_t(w) + size_t(x)]) < 200) {
          marginWhite = false;
          break;
        }
      }
    }
    check("quiet zone stays white", marginWhite);

    // Both colours must appear, otherwise nothing was drawn.
    size_t dark = 0, light = 0;
    for (int y = 32; y < 32 + kBox; ++y) {
      for (int x = 32; x < 32 + kBox; ++x) {
        if (luma(px[size_t(y) * size_t(w) + size_t(x)]) < 128) ++dark;
        else ++light;
      }
    }
    check("ink present", dark > 0 && light > 0,
          "dark=" + std::to_string(dark) + " light=" + std::to_string(light));
    check("writes qr_render_a.pgm", writePgm("/tmp/qr_render_a.pgm", px, w, h));
  }

  // --- 2. a realistic login URL --------------------------------------------
  {
    // What the endpoint actually returns: a 36-char UUID behind the prefix.
    const std::string url =
        "https://music.163.com/login?codekey=9f405d09-7f09-4857-9d4b-d0bb3c775fca";
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    qr::Code code;
    check("encode login URL", render(url, px, w, h, code));
    // 160 px must still give every module >= 3 px, or a TV-distance scan fails.
    const int scale = kBox / (code.size + 4);
    check("module is at least 3 px", scale >= 3,
          "v" + std::to_string(code.version) + " scale=" + std::to_string(scale));
    check("writes qr_render_login.pgm",
          writePgm("/tmp/qr_render_login.pgm", px, w, h));
  }

  // --- 3. a longer-than-realistic key --------------------------------------
  {
    std::string url = "https://music.163.com/login?codekey=";
    for (int i = 0; i < 52; ++i) url.push_back(char('0' + (i % 10)));
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    qr::Code code;
    check("encode long URL", render(url, px, w, h, code));
    check("long URL fits", code.size > 0);
    check("writes qr_render_long.pgm", writePgm("/tmp/qr_render_long.pgm", px, w, h));
  }

  std::printf("\n%d passed, %d failed\n", gPass, gFail);
  std::printf("verify with: zbarimg -q --raw /tmp/qr_render_*.pgm\n");
  return gFail == 0 ? 0 : 1;
}