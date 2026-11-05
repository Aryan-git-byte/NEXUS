#pragma GCC optimize ("O3")
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "soc/usb_serial_jtag_reg.h"
#include "hid_ble.h"

// ---- LCD ----
#define PIN_DC 13
#define PIN_WR 15
#define PIN_RST -1
#define W 320
#define H 480
#define INVERT_COLORS 0
#define WRBIT (1u << PIN_WR)
#define DCBIT (1u << PIN_DC)

// ---- Touch (XPT2046, bit-banged SPI) ----
#define T_CLK 47
#define T_CS  48
#define T_DIN 43
#define T_DOUT 44
#define Z_THRESHOLD 600

static const int8_t D[16] = {8, 21, 18, 17, 16, 6, 4, 2, 20, 19, 10, 7, 12, 11, 14, 9};
static uint32_t lo[256], hi[256], DATA_MASK = 0;

static constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ================= LCD =================
static void w16(uint16_t v) {
  REG_WRITE(GPIO_OUT_W1TC_REG, DATA_MASK);
  REG_WRITE(GPIO_OUT_W1TS_REG, lo[v & 0xFF] | hi[v >> 8]);
  REG_WRITE(GPIO_OUT_W1TC_REG, WRBIT);
  REG_WRITE(GPIO_OUT_W1TS_REG, WRBIT);
}
static void cmd(uint8_t c) {
  REG_WRITE(GPIO_OUT_W1TC_REG, DCBIT);
  w16(c);
  REG_WRITE(GPIO_OUT_W1TS_REG, DCBIT);
}
static void dat(uint8_t d) { w16(d); }
static void cmdd(uint8_t c, const uint8_t *d, int n) {
  cmd(c);
  for (int i = 0; i < n; i++) dat(d[i]);
}
static void pinout(int p) {
  gpio_reset_pin((gpio_num_t)p);
  gpio_set_direction((gpio_num_t)p, GPIO_MODE_OUTPUT);
  gpio_set_drive_capability((gpio_num_t)p, GPIO_DRIVE_CAP_3);
}
static void setWindow(int x0, int y0, int x1, int y1) {
  cmd(0x2A); dat(x0 >> 8); dat(x0 & 255); dat(x1 >> 8); dat(x1 & 255);
  cmd(0x2B); dat(y0 >> 8); dat(y0 & 255); dat(y1 >> 8); dat(y1 & 255);
  cmd(0x2C);
}
static void fillRect(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  setWindow(x, y, x + w - 1, y + h - 1);
  const uint32_t d = lo[c & 255] | hi[c >> 8];
  REG_WRITE(GPIO_OUT_W1TC_REG, DATA_MASK);
  REG_WRITE(GPIO_OUT_W1TS_REG, d);
  for (int i = w * h; i > 0; i--) {
    REG_WRITE(GPIO_OUT_W1TC_REG, WRBIT);
    REG_WRITE(GPIO_OUT_W1TS_REG, WRBIT);
  }
}
static void lcdInit() {
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  for (int i = 0; i < 16; i++) { pinout(D[i]); DATA_MASK |= 1u << D[i]; }
  for (int v = 0; v < 256; v++) {
    lo[v] = hi[v] = 0;
    for (int b = 0; b < 8; b++)
      if (v & (1 << b)) { lo[v] |= 1u << D[b]; hi[v] |= 1u << D[b + 8]; }
  }
  pinout(PIN_DC);
  pinout(PIN_WR);
  REG_WRITE(GPIO_OUT_W1TS_REG, WRBIT | DCBIT);
  if (PIN_RST >= 0) {
    pinout(PIN_RST);
    gpio_set_level((gpio_num_t)PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));
  }
  cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));
  cmd(0x11); vTaskDelay(pdMS_TO_TICKS(150));
  const uint8_t pg[] = {0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F};
  const uint8_t ng[] = {0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F};
  cmdd(0xE0, pg, 15);
  cmdd(0xE1, ng, 15);
  { const uint8_t d[] = {0x17, 0x15};              cmdd(0xC0, d, 2); }
  { const uint8_t d[] = {0x41};                    cmdd(0xC1, d, 1); }
  { const uint8_t d[] = {0x00, 0x12, 0x80};        cmdd(0xC5, d, 3); }
  { const uint8_t d[] = {0x48};                    cmdd(0x36, d, 1); }
  { const uint8_t d[] = {0x55};                    cmdd(0x3A, d, 1); }
  { const uint8_t d[] = {0x00};                    cmdd(0xB0, d, 1); }
  { const uint8_t d[] = {0xA0};                    cmdd(0xB1, d, 1); }
  { const uint8_t d[] = {0x02};                    cmdd(0xB4, d, 1); }
  { const uint8_t d[] = {0x02, 0x02};              cmdd(0xB6, d, 2); }
  { const uint8_t d[] = {0x00};                    cmdd(0xE9, d, 1); }
  { const uint8_t d[] = {0xA9, 0x51, 0x2C, 0x82};  cmdd(0xF7, d, 4); }
  if (INVERT_COLORS) cmd(0x21);
  cmd(0x29); vTaskDelay(pdMS_TO_TICKS(50));
}

// ================= touch =================
static void touchInit() {
  gpio_reset_pin((gpio_num_t)T_DIN);
  gpio_reset_pin((gpio_num_t)T_DOUT);
  gpio_reset_pin((gpio_num_t)T_CLK);
  gpio_reset_pin((gpio_num_t)T_CS);
  gpio_set_direction((gpio_num_t)T_DIN, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)T_CLK, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)T_CS,  GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)T_DOUT, GPIO_MODE_INPUT);
  gpio_set_level((gpio_num_t)T_CS, 1);
  gpio_set_level((gpio_num_t)T_CLK, 0);
}
static uint16_t xpt(uint8_t command) {
  uint32_t r = 0;
  gpio_set_level((gpio_num_t)T_CS, 0);
  esp_rom_delay_us(1);
  for (int i = 7; i >= 0; i--) {
    gpio_set_level((gpio_num_t)T_DIN, (command >> i) & 1);
    gpio_set_level((gpio_num_t)T_CLK, 1); esp_rom_delay_us(1);
    gpio_set_level((gpio_num_t)T_CLK, 0); esp_rom_delay_us(1);
  }
  gpio_set_level((gpio_num_t)T_DIN, 0);
  for (int i = 0; i < 16; i++) {
    gpio_set_level((gpio_num_t)T_CLK, 1); esp_rom_delay_us(1);
    r = (r << 1) | gpio_get_level((gpio_num_t)T_DOUT);
    gpio_set_level((gpio_num_t)T_CLK, 0); esp_rom_delay_us(1);
  }
  gpio_set_level((gpio_num_t)T_CS, 1);
  return (r >> 3) & 0xFFF;
}
static void sort5(uint16_t *a) {
  for (int i = 1; i < 5; i++) {
    uint16_t v = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
}
static bool readRaw(int &rx, int &ry) {
  uint16_t z1 = xpt(0xB1), z2 = xpt(0xC1);
  int z = (int)z1 + 4095 - (int)z2;
  if (z < Z_THRESHOLD) return false;
  uint16_t xs[5], ys[5];
  for (int i = 0; i < 5; i++) { xs[i] = xpt(0xD1); ys[i] = xpt(0x91); }
  sort5(xs); sort5(ys);
  rx = xs[2]; ry = ys[2];
  return true;
}

// ================= shapes =================
static void disc(int x, int y, int r, uint16_t col) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)sqrtf((float)(r * r - dy * dy));
    fillRect(x - dx, y + dy, 2 * dx + 1, 1, col);
  }
}
static void frame(int x, int y, int w, int h, int t, uint16_t col) {
  fillRect(x, y, w, t, col);
  fillRect(x, y + h - t, w, t, col);
  fillRect(x, y, t, h, col);
  fillRect(x + w - t, y, t, h, col);
}
static void line(int x0, int y0, int x1, int y1, int t, uint16_t col) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    fillRect(x0 - t / 2, y0 - t / 2, t, t, col);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
static void triRight(int x, int cy, int w, int h, uint16_t col) {
  for (int i = 0; i < w; i += 2) {
    int ht = h * (w - i) / w;
    fillRect(x + i, cy - ht / 2, 2, ht, col);
  }
}
static void speaker(int cx, int cy, uint16_t col) {
  fillRect(cx - 18, cy - 6, 8, 12, col);
  for (int i = 0; i < 14; i++) {
    int ht = 12 + i * 18 / 14;
    fillRect(cx - 10 + i, cy - ht / 2, 1, ht, col);
  }
}

// ================= font (5x7) =================
static const uint8_t FONT[38][5] = {
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
  {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
  {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
  {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
  {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
  {0x08,0x08,0x08,0x08,0x08},{0x08,0x08,0x3E,0x08,0x08}
};
static int glyphIdx(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
  if (c == '-') return 36;
  if (c == '+') return 37;
  return -1;
}
static int textWidth(const char *s, int sc) {
  int n = 0;
  while (s[n]) n++;
  return n > 0 ? (n * 6 - 1) * sc : 0;
}
static void drawText(int x, int y, const char *s, int sc, uint16_t col) {
  for (; *s; s++, x += 6 * sc) {
    int g = glyphIdx(*s);
    if (g < 0) continue;
    for (int c = 0; c < 5; c++) {
      uint8_t bits = FONT[g][c];
      int r = 0;
      while (r < 7) {
        if (bits & (1 << r)) {
          int r0 = r;
          while (r < 7 && (bits & (1 << r))) r++;
          fillRect(x + c * sc, y + r0 * sc, sc, (r - r0) * sc, col);
        } else {
          r++;
        }
      }
    }
  }
}

// ================= calibration =================
static float cA, cB, cC, cD, cE, cF;

static void drawTarget(int x, int y, uint16_t ring) {
  disc(x, y, 22, ring);
  disc(x, y, 17, 0x0000);
  disc(x, y, 4, 0xF800);
}
static void getCalPoint(int sx, int sy, int &tx, int &ty) {
  fillRect(0, 0, W, H, 0x0000);
  drawTarget(sx, sy, 0xFFFF);
  int x, y;
  while (readRaw(x, y)) vTaskDelay(pdMS_TO_TICKS(10));
  while (!readRaw(x, y)) vTaskDelay(pdMS_TO_TICKS(10));
  vTaskDelay(pdMS_TO_TICKS(80));
  long ax = 0, ay = 0;
  int n = 0;
  for (int i = 0; i < 30; i++) {
    if (readRaw(x, y)) { ax += x; ay += y; n++; }
    vTaskDelay(pdMS_TO_TICKS(8));
  }
  if (n == 0) n = 1;
  tx = ax / n; ty = ay / n;
  drawTarget(sx, sy, 0x07E0);
  while (readRaw(x, y)) vTaskDelay(pdMS_TO_TICKS(10));
  vTaskDelay(pdMS_TO_TICKS(200));
}
static void calibrate() {
  const int sx[3] = {56, W - 56, W / 2};
  const int sy[3] = {72, H / 2,  H - 72};
  while (true) {
    int tx[3], ty[3];
    for (int i = 0; i < 3; i++) getCalPoint(sx[i], sy[i], tx[i], ty[i]);
    float det = (float)(tx[0] - tx[2]) * (ty[1] - ty[2]) - (float)(tx[1] - tx[2]) * (ty[0] - ty[2]);
    if (det == 0) continue;
    cA = ((sx[0] - sx[2]) * (float)(ty[1] - ty[2]) - (sx[1] - sx[2]) * (float)(ty[0] - ty[2])) / det;
    cB = ((tx[0] - tx[2]) * (float)(sx[1] - sx[2]) - (tx[1] - tx[2]) * (float)(sx[0] - sx[2])) / det;
    cC = (sx[0] * (float)(tx[1]*ty[2] - tx[2]*ty[1]) - sx[1] * (float)(tx[0]*ty[2] - tx[2]*ty[0])
        + sx[2] * (float)(tx[0]*ty[1] - tx[1]*ty[0])) / det;
    cD = ((sy[0] - sy[2]) * (float)(ty[1] - ty[2]) - (sy[1] - sy[2]) * (float)(ty[0] - ty[2])) / det;
    cE = ((tx[0] - tx[2]) * (float)(sy[1] - sy[2]) - (tx[1] - tx[2]) * (float)(sy[0] - sy[2])) / det;
    cF = (sy[0] * (float)(tx[1]*ty[2] - tx[2]*ty[1]) - sy[1] * (float)(tx[0]*ty[2] - tx[2]*ty[0])
        + sy[2] * (float)(tx[0]*ty[1] - tx[1]*ty[0])) / det;
    float sxScale = fabsf(cA) + fabsf(cB);
    float syScale = fabsf(cD) + fabsf(cE);
    if (sxScale > 0.03f && sxScale < 0.2f && syScale > 0.02f && syScale < 0.2f) return;
  }
}
static bool getTouch(int &sx, int &sy) {
  int rx, ry;
  if (!readRaw(rx, ry)) return false;
  sx = (int)(cA * rx + cB * ry + cC);
  sy = (int)(cD * rx + cE * ry + cF);
  if (sx < 0) sx = 0;
  if (sx >= W) sx = W - 1;
  if (sy < 0) sy = 0;
  if (sy >= H) sy = H - 1;
  return true;
}
static bool loadCal() {
  nvs_handle_t h;
  if (nvs_open("touch", NVS_READONLY, &h) != ESP_OK) return false;
  float v[6];
  size_t len = sizeof(v);
  esp_err_t e = nvs_get_blob(h, "cal", v, &len);
  nvs_close(h);
  if (e != ESP_OK || len != sizeof(v)) return false;
  cA = v[0]; cB = v[1]; cC = v[2]; cD = v[3]; cE = v[4]; cF = v[5];
  return true;
}
static void saveCal() {
  nvs_handle_t h;
  if (nvs_open("touch", NVS_READWRITE, &h) != ESP_OK) return;
  float v[6] = {cA, cB, cC, cD, cE, cF};
  nvs_set_blob(h, "cal", v, sizeof(v));
  nvs_commit(h);
  nvs_close(h);
}

// ================= stream deck =================
enum { I_COPY, I_PASTE, I_CUT, I_UNDO, I_SAVE, I_SNIP, I_DESK, I_LOCK, I_PLAY, I_NEXT, I_VOLD, I_VOLU };
enum { K_KEY, K_CC };

struct Btn {
  const char *label;
  uint8_t icon;
  uint16_t color;
  uint8_t kind;
  uint8_t mod;      // bit0 Ctrl, bit1 Shift, bit2 Alt, bit3 Win/Cmd
  uint16_t code;    // HID keycode, or consumer usage
};

static const Btn BTNS[12] = {
  {"COPY",  I_COPY,  rgb(37, 99, 235),  K_KEY, 0x01, 0x06},   // Ctrl+C
  {"PASTE", I_PASTE, rgb(22, 163, 74),  K_KEY, 0x01, 0x19},   // Ctrl+V
  {"CUT",   I_CUT,   rgb(220, 38, 38),  K_KEY, 0x01, 0x1B},   // Ctrl+X
  {"UNDO",  I_UNDO,  rgb(217, 119, 6),  K_KEY, 0x01, 0x1D},   // Ctrl+Z
  {"SAVE",  I_SAVE,  rgb(8, 145, 178),  K_KEY, 0x01, 0x16},   // Ctrl+S
  {"SNIP",  I_SNIP,  rgb(147, 51, 234), K_KEY, 0x0A, 0x16},   // Win+Shift+S
  {"DESK",  I_DESK,  rgb(71, 85, 105),  K_KEY, 0x08, 0x07},   // Win+D
  {"LOCK",  I_LOCK,  rgb(190, 24, 93),  K_KEY, 0x08, 0x0F},   // Win+L
  {"PLAY",  I_PLAY,  rgb(5, 150, 105),  K_CC,  0, 0xCD},
  {"NEXT",  I_NEXT,  rgb(79, 70, 229),  K_CC,  0, 0xB5},
  {"VOL-",  I_VOLD,  rgb(202, 138, 4),  K_CC,  0, 0xEA},
  {"VOL+",  I_VOLU,  rgb(234, 88, 12),  K_CC,  0, 0xE9},
};

#define BW 96
#define BH 100
static int bx(int i) { return 8 + (i % 3) * (BW + 8); }
static int by(int i) { return 44 + (i / 3) * (BH + 8); }

static void drawIcon(int id, int cx, int cy, uint16_t fg, uint16_t bg) {
  switch (id) {
    case I_COPY:
      frame(cx - 16, cy - 16, 22, 26, 3, fg);
      fillRect(cx - 6, cy - 8, 22, 26, bg);
      frame(cx - 6, cy - 8, 22, 26, 3, fg);
      break;
    case I_PASTE:
      frame(cx - 15, cy - 14, 30, 36, 3, fg);
      fillRect(cx - 8, cy - 19, 16, 9, fg);
      fillRect(cx - 8, cy - 2, 16, 3, fg);
      fillRect(cx - 8, cy + 6, 16, 3, fg);
      break;
    case I_CUT:
      line(cx - 10, cy + 14, cx + 10, cy - 16, 3, fg);
      line(cx + 10, cy + 14, cx - 10, cy - 16, 3, fg);
      disc(cx - 10, cy + 14, 6, fg);
      disc(cx + 10, cy + 14, 6, fg);
      disc(cx - 10, cy + 14, 3, bg);
      disc(cx + 10, cy + 14, 3, bg);
      break;
    case I_UNDO:
      line(cx - 16, cy - 6, cx - 6, cy - 16, 3, fg);
      line(cx - 16, cy - 6, cx - 6, cy + 4, 3, fg);
      line(cx - 16, cy - 6, cx + 8, cy - 6, 3, fg);
      line(cx + 8, cy - 6, cx + 16, cy + 2, 3, fg);
      line(cx + 16, cy + 2, cx + 8, cy + 12, 3, fg);
      line(cx + 8, cy + 12, cx - 6, cy + 12, 3, fg);
      break;
    case I_SAVE:
      fillRect(cx - 17, cy - 17, 34, 34, fg);
      fillRect(cx - 10, cy - 17, 20, 12, bg);
      fillRect(cx + 3, cy - 15, 4, 8, fg);
      fillRect(cx - 11, cy + 2, 22, 15, bg);
      fillRect(cx - 8, cy + 6, 16, 2, fg);
      fillRect(cx - 8, cy + 11, 16, 2, fg);
      break;
    case I_SNIP:
      fillRect(cx - 18, cy - 18, 14, 4, fg); fillRect(cx - 18, cy - 18, 4, 14, fg);
      fillRect(cx + 4, cy - 18, 14, 4, fg);  fillRect(cx + 14, cy - 18, 4, 14, fg);
      fillRect(cx - 18, cy + 14, 14, 4, fg); fillRect(cx - 18, cy + 4, 4, 14, fg);
      fillRect(cx + 4, cy + 14, 14, 4, fg);  fillRect(cx + 14, cy + 4, 4, 14, fg);
      disc(cx, cy, 5, fg);
      break;
    case I_DESK:
      frame(cx - 20, cy - 16, 40, 26, 3, fg);
      fillRect(cx - 3, cy + 10, 6, 8, fg);
      fillRect(cx - 10, cy + 18, 20, 3, fg);
      break;
    case I_LOCK:
      frame(cx - 9, cy - 20, 18, 22, 4, fg);
      fillRect(cx - 14, cy - 4, 28, 24, fg);
      disc(cx, cy + 6, 3, bg);
      fillRect(cx - 1, cy + 8, 3, 6, bg);
      break;
    case I_PLAY:
      triRight(cx - 10, cy, 28, 36, fg);
      break;
    case I_NEXT:
      triRight(cx - 14, cy, 24, 32, fg);
      fillRect(cx + 12, cy - 16, 5, 32, fg);
      break;
    case I_VOLD:
      speaker(cx - 2, cy, fg);
      fillRect(cx + 8, cy - 2, 12, 4, fg);
      break;
    case I_VOLU:
      speaker(cx - 4, cy, fg);
      fillRect(cx + 8, cy - 2, 12, 4, fg);
      fillRect(cx + 12, cy - 6, 4, 12, fg);
      break;
  }
}

static void drawButton(int i) {
  const Btn &b = BTNS[i];
  int x = bx(i), y = by(i);
  fillRect(x, y, BW, BH, b.color);
  drawIcon(b.icon, x + BW / 2, y + 36, 0xFFFF, b.color);
  drawText(x + (BW - textWidth(b.label, 2)) / 2, y + 74, b.label, 2, 0xFFFF);
}

static void drawStatus(bool connected) {
  fillRect(0, 0, W, 40, 0x0000);
  disc(22, 20, 8, connected ? 0x07E0 : rgb(255, 140, 0));
  drawText(44, 13, connected ? "CONNECTED" : "WAITING", 2, 0xFFFF);
}

static void drawUI(bool connected) {
  fillRect(0, 0, W, H, 0x0000);
  drawStatus(connected);
  for (int i = 0; i < 12; i++) drawButton(i);
}

static int hit(int x, int y) {
  for (int i = 0; i < 12; i++) {
    if (x >= bx(i) && x < bx(i) + BW && y >= by(i) && y < by(i) + BH) return i;
  }
  return -1;
}

static void pressBtn(int i) {
  frame(bx(i), by(i), BW, BH, 4, 0xFFFF);
  if (BTNS[i].kind == K_KEY) hid_ble_key(BTNS[i].mod, (uint8_t)BTNS[i].code);
  else hid_ble_consumer(BTNS[i].code);
}
static void releaseBtn(int i) {
  if (BTNS[i].kind == K_KEY) hid_ble_key(0, 0);
  else hid_ble_consumer(0);
  frame(bx(i), by(i), BW, BH, 4, BTNS[i].color);
}

extern "C" void app_main(void) {
  lcdInit();
  touchInit();

  esp_err_t r = nvs_flash_init();
  if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  hid_ble_init("StreamDeck");

  bool ok = loadCal();
  if (ok) {
    fillRect(0, 0, W, H, 0x0000);
    fillRect(W / 2 - 30, H / 2 - 30, 60, 60, 0x07E0);   // touch within 1 s to recalibrate
    for (int i = 0; i < 100 && ok; i++) {
      int x, y;
      if (readRaw(x, y)) ok = false;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  if (!ok) {
    calibrate();
    saveCal();
  }

  bool lastConn = hid_ble_connected();
  drawUI(lastConn);

  int cur = -1, miss = 0;
  while (true) {
    bool c = hid_ble_connected();
    if (c != lastConn) {
      lastConn = c;
      drawStatus(c);
    }

    int x, y;
    if (getTouch(x, y)) {
      miss = 0;
      if (cur < 0) {
        int i = hit(x, y);
        if (i >= 0) { cur = i; pressBtn(i); }
      }
    } else if (cur >= 0) {
      if (++miss >= 3) { releaseBtn(cur); cur = -1; miss = 0; }
    }
    vTaskDelay(1);
  }
}