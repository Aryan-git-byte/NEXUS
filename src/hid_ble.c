#include <string.h>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/util/util.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "store/config/ble_store_config.h"
#include "hid_ble.h"

// ESP-IDF NimBLE: implementation exists, public header omits declaration.
extern void ble_store_config_init(void);
static const uint8_t report_map[] = {
  // keyboard, report id 1
  0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
  0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
  0x95,0x01, 0x75,0x08, 0x81,0x01,
  0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65, 0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00,
  0xC0,
  // consumer control, report id 2
  0x05,0x0C, 0x09,0x01, 0xA1,0x01, 0x85,0x02,
  0x15,0x00, 0x26,0xFF,0x03, 0x19,0x00, 0x2A,0xFF,0x03, 0x75,0x10, 0x95,0x01, 0x81,0x00,
  0xC0
};
static const uint8_t hid_info[]  = {0x11, 0x01, 0x00, 0x02};
static const uint8_t ref_kb[]    = {1, 1};
static const uint8_t ref_cc[]    = {2, 1};
static const uint8_t pnp_id[]    = {0x02, 0x3A, 0x30, 0x01, 0x00, 0x01, 0x00};
static const uint8_t mfr_name[]  = "XolveTech";
static uint8_t battery           = 100;
static uint8_t protocol_mode     = 1;
static uint8_t kb_report[8];
static uint8_t cc_report[2];

struct blob { const uint8_t *d; uint16_t n; };
static const struct blob b_map  = { report_map, sizeof(report_map) };
static const struct blob b_info = { hid_info, sizeof(hid_info) };
static const struct blob b_rkb  = { ref_kb, 2 };
static const struct blob b_rcc  = { ref_cc, 2 };
static const struct blob b_pnp  = { pnp_id, sizeof(pnp_id) };
static const struct blob b_mfr  = { mfr_name, 9 };
static const struct blob b_bat  = { &battery, 1 };
static const struct blob b_pm   = { &protocol_mode, 1 };
static const struct blob b_kb   = { kb_report, 8 };
static const struct blob b_cc   = { cc_report, 2 };

static uint16_t kb_handle, cc_handle;
static volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t own_addr_type;
static char dev_name[32];

static void advertise(void);

static int access_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  const struct blob *b = (const struct blob *)arg;
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR || ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
    return os_mbuf_append(ctxt->om, b->d, b->n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  return 0;   // writes (control point, protocol mode) are accepted and ignored
}

static const struct ble_gatt_svc_def svcs[] = {
  { .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = BLE_UUID16_DECLARE(0x1812),
    .characteristics = (struct ble_gatt_chr_def[]) {
      { .uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = access_cb, .arg = (void *)&b_info,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
      { .uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = access_cb, .arg = (void *)&b_map,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
      { .uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = access_cb, .arg = (void *)&b_pm,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
      { .uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = access_cb, .arg = (void *)&b_pm,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
      { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access_cb, .arg = (void *)&b_kb,
        .descriptors = (struct ble_gatt_dsc_def[]) {
          { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
            .access_cb = access_cb, .arg = (void *)&b_rkb },
          { 0 } },
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &kb_handle },
      { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access_cb, .arg = (void *)&b_cc,
        .descriptors = (struct ble_gatt_dsc_def[]) {
          { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
            .access_cb = access_cb, .arg = (void *)&b_rcc },
          { 0 } },
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &cc_handle },
      { 0 } } },
  { .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = BLE_UUID16_DECLARE(0x180F),
    .characteristics = (struct ble_gatt_chr_def[]) {
      { .uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = access_cb, .arg = (void *)&b_bat,
        .flags = BLE_GATT_CHR_F_READ },
      { 0 } } },
  { .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = BLE_UUID16_DECLARE(0x180A),
    .characteristics = (struct ble_gatt_chr_def[]) {
      { .uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = access_cb, .arg = (void *)&b_mfr,
        .flags = BLE_GATT_CHR_F_READ },
      { .uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = access_cb, .arg = (void *)&b_pnp,
        .flags = BLE_GATT_CHR_F_READ },
      { 0 } } },
  { 0 }
};

static int gap_event(struct ble_gap_event *ev, void *arg) {
  switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (ev->connect.status == 0) {
        conn = ev->connect.conn_handle;
        ble_gap_security_initiate(conn);
      } else {
        conn = BLE_HS_CONN_HANDLE_NONE;
        advertise();
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      conn = BLE_HS_CONN_HANDLE_NONE;
      advertise();
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      advertise();
      break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      struct ble_gap_conn_desc d;
      ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d);
      ble_store_util_delete_peer(&d.peer_id_addr);
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
      break;
  }
  return 0;
}

static void advertise(void) {
  struct ble_hs_adv_fields f;
  struct ble_gap_adv_params ap;
  static ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);

  memset(&f, 0, sizeof(f));
  f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  f.name = (uint8_t *)dev_name;
  f.name_len = strlen(dev_name);
  f.name_is_complete = 1;
  f.appearance = 0x03C1;
  f.appearance_is_present = 1;
  f.uuids16 = &hid_uuid;
  f.num_uuids16 = 1;
  f.uuids16_is_complete = 1;
  ble_gap_adv_set_fields(&f);

  memset(&ap, 0, sizeof(ap));
  ap.conn_mode = BLE_GAP_CONN_MODE_UND;
  ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &ap, gap_event, NULL);
}

static void on_sync(void) {
  ble_hs_id_infer_auto(0, &own_addr_type);
  advertise();
}

static void host_task(void *p) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void hid_ble_init(const char *name) {
  strncpy(dev_name, name, sizeof(dev_name) - 1);
  nimble_port_init();

  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_gatts_count_cfg(svcs);
  ble_gatts_add_svcs(svcs);
  ble_svc_gap_device_name_set(dev_name);
  ble_svc_gap_device_appearance_set(0x03C1);
  ble_store_config_init();

  nimble_port_freertos_init(host_task);
}

bool hid_ble_connected(void) { return conn != BLE_HS_CONN_HANDLE_NONE; }

static void notify(uint16_t handle, const uint8_t *data, int len) {
  uint16_t c = conn;
  if (c == BLE_HS_CONN_HANDLE_NONE) return;
  struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
  if (om) ble_gatts_notify_custom(c, handle, om);
}

void hid_ble_key(uint8_t mod, uint8_t key) {
  memset(kb_report, 0, sizeof(kb_report));
  kb_report[0] = mod;
  kb_report[2] = key;
  notify(kb_handle, kb_report, 8);
}

void hid_ble_consumer(uint16_t usage) {
  cc_report[0] = usage & 0xFF;
  cc_report[1] = usage >> 8;
  notify(cc_handle, cc_report, 2);
}