#pragma once
#include <atomic>
#include "app_model.hpp"
#include "ble_protocol.hpp"
#include "storage/storage.hpp"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_npl.h"
#include "freertos/task.h"

namespace nexus {
enum class Channel : uint8_t { Commands = 0, Telemetry = 1, Responses = 2, Profiles = 3, Status = 4 };
class BleManager {
public:
    explicit BleManager(Storage& storage) : storage_(storage) {}
    esp_err_t initialize();
    uint32_t action(const char* profile, const char* button, bool long_press);
    uint32_t volume(int value);
    bool save_settings(const DeviceSettings& settings);
    bool select_profile(const char* profile);
    bool open_pairing();
    bool forget_pc();
    bool restart(bool factory_reset = false);
    bool connected() const { return connection_.load() != 0xffff; }
    bool ready() const { return ready_.load(); }
    bool encrypted() const { return encrypted_.load(); }
    bool bonded() const { return has_identity_.load(); }
    bool advertising() const { return advertising_.load(); }
    bool host_synced() const { return host_synced_.load(); }
    int radio_error() const { return radio_error_.load(); }
    uint32_t pairing_remaining() const;
private:
    struct Chunk { uint16_t connection = 0, size = 0; uint32_t generation = 0; Channel channel{}; uint8_t data[512]{}; };
    enum class RequestKind : uint8_t { Action, Volume, Settings, Select, Pair, Forget, Restart, Reset };
    struct Request {
        RequestKind kind{}; uint32_t sequence = 0; int value = 0; bool long_press = false;
        char profile[33]{}, button[33]{}; DeviceSettings settings{};
    };
    struct Pending {
        bool used = false; uint8_t attempts = 0; uint32_t sequence = 0;
        uint64_t sent_ms = 0; char json[384]{};
    };
    static BleManager* instance_;
    static int access(uint16_t connection, uint16_t handle, ble_gatt_access_ctxt* context, void* arg);
    static int gap(ble_gap_event* event, void* arg);
    static void sync();
    static void reset(int reason);
    static void retry_advertising(ble_npl_event* event);
    static void host_task(void* arg);
    static void worker_task(void* arg);
    void advertise();
    void worker();
    void incoming(Channel channel, uint8_t* data, size_t size);
    void request(const Request& request);
    bool notify(Channel channel, const char* json);
    void ack(const char* type, uint32_t sequence, bool success, const char* text);
    void invalid(const char* reason);
    void reset_session();
    bool authorize(const ble_gap_conn_desc& description);
    uint32_t next_sequence();
    Storage& storage_;
    QueueHandle_t incoming_ = nullptr, requests_ = nullptr;
    std::atomic<uint16_t> connection_{0xffff};
    std::atomic<bool> encrypted_{false}, ready_{false}, has_identity_{false};
    std::atomic<bool> advertising_{false}, host_synced_{false};
    std::atomic<int> radio_error_{0};
    ble_npl_callout advertising_retry_{};
    TaskHandle_t host_task_handle_ = nullptr;
    std::atomic<uint32_t> generation_{0}, sequence_{0};
    std::atomic<uint64_t> pairing_until_{0}, connected_at_{0}, last_seen_{0};
    BondIdentity identity_{};
    char session_[33]{};
    uint8_t own_address_type_ = 0;
    uint16_t command_handle_ = 0, status_handle_ = 0, message_id_ = 0;
    std::atomic<bool> command_subscribed_{false}, status_subscribed_{false};
    uint32_t bad_count_ = 0, last_telemetry_sequence_ = 0;
    uint32_t last_profile_sequence_ = 0, last_profile_crc_ = 0;
    uint64_t bad_since_ = 0;
    Pending pending_[8]{};
};
}
