#include "ble_manager.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include "esp_random.h"
#include "esp_system.h"
#include "assets/icon_data.hpp"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

extern "C" void ble_store_config_init(void);
namespace nexus {
BleManager* BleManager::instance_ = nullptr;
namespace {
const ble_uuid128_t service_uuid = BLE_UUID128_INIT(0x01,0x00,0x12,0x8c,0xf6,0xa4,0x10,0x9e,0x7d,0x4e,0x9b,0x7b,0x00,0x00,0xc2,0xb7);
const ble_uuid128_t characteristic_uuids[] = {
    BLE_UUID128_INIT(0x01,0x00,0x12,0x8c,0xf6,0xa4,0x10,0x9e,0x7d,0x4e,0x9b,0x7b,0x01,0x00,0xc2,0xb7),
    BLE_UUID128_INIT(0x01,0x00,0x12,0x8c,0xf6,0xa4,0x10,0x9e,0x7d,0x4e,0x9b,0x7b,0x02,0x00,0xc2,0xb7),
    BLE_UUID128_INIT(0x01,0x00,0x12,0x8c,0xf6,0xa4,0x10,0x9e,0x7d,0x4e,0x9b,0x7b,0x03,0x00,0xc2,0xb7),
    BLE_UUID128_INIT(0x01,0x00,0x12,0x8c,0xf6,0xa4,0x10,0x9e,0x7d,0x4e,0x9b,0x7b,0x04,0x00,0xc2,0xb7),
    BLE_UUID128_INIT(0x01,0x00,0x12,0x8c,0xf6,0xa4,0x10,0x9e,0x7d,0x4e,0x9b,0x7b,0x05,0x00,0xc2,0xb7)};
ble_gatt_chr_def characteristics[6]{};
ble_gatt_svc_def services[2]{};
bool type_is(const cJSON* root, const char* text) {
    const auto* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    return cJSON_IsString(type) && std::strcmp(type->valuestring, text) == 0;
}
}

esp_err_t BleManager::initialize() {
    instance_ = this;
    has_identity_ = storage_.load_identity(identity_) == ESP_OK;
    if (!has_identity_) pairing_until_ = now_ms() + config::kPairingSeconds * 1000;
    incoming_ = xQueueCreate(12, sizeof(Chunk)); requests_ = xQueueCreate(12, sizeof(Request));
    if (!incoming_ || !requests_) return ESP_ERR_NO_MEM;
    esp_err_t error = nimble_port_init();
    if (error != ESP_OK) return error;
    ble_hs_cfg.sync_cb = sync;
    ble_hs_cfg.reset_cb = reset;
    ble_npl_callout_init(&advertising_retry_,nimble_port_get_dflt_eventq(),retry_advertising,nullptr);
    // Do not use status_rr: it can evict the owner's bond when an unknown peer fills storage.
    ble_hs_cfg.store_status_cb = nullptr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1; ble_hs_cfg.sm_mitm = 1; ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_store_config_init();
    for (size_t i = 0; i < 5; ++i) {
        auto& characteristic = characteristics[i];
        characteristic.uuid = &characteristic_uuids[i].u;
        characteristic.access_cb = access;
        characteristic.arg = reinterpret_cast<void*>(i);
        characteristic.min_key_size = 16;
        characteristic.flags = i == 0 ? BLE_GATT_CHR_F_NOTIFY :
            BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN;
        if (i == 0 || i == 4) {
            characteristic.flags |= BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN;
            characteristic.val_handle = i == 0 ? &command_handle_ : &status_handle_;
        }
        if (i == 4) characteristic.flags |= BLE_GATT_CHR_F_NOTIFY;
    }
    services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[0].uuid = &service_uuid.u; services[0].characteristics = characteristics;
    if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0) return ESP_FAIL;
    ble_svc_gap_device_name_set(config::kName);
    ble_att_set_preferred_mtu(247);
    if (xTaskCreatePinnedToCore(worker_task, "nexus_ble_io", 10240, this, 3, nullptr, 0) != pdPASS) return ESP_ERR_NO_MEM;
    // IDF's convenience wrapper ignores xTaskCreate's result. Check it here so
    // a missing host task cannot leave a working screen with a silent radio.
    if (xTaskCreatePinnedToCore(host_task,"nexus_ble_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,
        this,configMAX_PRIORITIES - 4,&host_task_handle_,CONFIG_BT_NIMBLE_PINNED_TO_CORE) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

int BleManager::access(uint16_t connection, uint16_t, ble_gatt_access_ctxt* context, void* arg) {
    auto& self = *instance_;
    const auto channel = static_cast<Channel>(reinterpret_cast<uintptr_t>(arg));
    if (connection != self.connection_ || !self.encrypted_) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char status[256];
        const int count = std::snprintf(status, sizeof(status),
            "{\"name\":\"NEXUS DECK\",\"firmware\":\"%s\",\"protocol\":1,\"icon_assets\":true,\"local_apps\":true,\"timer_alerts\":true,\"max_message\":49152,\"ready\":%s,\"mtu\":%u}",
            config::kVersion, self.ready_ ? "true" : "false", ble_att_mtu(connection));
        return os_mbuf_append(context->om, status, count) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR || channel == Channel::Commands) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    Chunk chunk{};
    chunk.connection = connection; chunk.generation = self.generation_; chunk.channel = channel;
    uint16_t size = OS_MBUF_PKTLEN(context->om);
    if (size <= wire::kHeaderSize || size > sizeof(chunk.data)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(context->om, chunk.data, sizeof(chunk.data), &chunk.size) != 0)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    return xQueueSend(self.incoming_, &chunk, 0) == pdTRUE ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

bool BleManager::authorize(const ble_gap_conn_desc& description) {
    const auto& state = description.sec_state;
    if (!state.encrypted || !state.authenticated || !state.bonded || state.key_size != 16) return false;
    BondIdentity peer{};
    peer.type = description.peer_id_addr.type;
    std::memcpy(peer.address, description.peer_id_addr.val, 6);
    if (has_identity_) return peer.type == identity_.type && std::memcmp(peer.address, identity_.address, 6) == 0;
    if (now_ms() > pairing_until_ || storage_.save_identity(peer) != ESP_OK) return false;
    identity_ = peer; has_identity_ = true; pairing_until_ = 0;
    return true;
}

int BleManager::gap(ble_gap_event* event, void*) {
    auto& self = *instance_;
    UiEvent ui{};
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        self.advertising_ = false;
        if (event->connect.status == 0) {
            self.connection_ = event->connect.conn_handle; ++self.generation_;
            self.encrypted_ = self.ready_ = false;
            self.connected_at_ = self.last_seen_ = now_ms();
            ble_gap_conn_desc description{};
            if (ble_gap_conn_find(self.connection_, &description) != 0 ||
                (self.has_identity_ && (description.peer_id_addr.type != self.identity_.type ||
                 std::memcmp(description.peer_id_addr.val, self.identity_.address, 6) != 0)) ||
                (!self.has_identity_ && now_ms() > self.pairing_until_)) {
                ble_gap_terminate(self.connection_, BLE_ERR_REM_USER_CONN_TERM); break;
            }
            ui.kind = UiEventKind::Link; ui.value = 1; post_ui(ui);
            ble_gap_security_initiate(self.connection_);
        } else self.advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        self.advertising_ = false;
        self.connection_ = 0xffff; self.encrypted_ = self.ready_ = false; ++self.generation_;
        self.command_subscribed_ = self.status_subscribed_ = false;
        ui.kind = UiEventKind::Link; ui.value = 0; post_ui(ui);
        self.advertise(); break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        ble_gap_conn_desc description{};
        bool accepted = event->enc_change.status == 0 && ble_gap_conn_find(event->enc_change.conn_handle, &description) == 0
                        && self.authorize(description);
        self.encrypted_ = accepted;
        if (!accepted) ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
        else { ui.kind = UiEventKind::Secure; ui.value = 1; post_ui(ui); }
        break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (self.has_identity_ || now_ms() > self.pairing_until_ || event->passkey.params.action != BLE_SM_IOACT_DISP) {
            ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_AUTH_FAIL); break;
        }
        ble_sm_io io{}; io.action = BLE_SM_IOACT_DISP; io.passkey = esp_random() % 1000000;
        ui.kind = UiEventKind::Passkey; ui.value = io.passkey;
        if (!post_ui(ui) || ble_sm_inject_io(event->passkey.conn_handle, &io) != 0)
            ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == self.command_handle_) self.command_subscribed_ = event->subscribe.cur_notify;
        if (event->subscribe.attr_handle == self.status_handle_) self.status_subscribed_ = event->subscribe.cur_notify;
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: self.advertising_ = false; self.advertise(); break;
    default: break;
    }
    return 0;
}

void BleManager::advertise() {
    if (!host_synced_ || connected()) return;
    if (ble_gap_adv_active()) { advertising_ = true; radio_error_ = 0; return; }
    auto failed = [this](int error) {
        advertising_ = false; radio_error_ = error;
        ble_npl_callout_reset(&advertising_retry_,ble_npl_time_ms_to_ticks32(1000));
    };
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = const_cast<ble_uuid128_t*>(&service_uuid); fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
    // A short primary name fits alongside the 128-bit UUID and is visible even
    // to passive scanners. The scan response carries the complete product name.
    fields.name = reinterpret_cast<const uint8_t*>("NEXUS"); fields.name_len = 5; fields.name_is_complete = 0;
    int error = ble_gap_adv_set_fields(&fields);
    if (error) { failed(error); return; }
    ble_hs_adv_fields response{};
    response.name = reinterpret_cast<const uint8_t*>(config::kName); response.name_len = std::strlen(config::kName);
    response.name_is_complete = 1;
    error = ble_gap_adv_rsp_set_fields(&response);
    if (error) { failed(error); return; }
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND; params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = 160; params.itvl_max = 240; params.channel_map = 7;
    error = ble_gap_adv_start(own_address_type_, nullptr, BLE_HS_FOREVER, &params, gap, nullptr);
    if (error) { failed(error); return; }
    advertising_ = true; radio_error_ = 0;
}
void BleManager::sync() {
    auto& self = *instance_;
    int error = ble_hs_util_ensure_addr(0);
    if (!error) error = ble_hs_id_infer_auto(0,&self.own_address_type_);
    self.radio_error_ = error;
    self.host_synced_ = error == 0;
    if (!error) self.advertise();
}
void BleManager::reset(int reason) {
    auto& self = *instance_;
    self.host_synced_ = self.advertising_ = false; self.radio_error_ = reason;
    self.connection_ = 0xffff; self.ready_ = self.encrypted_ = false; ++self.generation_;
    UiEvent event{}; event.kind = UiEventKind::Link; event.value = 0; post_ui(event);
}
void BleManager::retry_advertising(ble_npl_event*) {
    if (instance_->host_synced_) instance_->advertise();
}
void BleManager::host_task(void*) { nimble_port_run(); vTaskDelete(nullptr); }
void BleManager::worker_task(void* context) { static_cast<BleManager*>(context)->worker(); }

uint32_t BleManager::next_sequence() {
    uint32_t result = sequence_.fetch_add(1) + 1;
    if (result == 0) result = sequence_.fetch_add(1) + 1;
    return result;
}
uint32_t BleManager::action(const char* profile, const char* button, bool long_press) {
    if (!ready_ || !valid_identifier(profile) || !valid_identifier(button)) return 0;
    Request r{}; r.kind = RequestKind::Action; r.sequence = next_sequence(); r.long_press = long_press;
    std::snprintf(r.profile, sizeof(r.profile), "%s", profile); std::snprintf(r.button, sizeof(r.button), "%s", button);
    return xQueueSend(requests_, &r, 0) == pdTRUE ? r.sequence : 0;
}
uint32_t BleManager::volume(int value) {
    if (!ready_ || value < 0 || value > 100) return 0;
    Request r{}; r.kind = RequestKind::Volume; r.sequence = next_sequence(); r.value = value;
    return xQueueSend(requests_, &r, 0) == pdTRUE ? r.sequence : 0;
}
bool BleManager::save_settings(const DeviceSettings& settings) {
    Request r{}; r.kind = RequestKind::Settings; r.settings = settings;
    return xQueueSend(requests_, &r, 0) == pdTRUE;
}
uint32_t BleManager::timer_alert(apps::TimerEvent event) {
    if (!ready_ || event == apps::TimerEvent::None || static_cast<unsigned>(event) > 3) return 0;
    Request r{}; r.kind = RequestKind::TimerAlert; r.sequence = next_sequence();
    r.value = static_cast<int>(event);
    return xQueueSend(requests_,&r,0) == pdTRUE ? r.sequence : 0;
}
bool BleManager::save_apps(const apps::Preferences& preferences) {
    if (!apps::valid_preferences(preferences)) return false;
    Request r{}; r.kind = RequestKind::Apps; r.apps = preferences;
    return xQueueSend(requests_,&r,0) == pdTRUE;
}
bool BleManager::select_profile(const char* profile) {
    if (!valid_identifier(profile)) return false;
    Request r{}; r.kind = RequestKind::Select; r.sequence = next_sequence();
    std::snprintf(r.profile, sizeof(r.profile), "%s", profile);
    return xQueueSend(requests_, &r, 0) == pdTRUE;
}
bool BleManager::open_pairing() { Request r{}; r.kind = RequestKind::Pair; return xQueueSend(requests_, &r, 0) == pdTRUE; }
bool BleManager::forget_pc() { Request r{}; r.kind = RequestKind::Forget; return xQueueSend(requests_, &r, 0) == pdTRUE; }
bool BleManager::restart(bool reset) {
    Request r{}; r.kind = reset ? RequestKind::Reset : RequestKind::Restart;
    return xQueueSend(requests_, &r, 0) == pdTRUE;
}
uint32_t BleManager::pairing_remaining() const {
    uint64_t until = pairing_until_, now = now_ms(); return until > now ? (until - now + 999) / 1000 : 0;
}

bool BleManager::notify(Channel channel, const char* json) {
    const uint16_t connection = connection_;
    if (!encrypted_ || connection == 0xffff || !json) return false;
    if (channel == Channel::Commands ? !command_subscribed_ : !status_subscribed_) return false;
    const uint16_t handle = channel == Channel::Commands ? command_handle_ : status_handle_;
    const size_t total = std::strlen(json), capacity = std::clamp<int>(ble_att_mtu(connection) - 3, 20, 512);
    if (total > 512) return false;
    uint8_t packet[512];
    uint16_t id = ++message_id_; if (!id) id = ++message_id_;
    uint32_t crc = wire::crc32(reinterpret_cast<const uint8_t*>(json), total);
    for (size_t offset = 0; offset < total;) {
        size_t size = wire::frame(packet, capacity, reinterpret_cast<const uint8_t*>(json), total, id, offset, crc);
        bool sent = false;
        for (int attempt = 0; attempt < 24 && !sent; ++attempt) {
            if (connection != connection_ || !encrypted_) return false;
            os_mbuf* buffer = ble_hs_mbuf_from_flat(packet, size);
            if (buffer && ble_gatts_notify_custom(connection, handle, buffer) == 0) sent = true;
            if (!sent) vTaskDelay(pdMS_TO_TICKS(4));
        }
        if (!sent) return false;
        offset += size - wire::kHeaderSize;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}
void BleManager::ack(const char* type, uint32_t seq, bool success, const char* text) {
    // Error strings originate in this firmware, never from untrusted JSON.
    char json[384];
    std::snprintf(json, sizeof(json), "{\"type\":\"%s\",\"sequence\":%lu,\"session\":\"%s\",\"status\":\"%s\",\"message\":\"%s\"}",
                  type, static_cast<unsigned long>(seq), session_, success ? "success" : "error", text);
    notify(Channel::Status, json);
}
void BleManager::invalid(const char* reason) {
    uint64_t now = now_ms();
    if (now - bad_since_ > 10000) { bad_since_ = now; bad_count_ = 0; }
    if (++bad_count_ > 12 && connected()) ble_gap_terminate(connection_, BLE_ERR_REM_USER_CONN_TERM);
    UiEvent event{}; event.kind = UiEventKind::Notice;
    std::snprintf(event.text, sizeof(event.text), "%s", reason); post_ui(event);
}
void BleManager::reset_session() {
    session_[0] = 0; ready_ = false; last_telemetry_sequence_ = 0;
    last_profile_sequence_ = last_profile_crc_ = 0;
    for (auto& p : pending_) if (p.used) {
        UiEvent event{}; event.kind = UiEventKind::Response; event.sequence = p.sequence; event.value = 0;
        std::snprintf(event.text,sizeof(event.text),"PC disconnected"); post_ui(event); p.used = false;
    }
}

void BleManager::incoming(Channel channel, uint8_t* data, size_t size) {
    data[size] = 0;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(parse_json(data, size), cJSON_Delete);
    uint32_t seq;
    if (!root || !valid_sequence(root.get(), seq)) { invalid("Rejected malformed message"); return; }
    if (channel == Channel::Status && type_is(root.get(), "hello")) {
        const char* const fields[] = {"type","protocol","sequence","session","pc_name","epoch","utc_offset"};
        const auto* protocol = cJSON_GetObjectItemCaseSensitive(root.get(), "protocol");
        char pc[64];
        int64_t epoch; int32_t utc_offset;
        if (!valid_session(root.get()) || !json_fields(root.get(), fields, 7) ||
            !cJSON_IsNumber(protocol) || protocol->valuedouble != 1 || !json_text(root.get(), "pc_name", pc, sizeof(pc)) ||
            !json_clock(root.get(),epoch,utc_offset) ||
            !status_subscribed_ || !command_subscribed_) { invalid("Rejected handshake"); return; }
        const char* incoming_session = cJSON_GetObjectItemCaseSensitive(root.get(),"session")->valuestring;
        if (session_[0] && std::strcmp(session_, incoming_session) != 0) { invalid("Session already established"); return; }
        std::snprintf(session_, sizeof(session_), "%s", incoming_session);
        last_seen_ = now_ms(); ready_ = true;
        UiEvent event{}; event.kind = UiEventKind::Ready; event.value = 1;
        std::snprintf(event.text, sizeof(event.text), "%s", pc); post_ui(event);
        ack("hello_ack",seq,true,"Encrypted session established"); return;
    }
    if (!ready_ || !valid_session(root.get(), session_)) { invalid("Rejected stale session"); return; }
    if (channel == Channel::Telemetry && type_is(root.get(),"telemetry")) {
        Telemetry telemetry;
        if (seq <= last_telemetry_sequence_ || !parse_telemetry(root.get(), telemetry)) { invalid("Rejected telemetry"); return; }
        last_telemetry_sequence_ = seq; last_seen_ = now_ms();
        xQueueOverwrite(telemetry_mailbox, &telemetry);
    } else if (channel == Channel::Responses && type_is(root.get(),"response")) {
        const char* const fields[] = {"type","sequence","session","status","message"};
        char status[16], message[176];
        if (!json_fields(root.get(),fields,5) || !json_text(root.get(),"status",status,sizeof(status)) ||
            (std::strcmp(status,"success") != 0 && std::strcmp(status,"error") != 0) ||
            !json_text(root.get(),"message",message,sizeof(message),true)) { invalid("Rejected action response"); return; }
        last_seen_ = now_ms();
        for (auto& pending : pending_) if (pending.used && pending.sequence == seq) {
            pending.used = false; UiEvent event{}; event.kind = UiEventKind::Response;
            event.sequence = seq; event.value = std::strcmp(status,"success") == 0;
            std::snprintf(event.text,sizeof(event.text),"%s",message); post_ui(event); break;
        }
    } else if (channel == Channel::Profiles && type_is(root.get(),"icon_sync")) {
        const char* const fields[] = {"type","sequence","session","asset","data"};
        char id[15];
        const auto* encoded = cJSON_GetObjectItemCaseSensitive(root.get(),"data");
        if (!json_fields(root.get(),fields,5) || !json_text(root.get(),"asset",id,sizeof(id)) ||
            !icons::valid_id(id) || !cJSON_IsString(encoded)) { ack("icon_ack",seq,false,"Invalid icon fields"); return; }
        const size_t encoded_size = std::strlen(encoded->valuestring);
        if (encoded_size > ((icons::kMaxBytes + 2) / 3) * 4) { ack("icon_ack",seq,false,"Icon is too large"); return; }
        auto decoded = std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[icons::kMaxBytes]);
        size_t decoded_size = 0;
        icons::IconData image;
        if (!decoded || mbedtls_base64_decode(decoded.get(),icons::kMaxBytes,&decoded_size,
            reinterpret_cast<const uint8_t*>(encoded->valuestring),encoded_size) != 0 ||
            !icons::IconData::parse(decoded.get(),decoded_size,image)) { ack("icon_ack",seq,false,"Invalid icon pixels"); return; }
        uint8_t digest[32]; char expected[15];
        if (mbedtls_sha256(decoded.get(),decoded_size,digest,0) != 0) { ack("icon_ack",seq,false,"Icon hash failed"); return; }
        for (unsigned i = 0; i < 7; ++i) std::snprintf(expected + i * 2,3,"%02x",digest[i]);
        if (std::strcmp(expected,id) != 0) { ack("icon_ack",seq,false,"Icon content ID does not match"); return; }
        last_seen_ = now_ms();
        const esp_err_t saved = storage_.save_icon(id,decoded.get(),decoded_size);
        ack("icon_ack",seq,saved == ESP_OK,saved == ESP_OK ? "Artwork saved" : "Icon storage is full or unavailable");
    } else if (channel == Channel::Profiles && type_is(root.get(),"profile_sync")) {
        last_seen_ = now_ms();
        const uint32_t crc = wire::crc32(data,size);
        if (seq <= last_profile_sequence_) {
            const bool retry = seq == last_profile_sequence_ && crc == last_profile_crc_;
            ack("profile_ack",seq,retry,retry ? "Profiles already saved" : "Expired or conflicting profile request");
            return;
        }
        auto candidate = std::unique_ptr<ProfileSet>(new(std::nothrow) ProfileSet); char error[96];
        if (!candidate) { ack("profile_ack",seq,false,"Not enough memory to synchronize profiles"); return; }
        if (!parse_profiles(root.get(), *candidate, error, sizeof(error))) { ack("profile_ack",seq,false,error); return; }
        for (unsigned p = 0; p < candidate->count; ++p)
            for (unsigned b = 0; b < candidate->profiles[p].count; ++b) {
                const auto& button = candidate->profiles[p].buttons[b];
                if (button.icon_asset[0] && !storage_.has_icon(button.icon_asset)) {
                    ack("profile_ack",seq,false,"Send the button artwork before syncing profiles"); return;
                }
            }
        if (storage_.save_profiles(reinterpret_cast<char*>(data)) != ESP_OK) { ack("profile_ack",seq,false,"NVS profile save failed"); return; }
        UiEvent event{}; event.kind = UiEventKind::Profiles; event.profiles = candidate.get();
        if (!post_ui(event)) { ack("profile_ack",seq,false,"UI busy; retry synchronization"); return; }
        candidate.release(); last_profile_sequence_ = seq; last_profile_crc_ = crc;
        storage_.purge_icons(*event.profiles);
        ack("profile_ack",seq,true,"Profiles validated and saved");
    } else invalid("Rejected message type or characteristic");
}

void BleManager::request(const Request& r) {
    UiEvent event{}; event.kind = UiEventKind::Notice;
    switch (r.kind) {
    case RequestKind::Apps:
        event.kind = UiEventKind::AppsSaved;
        event.value = storage_.save_apps(r.apps) == ESP_OK;
        post_ui(event); break;
    case RequestKind::Settings:
        if (storage_.save_settings(r.settings) != ESP_OK) {
            std::snprintf(event.text,sizeof(event.text),"Settings could not be saved"); post_ui(event);
        }
        break;
    case RequestKind::Pair:
        if (host_synced_) ble_npl_callout_reset(&advertising_retry_,ble_npl_time_ms_to_ticks32(1));
        if (has_identity_) std::snprintf(event.text,sizeof(event.text),"Forget the saved PC before pairing another");
        else { pairing_until_ = now_ms() + config::kPairingSeconds * 1000;
               std::snprintf(event.text,sizeof(event.text),"Pairing open for 120 seconds"); }
        post_ui(event); break;
    case RequestKind::Forget:
        if (connected()) ble_gap_terminate(connection_, BLE_ERR_REM_USER_CONN_TERM);
        if (ble_store_clear() != 0 || storage_.forget_identity() != ESP_OK) {
            std::snprintf(event.text,sizeof(event.text),"Could not clear PC bond"); post_ui(event);
        } else { has_identity_ = false; pairing_until_ = now_ms() + config::kPairingSeconds * 1000;
                 std::snprintf(event.text,sizeof(event.text),"PC forgotten; remove its Windows pairing too"); post_ui(event); }
        break;
    case RequestKind::Restart: vTaskDelay(pdMS_TO_TICKS(100)); esp_restart(); break;
    case RequestKind::Reset:
        if (storage_.factory_reset() == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(100)); esp_restart(); }
        else { std::snprintf(event.text,sizeof(event.text),"Factory reset failed"); post_ui(event); }
        break;
    case RequestKind::Select: {
        if (!ready_) break;
        char json[224];
        std::snprintf(json,sizeof(json),"{\"type\":\"profile_select\",\"sequence\":%lu,\"session\":\"%s\",\"profile\":\"%s\"}",
                      static_cast<unsigned long>(r.sequence),session_,r.profile);
        notify(Channel::Commands,json); break;
    }
    case RequestKind::Action:
    case RequestKind::Volume:
    case RequestKind::TimerAlert: {
        Pending* free = nullptr;
        for (auto& p : pending_) if (!p.used) { free = &p; break; }
        if (!ready_ || !free) {
            event.kind = UiEventKind::Response; event.sequence = r.sequence; event.value = 0;
            std::snprintf(event.text,sizeof(event.text),"PC unavailable or action queue full"); post_ui(event); break;
        }
        if (r.kind == RequestKind::TimerAlert) {
            const char* kinds[] = {"","countdown","focus","break"};
            std::snprintf(free->json,sizeof(free->json),"{\"type\":\"timer_alert\",\"kind\":\"%s\",\"sequence\":%lu,\"session\":\"%s\"}",
                          kinds[std::clamp(r.value,1,3)],static_cast<unsigned long>(r.sequence),session_);
        } else if (r.kind == RequestKind::Volume)
            std::snprintf(free->json,sizeof(free->json),"{\"type\":\"action\",\"id\":\"system_volume_set\",\"value\":%d,\"sequence\":%lu,\"session\":\"%s\"}",
                          r.value,static_cast<unsigned long>(r.sequence),session_);
        else
            std::snprintf(free->json,sizeof(free->json),"{\"type\":\"action\",\"id\":\"%s\",\"profile\":\"%s\",\"gesture\":\"%s\",\"sequence\":%lu,\"session\":\"%s\"}",
                          r.button,r.profile,r.long_press ? "long" : "press",static_cast<unsigned long>(r.sequence),session_);
        free->used = true; free->sequence = r.sequence; free->attempts = 1; free->sent_ms = now_ms();
        notify(Channel::Commands,free->json); break;
    }
    }
}

void BleManager::worker() {
    // Separate characteristic buffers permit telemetry and profile transfers to interleave safely.
    auto profile_buffer = std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[config::kMaxMessage + 1]);
    auto telemetry_buffer = std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[2049]);
    auto response_buffer = std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[1025]);
    auto status_buffer = std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[1025]);
    if (!profile_buffer || !telemetry_buffer || !response_buffer || !status_buffer) {
        UiEvent event{}; event.kind = UiEventKind::Notice;
        std::snprintf(event.text,sizeof(event.text),"BLE buffers unavailable. Restart the device."); post_ui(event);
        ready_ = false; vTaskDelete(nullptr); return;
    }
    wire::Reassembler profiles(profile_buffer.get(), config::kMaxMessage), telemetry(telemetry_buffer.get(), 2048);
    wire::Reassembler responses(response_buffer.get(), 1024), status(status_buffer.get(), 1024);
    wire::Reassembler* assemblers[] = {nullptr,&telemetry,&responses,&profiles,&status};
    uint32_t generation = generation_;
    while (true) {
        if (generation != generation_) {
            generation = generation_; reset_session();
            for (auto* a : assemblers) if (a) a->reset();
        }
        Chunk chunk;
        if (xQueueReceive(incoming_, &chunk, pdMS_TO_TICKS(10)) == pdTRUE && chunk.connection == connection_ &&
            chunk.generation == generation && encrypted_) {
            auto* assembler = assemblers[static_cast<unsigned>(chunk.channel)];
            if (assembler) {
                auto result = assembler->feed(chunk.data, chunk.size, now_ms());
                if (result != wire::FeedResult::Invalid) last_seen_ = now_ms();
                if (result == wire::FeedResult::Complete) {
                    incoming(chunk.channel, assembler->data(), assembler->size());
                } else if (result == wire::FeedResult::Invalid) invalid("Rejected BLE fragment");
            }
        }
        Request r;
        if (xQueueReceive(requests_, &r, 0) == pdTRUE) request(r);
        uint64_t now = now_ms();
        for (auto& pending : pending_) if (pending.used && now - pending.sent_ms >= 1600) {
            if (!ready_ || pending.attempts >= 3) {
                UiEvent event{}; event.kind = UiEventKind::Response; event.sequence = pending.sequence;
                std::snprintf(event.text,sizeof(event.text),"PC did not acknowledge this action"); post_ui(event); pending.used = false;
            } else { ++pending.attempts; pending.sent_ms = now; notify(Channel::Commands,pending.json); }
        }
        if (connected() && ((ready_ && now - last_seen_ > config::kHeartbeatTimeoutMs) ||
            (!ready_ && now - connected_at_ > 60000))) ble_gap_terminate(connection_, BLE_ERR_REM_USER_CONN_TERM);
        // A long fragmented write also counts as activity, before complete JSON is available.
        if (connected() && now - last_seen_ > 5000 && uxQueueMessagesWaiting(incoming_) > 0) last_seen_ = now;
    }
}
}
