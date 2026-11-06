#pragma once
// MicroSD card sentence manager, BQ27220 battery display, GT911 touch handler,
// and 800x480 e-ink UI renderer for Seeed Studio reTerminal Sticky.

#include "esphome.h"
#include "esphome/components/i2c/i2c.h"
#include "hangul_canvas.h"
#include "default_sentences.h"

#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include <esp_random.h>
#include <dirent.h>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace sticky_korean {

static const size_t MAX_HISTORY = 30;
static const char *const TAG = "sticky_korean";

class StickySentenceApp {
 public:
  bool review_mode{false};
  bool using_sd_card{false};
  int sentence_index{0};
  int battery_pct{-1};
  int battery_mv{0};
  int battery_ma{0};
  bool battery_charging{false};

  std::string current_sentence;
  std::vector<std::string> main_sentences;
  std::vector<std::string> review_sentences;
  std::vector<std::string> history;

  static std::string trim_line(const std::string &s) {
    size_t start = 0;
    // Skip UTF-8 BOM if present at start of line
    if (s.size() >= 3 &&
        static_cast<uint8_t>(s[0]) == 0xEF &&
        static_cast<uint8_t>(s[1]) == 0xBB &&
        static_cast<uint8_t>(s[2]) == 0xBF) {
      start = 3;
    }
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
      start++;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
      end--;
    }
    return s.substr(start, end - start);
  }

  static std::string to_lower_ascii(const std::string &s) {
    std::string out = s;
    for (char &c : out) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
  }

  // Read BQ27220 fuel gauge (I2C @ 0x55 on i2c_sensor) directly so battery level
  // is available immediately on the very first boot screen as well as periodic updates.
  void update_battery_from_i2c(esphome::i2c::I2CBus *bus) {
    if (bus == nullptr) return;
    uint8_t reg_soc = 0x2C;
    uint8_t raw[2] = {0, 0};
    if (bus->write_readv(0x55, &reg_soc, 1, raw, 2) == esphome::i2c::ERROR_OK) {
      uint16_t soc = static_cast<uint16_t>(raw[0]) | (static_cast<uint16_t>(raw[1]) << 8);
      if (soc <= 100) {
        battery_pct = static_cast<int>(soc);
      }
    }

    uint8_t reg_volt = 0x08;
    if (bus->write_readv(0x55, &reg_volt, 1, raw, 2) == esphome::i2c::ERROR_OK) {
      battery_mv = static_cast<int>(static_cast<uint16_t>(raw[0]) | (static_cast<uint16_t>(raw[1]) << 8));
    }

    uint8_t reg_curr = 0x0C;
    if (bus->write_readv(0x55, &reg_curr, 1, raw, 2) == esphome::i2c::ERROR_OK) {
      int16_t cur = static_cast<int16_t>(static_cast<uint16_t>(raw[0]) | (static_cast<uint16_t>(raw[1]) << 8));
      battery_ma = static_cast<int>(cur);
      battery_charging = (cur > 20);
    }
  }

  bool mount_sd(sdmmc_card_t **out_card) {
    // Check SD Card Detect pin (GPIO11: LOW = card inserted)
    int det_level = gpio_get_level(GPIO_NUM_11);
    ESP_LOGI(TAG, "SD Card Detect (GPIO11): %s (level=%d)",
             det_level == 0 ? "INSERTED" : "NOT DETECTED", det_level);

    // Ensure ePaper Display CS (GPIO15) is deselected (HIGH) and SD power (GPIO10) is ON (HIGH)
    gpio_set_level(GPIO_NUM_15, 1);
    gpio_set_level(GPIO_NUM_10, 1);

    // Enable internal pull-ups on SPI MISO (GPIO12), MOSI (GPIO14), and SD CS (GPIO8)
    gpio_set_pull_mode(GPIO_NUM_12, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_14, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_8, GPIO_PULLUP_ONLY);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;     // Shared SPI2 bus initialized by ESPHome spi: component
    host.max_freq_khz = 4000;  // 4 MHz clock for reliable full-duplex MISO over ESP32-S3 GPIO Matrix

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_8;  // Seeed reTerminal Sticky MicroSD CS pin
    slot_config.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, out_card);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "esp_vfs_fat_sdspi_mount failed: %s (0x%x)", esp_err_to_name(ret), static_cast<unsigned>(ret));
      return false;
    }
    return (*out_card != nullptr);
  }

  void unmount_sd(sdmmc_card_t *card) {
    if (card != nullptr) {
      esp_vfs_fat_sdcard_unmount("/sdcard", card);
      // Keep SD CS (GPIO8) deselected (HIGH) so ePaper display SPI transfers have zero interference
      gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);
      gpio_set_level(GPIO_NUM_8, 1);
    }
  }

  void load_decks_from_sd_or_fallback() {
    main_sentences.clear();
    sdmmc_card_t *card = nullptr;
    if (mount_sd(&card)) {
      std::string main_path = "/sdcard/Korean_sentences.txt";
      std::string review_path = "/sdcard/Korean_review_sentences.txt";

      // List root directory of MicroSD card to aid debugging and match case-insensitively
      DIR *dir = opendir("/sdcard");
      if (dir != nullptr) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
          std::string fname = ent->d_name;
          std::string lower = to_lower_ascii(fname);
          ESP_LOGI(TAG, "MicroSD file: %s", fname.c_str());
          if (lower == "korean_sentences.txt" || lower == "korean_sentences.txt.txt" || lower == "korean~1.txt") {
            main_path = "/sdcard/" + fname;
          } else if (lower == "korean_review_sentences.txt" || lower == "korean_review_sentences.txt.txt") {
            review_path = "/sdcard/" + fname;
          }
        }
        closedir(dir);
      }

      // 1. Read main Korean sentences file from MicroSD
      std::ifstream f_main(main_path.c_str());
      if (f_main.is_open()) {
        std::string line;
        while (std::getline(f_main, line)) {
          std::string cleaned = trim_line(line);
          if (!cleaned.empty()) {
            main_sentences.push_back(cleaned);
          }
        }
        f_main.close();
        ESP_LOGI(TAG, "Read %u sentences from %s",
                 static_cast<unsigned>(main_sentences.size()), main_path.c_str());
      } else {
        ESP_LOGW(TAG, "MicroSD mounted, but could not open %s", main_path.c_str());
      }

      // 2. Read review sentences file from MicroSD
      std::ifstream f_rev(review_path.c_str());
      if (f_rev.is_open()) {
        review_sentences.clear();
        std::string line;
        while (std::getline(f_rev, line)) {
          std::string cleaned = trim_line(line);
          if (!cleaned.empty() &&
              std::find(review_sentences.begin(), review_sentences.end(), cleaned) == review_sentences.end()) {
            review_sentences.push_back(cleaned);
          }
        }
        f_rev.close();
      }

      unmount_sd(card);
    }

    if (!main_sentences.empty()) {
      using_sd_card = true;
      ESP_LOGI(TAG, "Active deck source: MicroSD [외장] (%u sentences)",
               static_cast<unsigned>(main_sentences.size()));
    } else {
      using_sd_card = false;
      main_sentences.reserve(DEFAULT_KOREAN_SENTENCES_COUNT);
      for (size_t i = 0; i < DEFAULT_KOREAN_SENTENCES_COUNT; i++) {
        main_sentences.emplace_back(DEFAULT_KOREAN_SENTENCES[i]);
      }
      ESP_LOGI(TAG, "Using %u built-in fallback sentences [내장]",
               static_cast<unsigned>(main_sentences.size()));
    }
  }

  void save_review_deck_to_sd() {
    sdmmc_card_t *card = nullptr;
    if (!mount_sd(&card)) {
      return;
    }
    std::ofstream f_rev("/sdcard/Korean_review_sentences.txt", std::ios::out | std::ios::trunc);
    if (f_rev.is_open()) {
      for (const auto &s : review_sentences) {
        f_rev << s << "\n";
      }
      f_rev.close();
      ESP_LOGI(TAG, "Saved %u review sentences to /sdcard/Korean_review_sentences.txt",
               static_cast<unsigned>(review_sentences.size()));
    }
    unmount_sd(card);
  }

  const std::vector<std::string> &active_deck() const {
    return review_mode ? review_sentences : main_sentences;
  }

  int pick_random_index(const std::vector<std::string> &deck, const std::string &avoid) const {
    if (deck.size() <= 1) return 0;
    for (int attempt = 0; attempt < 8; attempt++) {
      int idx = static_cast<int>(esp_random() % deck.size());
      if (deck[idx] != avoid) {
        return idx;
      }
    }
    return static_cast<int>(esp_random() % deck.size());
  }

  void init_on_boot(esphome::i2c::I2CBus *sensor_bus = nullptr) {
    if (sensor_bus != nullptr) {
      update_battery_from_i2c(sensor_bus);
    }
    load_decks_from_sd_or_fallback();
    next_random();
  }

  void next_random() {
    // If SD wasn't inserted at boot, try checking again when advancing
    if (!using_sd_card) {
      load_decks_from_sd_or_fallback();
    }
    const auto &deck = active_deck();
    if (deck.empty()) {
      current_sentence.clear();
      sentence_index = 0;
      return;
    }
    if (!current_sentence.empty() && (history.empty() || history.back() != current_sentence)) {
      history.push_back(current_sentence);
      if (history.size() > MAX_HISTORY) {
        history.erase(history.begin());
      }
    }
    sentence_index = pick_random_index(deck, current_sentence);
    current_sentence = deck[sentence_index];
    ESP_LOGI(TAG, "Next sentence [%d/%u]: %s", sentence_index + 1,
             static_cast<unsigned>(deck.size()), current_sentence.c_str());
  }

  void prev_sentence() {
    const auto &deck = active_deck();
    std::string popped;
    while (!history.empty()) {
      std::string candidate = history.back();
      history.pop_back();
      if (!candidate.empty() && candidate != current_sentence) {
        popped = candidate;
        break;
      }
    }
    if (!popped.empty()) {
      current_sentence = popped;
      auto it = std::find(deck.begin(), deck.end(), popped);
      sentence_index = (it != deck.end()) ? static_cast<int>(std::distance(deck.begin(), it)) : 0;
    } else if (!deck.empty()) {
      sentence_index = (sentence_index - 1 + static_cast<int>(deck.size())) % static_cast<int>(deck.size());
      current_sentence = deck[sentence_index];
    }
    ESP_LOGI(TAG, "Prev sentence [%d/%u]: %s", sentence_index + 1,
             static_cast<unsigned>(deck.size()), current_sentence.c_str());
  }

  void toggle_mark_review() {
    const auto &deck = active_deck();
    if (current_sentence.empty() && !deck.empty()) {
      sentence_index = std::max(0, std::min(static_cast<int>(deck.size()) - 1, sentence_index));
      current_sentence = deck[sentence_index];
    }
    if (current_sentence.empty()) return;

    auto it = std::find(review_sentences.begin(), review_sentences.end(), current_sentence);
    if (it != review_sentences.end()) {
      review_sentences.erase(it);
      ESP_LOGI(TAG, "Unmarked review sentence (total=%u)", static_cast<unsigned>(review_sentences.size()));
    } else {
      review_sentences.push_back(current_sentence);
      ESP_LOGI(TAG, "Marked sentence for review (total=%u)", static_cast<unsigned>(review_sentences.size()));
    }
    save_review_deck_to_sd();

    if (review_mode) {
      if (review_sentences.empty()) {
        current_sentence.clear();
        sentence_index = 0;
      } else {
        auto cur_it = std::find(review_sentences.begin(), review_sentences.end(), current_sentence);
        if (cur_it != review_sentences.end()) {
          sentence_index = static_cast<int>(std::distance(review_sentences.begin(), cur_it));
        } else {
          sentence_index = pick_random_index(review_sentences, "");
          current_sentence = review_sentences[sentence_index];
        }
      }
    }
  }

  void toggle_mode() {
    review_mode = !review_mode;
    const auto &deck = active_deck();
    if (!deck.empty()) {
      sentence_index = pick_random_index(deck, current_sentence);
      current_sentence = deck[sentence_index];
    } else {
      sentence_index = 0;
      current_sentence.clear();
    }
    ESP_LOGI(TAG, "Toggled mode -> %s", review_mode ? "REVIEW" : "ALL");
  }

  // Handle capacitive touch tap at screen coordinates (x in 0..800, y in 0..480)
  void handle_touch(int x, int y) {
    ESP_LOGI(TAG, "Touch input at x=%d, y=%d", x, y);

    // 1. Top Header Bar (y < 76): Tap mode badge / left header to toggle Normal <-> Review mode
    if (y < 76) {
      if (x >= 140 && x <= 480) {
        toggle_mode();
        return;
      }
      // Tapping elsewhere in the top header advances to next sentence
      next_random();
      return;
    }

    // 2. Bottom Button Bar (y > 410):
    //    - Left ("◀ 이전 (▲)", x < 260)       -> Previous sentence
    //    - Center ("☆ 복습 저장 (●)", 260..540) -> Toggle mark for review
    //    - Right ("다음 (▼) ▶", x > 540)      -> Next random sentence
    if (y > 410) {
      if (x < 260) {
        prev_sentence();
      } else if (x <= 540) {
        toggle_mark_review();
      } else {
        next_random();
      }
      return;
    }

    // 3. Main Center Korean Sentence Reading Panel (76 <= y <= 410):
    //    - Tap left edge (x < 200) -> Previous sentence
    //    - Tap center/right (x >= 200) -> Next random sentence
    if (x < 200) {
      prev_sentence();
    } else {
      next_random();
    }
  }

  bool is_current_marked() const {
    if (current_sentence.empty()) return false;
    return std::find(review_sentences.begin(), review_sentences.end(), current_sentence) != review_sentences.end();
  }

  // Draw battery icon + percentage badge in top-right header, returning the left-most X of the badge
  int draw_battery_badge(esphome::display::Display &it, int right_edge_x, int top_y) const {
    auto BLACK = esphome::Color::BLACK;
    char pct_buf[24];
    if (battery_pct >= 0 && battery_pct <= 100) {
      if (battery_charging) {
        std::snprintf(pct_buf, sizeof(pct_buf), "+%d%%", battery_pct);
      } else {
        std::snprintf(pct_buf, sizeof(pct_buf), "%d%%", battery_pct);
      }
    } else {
      std::snprintf(pct_buf, sizeof(pct_buf), "--%%");
    }

    int text_w = sticky_hangul::measure_hangul_line_px(pct_buf, 18.0f);
    int icon_w = 34;  // 30px body + 4px positive terminal nub
    int icon_h = 18;
    int gap = 6;
    int total_w = icon_w + gap + text_w + 16;
    int box_x = right_edge_x - total_w;

    // Outer pill border
    it.rectangle(box_x, top_y, total_w, 34, BLACK);
    it.rectangle(box_x + 1, top_y + 1, total_w - 2, 32, BLACK);

    // Battery body outline (30x18) + terminal cap (4x8)
    int bx = box_x + 8;
    int by = top_y + 8;
    it.rectangle(bx, by, 30, icon_h, BLACK);
    it.rectangle(bx + 1, by + 1, 28, icon_h - 2, BLACK);
    it.filled_rectangle(bx + 30, by + 5, 4, 8, BLACK);

    // Battery fill bar proportional to 0..100%
    if (battery_pct > 0) {
      int max_fill_w = 22;
      int fill_w = std::max(2, (std::min(100, battery_pct) * max_fill_w) / 100);
      it.filled_rectangle(bx + 4, by + 4, fill_w, icon_h - 8, BLACK);
    }

    // Percentage text next to battery icon
    sticky_hangul::draw_hangul_line(it, bx + icon_w + gap, top_y + 8, pct_buf, 18.0f, 9.0f, BLACK, false);
    return box_x;
  }

  void render(esphome::display::Display &it) const {
    const auto &deck = active_deck();
    bool is_marked = is_current_marked();
    const int W = it.get_width();   // 800
    const int H = it.get_height();  // 480

    // White background, black ink (epaper_spi maps Color::WHITE -> white, Color::BLACK -> black)
    auto WHITE = esphome::Color::WHITE;
    auto BLACK = esphome::Color::BLACK;
    it.fill(WHITE);

    // ---------------------------------------------------------
    // 1. Top Header Bar (y = 18..58, bottom border at y = 64)
    // ---------------------------------------------------------
    int badge1_w = sticky_hangul::measure_hangul_line_px("한국어 문장", 20.0f) + 24;
    it.filled_rectangle(24, 18, badge1_w, 34, BLACK);
    sticky_hangul::draw_hangul_line(it, 24 + 12, 25, "한국어 문장", 20.0f, 9.5f, WHITE, false);

    // Mode Badge: "전체 모드" or "★ 복습 (N)"
    char mode_buf[64];
    if (review_mode) {
      std::snprintf(mode_buf, sizeof(mode_buf), "★ 복습 (%u)", static_cast<unsigned>(review_sentences.size()));
    } else {
      std::snprintf(mode_buf, sizeof(mode_buf), "전체 모드");
    }
    int badge2_x = 24 + badge1_w + 10;
    int badge2_w = sticky_hangul::measure_hangul_line_px(mode_buf, 19.0f) + 22;
    if (review_mode) {
      it.filled_rectangle(badge2_x, 18, badge2_w, 34, BLACK);
      sticky_hangul::draw_hangul_line(it, badge2_x + 11, 25, mode_buf, 19.0f, 9.2f, WHITE, false);
    } else {
      it.rectangle(badge2_x, 18, badge2_w, 34, BLACK);
      it.rectangle(badge2_x + 1, 19, badge2_w - 2, 32, BLACK);
      sticky_hangul::draw_hangul_line(it, badge2_x + 11, 25, mode_buf, 19.0f, 9.2f, BLACK, false);
    }

    // Source Badge: "[외장]" (MicroSD) or "[내장]" (Fallback Flash)
    const char *src_label = using_sd_card ? "[외장]" : "[내장]";
    int badge3_x = badge2_x + badge2_w + 10;
    int badge3_w = sticky_hangul::measure_hangul_line_px(src_label, 17.0f) + 16;
    it.rectangle(badge3_x, 20, badge3_w, 30, BLACK);
    sticky_hangul::draw_hangul_line(it, badge3_x + 8, 26, src_label, 17.0f, 8.8f, BLACK, false);

    // Right side of Top Header:
    // (a) Battery Badge on far right (x = W - 24)
    int batt_left_x = draw_battery_badge(it, W - 24, 18);

    // (b) Counter ("12 / 1000") to the left of the battery badge
    char counter_buf[48];
    int disp_num = deck.empty() ? 0 : (sentence_index + 1);
    std::snprintf(counter_buf, sizeof(counter_buf), "%d / %u", disp_num, static_cast<unsigned>(deck.size()));
    int counter_w = sticky_hangul::measure_hangul_line_px(counter_buf, 19.0f);
    int counter_x = batt_left_x - 14 - counter_w;
    sticky_hangul::draw_hangul_line(it, counter_x, 26, counter_buf, 19.0f, 9.0f, BLACK, false);

    // (c) Optional "★ 복습" indicator pill to the left of the counter
    if (is_marked) {
      int mark_w = sticky_hangul::measure_hangul_line_px("★ 복습", 17.0f) + 18;
      int mark_x = counter_x - 10 - mark_w;
      if (mark_x > badge3_x + badge3_w + 6) {
        it.filled_rectangle(mark_x, 20, mark_w, 30, BLACK);
        sticky_hangul::draw_hangul_line(it, mark_x + 9, 26, "★ 복습", 17.0f, 9.0f, WHITE, false);
      }
    }

    // Header divider line (3px thick)
    it.filled_rectangle(24, 64, W - 48, 3, BLACK);

    // ---------------------------------------------------------
    // 2. Main Center Korean Sentence Reading Panel
    // ---------------------------------------------------------
    const int card_x = 24;
    const int card_y = 82;
    const int card_w = W - 48;
    const int card_h = H - 82 - 74;  // 324px tall

    // 3px bold card border
    it.rectangle(card_x, card_y, card_w, card_h, BLACK);
    it.rectangle(card_x + 1, card_y + 1, card_w - 2, card_h - 2, BLACK);
    it.rectangle(card_x + 2, card_y + 2, card_w - 4, card_h - 4, BLACK);

    int center_x = W / 2;
    int center_y = card_y + card_h / 2;

    if (deck.empty()) {
      sticky_hangul::draw_hangul_line(it, center_x, center_y - 30, "저장된 복습 문장이 없습니다.", 34.0f, 8.8f, BLACK, true);
      sticky_hangul::draw_hangul_line(it, center_x, center_y + 20, "문장에서 (●) 버튼을 눌러 추가하세요.", 22.0f, 8.5f, BLACK, true);
    } else {
      sticky_hangul::draw_hangul_sentence_centered(it, center_x, center_y, current_sentence, BLACK);
    }

    // ---------------------------------------------------------
    // 3. Bottom Sticky Physical & Touch Button Bar
    // ---------------------------------------------------------
    it.filled_rectangle(24, H - 58, W - 48, 2, BLACK);

    const int btn_y = H - 48;
    const int btn_h = 34;

    // Left Button: "◀ 이전 (▲)"
    int prev_w = sticky_hangul::measure_hangul_line_px("◀ 이전 (▲)", 19.0f) + 28;
    it.rectangle(24, btn_y, prev_w, btn_h, BLACK);
    it.rectangle(25, btn_y + 1, prev_w - 2, btn_h - 2, BLACK);
    sticky_hangul::draw_hangul_line(it, 24 + 14, btn_y + 7, "◀ 이전 (▲)", 19.0f, 9.0f, BLACK, false);

    // Center Button: "★ 복습 완료 (●)" or "☆ 복습 저장 (●)"
    const char *mark_btn_text = is_marked ? "★ 복습 완료 (●)" : "☆ 복습 저장 (●)";
    int mark_btn_w = sticky_hangul::measure_hangul_line_px(mark_btn_text, 20.0f) + 36;
    int mark_btn_x = (W - mark_btn_w) / 2;
    if (is_marked) {
      it.rectangle(mark_btn_x, btn_y, mark_btn_w, btn_h, BLACK);
      it.rectangle(mark_btn_x + 1, btn_y + 1, mark_btn_w - 2, btn_h - 2, BLACK);
      it.rectangle(mark_btn_x + 2, btn_y + 2, mark_btn_w - 4, btn_h - 4, BLACK);
      sticky_hangul::draw_hangul_line(it, mark_btn_x + 18, btn_y + 7, mark_btn_text, 20.0f, 9.2f, BLACK, false);
    } else {
      it.filled_rectangle(mark_btn_x, btn_y, mark_btn_w, btn_h, BLACK);
      sticky_hangul::draw_hangul_line(it, mark_btn_x + 18, btn_y + 7, mark_btn_text, 20.0f, 9.2f, WHITE, false);
    }

    // Right Button: "다음 (▼) ▶"
    int next_w = sticky_hangul::measure_hangul_line_px("다음 (▼) ▶", 19.0f) + 28;
    int next_x = W - 24 - next_w;
    it.rectangle(next_x, btn_y, next_w, btn_h, BLACK);
    it.rectangle(next_x + 1, btn_y + 1, next_w - 2, btn_h - 2, BLACK);
    sticky_hangul::draw_hangul_line(it, next_x + 14, btn_y + 7, "다음 (▼) ▶", 19.0f, 9.0f, BLACK, false);
  }
};

// Global singleton instance used by ESPHome lambdas
inline StickySentenceApp &app() {
  static StickySentenceApp instance;
  return instance;
}

}  // namespace sticky_korean
