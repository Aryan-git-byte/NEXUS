// Test the real OTA parser, hashing, retry, failure and boot-health logic.
// Flash and reset APIs are explicitly simulated; this is not a hardware test.
#include "update/ota_manager.hpp"
#include "app_model.hpp"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
uint64_t clock_ms = 1000;
esp_partition_t factory{ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY,0x60000,0x400000,"factory"};
esp_partition_t slot0{ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0,0x660000,0x400000,"ota_0"};
esp_partition_t slot1{ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1,0xa60000,0x400000,"ota_1"};
esp_partition_t metadata{ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA,0x5a000,0x2000,"otadata"};
const esp_partition_t* running = &factory;
const esp_partition_t* selected = &factory;
esp_ota_img_states_t state = ESP_OTA_IMG_VALID;
esp_app_desc_t description{};
bool writing = false, write_error = false, end_error = false;
int begin_count = 0, write_count = 0, abort_count = 0, boot_count = 0, valid_count = 0;
std::vector<uint8_t> flash_bytes;
std::vector<nexus::UiEvent> feedback;
struct Reset {};
const char* session = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* transfer = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
void reset() {
    clock_ms = 1000; running = selected = &factory; state = ESP_OTA_IMG_VALID;
    writing = write_error = end_error = false;
    begin_count = write_count = abort_count = boot_count = valid_count = 0;
    flash_bytes.clear(); feedback.clear(); nexus::ui_events = reinterpret_cast<void*>(1);
}
std::vector<uint8_t> image() {
    std::vector<uint8_t> data(2048);
    esp_image_header_t header{}; header.magic = ESP_IMAGE_HEADER_MAGIC;
    header.segment_count = 1; header.chip_id = ESP_CHIP_ID_ESP32S3;
    esp_image_segment_header_t segment{0x3c000020,static_cast<uint32_t>(data.size() - 32)};
    esp_app_desc_t app{}; app.magic_word = ESP_APP_DESC_MAGIC_WORD;
    std::strcpy(app.version,"1.3.0"); std::strcpy(app.project_name,"nexus_deck");
    std::memcpy(data.data(),&header,sizeof(header)); std::memcpy(data.data() + 24,&segment,sizeof(segment));
    std::memcpy(data.data() + 32,&app,sizeof(app));
    for (size_t i = 288; i < data.size(); ++i) data[i] = i % 251;
    return data;
}
std::string digest(const std::vector<uint8_t>& data) {
    uint8_t hash[32]; char output[65]{};
    check(mbedtls_sha256(data.data(),data.size(),hash,0) == 0,"Fixture SHA-256 failed");
    for (unsigned i = 0; i < 32; ++i) std::snprintf(output + i * 2,3,"%02x",hash[i]);
    return output;
}
Json request(const char* type) {
    Json root(cJSON_CreateObject(),cJSON_Delete);
    cJSON_AddStringToObject(root.get(),"type",type); cJSON_AddNumberToObject(root.get(),"sequence",7);
    cJSON_AddStringToObject(root.get(),"session",session); cJSON_AddStringToObject(root.get(),"transfer",transfer);
    return root;
}
Json begin(const std::vector<uint8_t>& data,const char* hash = nullptr) {
    auto root = request("ota_begin");
    cJSON_AddNumberToObject(root.get(),"size",data.size());
    cJSON_AddStringToObject(root.get(),"sha256",hash ? hash : digest(data).c_str());
    cJSON_AddStringToObject(root.get(),"version","1.3.0"); return root;
}
Json chunk(const std::vector<uint8_t>& data,size_t offset,size_t length) {
    auto root = request("ota_chunk"); cJSON_AddNumberToObject(root.get(),"offset",offset);
    std::vector<uint8_t> encoded((length + 2) / 3 * 4 + 1); size_t size;
    check(mbedtls_base64_encode(encoded.data(),encoded.size(),&size,data.data() + offset,length) == 0,"Fixture Base64 failed");
    cJSON_AddStringToObject(root.get(),"data",reinterpret_cast<char*>(encoded.data())); return root;
}
bool call(nexus::OtaManager& ota,const Json& message,uint32_t expected_offset = UINT32_MAX) {
    char output[512]; const bool result = ota.handle(message.get(),7,session,output,sizeof(output));
    Json reply(cJSON_Parse(output),cJSON_Delete); check(reply != nullptr,"OTA reply was not valid JSON");
    check(std::strcmp(cJSON_GetObjectItem(reply.get(),"status")->valuestring,result ? "success" : "error") == 0,
          "OTA success status disagreed with return value");
    if (expected_offset != UINT32_MAX)
        check(cJSON_GetObjectItem(reply.get(),"offset")->valuedouble == expected_offset,"Wrong OTA acknowledgement offset");
    return result;
}
void send_image(nexus::OtaManager& ota,const std::vector<uint8_t>& data) {
    for (size_t offset = 0; offset < data.size(); offset += 512)
        check(call(ota,chunk(data,offset,512),offset + 512),"A valid firmware chunk was rejected");
}
void test_success_and_retry() {
    reset(); const auto data = image(); nexus::OtaManager ota;
    check(call(ota,begin(data),0) && begin_count == 1,"Valid OTA begin failed");
    check(call(ota,begin(data),0) && begin_count == 1,"Duplicate begin wrote flash twice");
    check(call(ota,chunk(data,0,512),512),"First chunk failed");
    check(call(ota,chunk(data,0,512),512) && write_count == 1,"Duplicate chunk wrote flash twice");
    auto wrong = data; wrong[400] ^= 1;
    check(!call(ota,chunk(wrong,0,512)) && write_count == 1,"Conflicting duplicate was accepted");
    for (size_t offset = 512; offset < data.size(); offset += 512)
        check(call(ota,chunk(data,offset,512),offset + 512),"Firmware chunk failed");
    check(call(ota,request("ota_end")) && selected == &slot0 && boot_count == 1,"Verified image was not selected");
    check(call(ota,request("ota_end")) && boot_count == 1,"Duplicate end selected boot twice");
    check(!call(ota,request("ota_abort")),"Committed firmware was cancelled");
    check(flash_bytes == data,"Transfer changed image bytes");
    clock_ms += 1499; ota.poll(true); ++clock_ms;
    bool restarted = false; try { ota.poll(true); } catch (const Reset&) { restarted = true; }
    check(restarted,"Committed update did not restart");
    std::puts("PASS: OTA writes only the inactive slot, checks SHA-256 and retries without duplicate writes");
}
void test_invalid_messages() {
    reset(); const auto data = image(); nexus::OtaManager ota;
    auto oversized = begin(data); cJSON_SetNumberValue(cJSON_GetObjectItem(oversized.get(),"size"),0x400001);
    check(!call(ota,oversized) && begin_count == 0,"Oversized firmware started a flash write");
    auto extra = begin(data); cJSON_AddStringToObject(extra.get(),"command","unexpected");
    check(!call(ota,extra) && begin_count == 0,"Unexpected manifest field was accepted");
    check(call(ota,begin(data)),"Valid begin failed");
    auto bad = chunk(data,0,512); cJSON_ReplaceItemInObject(bad.get(),"offset",cJSON_CreateBool(false));
    check(!call(ota,bad) && write_count == 0,"Boolean offset was accepted");
    check(!call(ota,chunk(data,512,512)) && write_count == 0,"Out-of-order chunk was accepted");
    auto invalid_encoding = request("ota_chunk"); cJSON_AddNumberToObject(invalid_encoding.get(),"offset",0);
    cJSON_AddStringToObject(invalid_encoding.get(),"data","!bad!");
    check(!call(ota,invalid_encoding) && write_count == 0,"Invalid Base64 was accepted");
    auto wrong_chip = data; wrong_chip[12] = 0;
    check(!call(ota,chunk(wrong_chip,0,512)) && !ota.active() && write_count == 0,"Wrong chip was written to flash");
    check(selected == &factory && boot_count == 0,"Rejected update changed the boot image");
    std::puts("PASS: malformed manifests, bounds, offsets, Base64 and incompatible descriptors are rejected");
}
void test_failures_and_cancellation() {
    reset(); const auto data = image(); nexus::OtaManager ota;
    const std::string bad_digest(64,'0'); check(call(ota,begin(data,bad_digest.c_str())),"Begin failed");
    send_image(ota,data); check(!call(ota,request("ota_end")) && !ota.active() && boot_count == 0,"Hash mismatch was committed");
    check(call(ota,begin(data)),"Could not retry after bad hash"); send_image(ota,data); end_error = true;
    check(!call(ota,request("ota_end")) && boot_count == 0,"IDF image validation failure was ignored"); end_error = false;
    check(call(ota,begin(data)),"Could not retry after validation failure"); write_error = true;
    check(!call(ota,chunk(data,0,512)) && !ota.active() && boot_count == 0,"Flash failure was ignored"); write_error = false;
    check(call(ota,begin(data)),"Could not retry after flash failure");
    check(call(ota,request("ota_abort")) && !ota.active() && selected == &factory,"Cancellation changed the boot image");
    check(call(ota,begin(data)),"Could not retry after cancellation"); ota.abort("Disconnected");
    check(!ota.active() && selected == &factory,"Disconnect changed the current firmware");
    check(call(ota,begin(data)),"Could not retry after disconnect"); clock_ms += 120001; ota.poll(true);
    check(!ota.active() && selected == &factory,"Timeout changed the current firmware");
    std::puts("PASS: corruption, flash errors, cancel, disconnect and timeout retain current firmware");
}
void test_boot_health() {
    reset(); running = &slot0; state = ESP_OTA_IMG_PENDING_VERIFY;
    nexus::OtaManager ota; ota.poll(true); clock_ms += 10000; ota.poll(false);
    clock_ms += 1000; ota.poll(true); clock_ms += 19999; ota.poll(true);
    check(valid_count == 0,"Boot was confirmed without 20 continuous healthy seconds");
    ++clock_ms; ota.poll(true); check(valid_count == 1 && nexus::OtaManager::boot_confirmed(),"Healthy boot was not confirmed");
    reset(); running = &slot1; state = ESP_OTA_IMG_PENDING_VERIFY; nexus::OtaManager failed;
    failed.poll(false); clock_ms += 45001;
    bool restarted = false; try { failed.poll(false); } catch (const Reset&) { restarted = true; }
    check(restarted && valid_count == 0,"Unhealthy pending firmware was not restarted for rollback");
    std::puts("PASS: new firmware requires continuous UI/radio health; unhealthy pending boot restarts for rollback");
}
}

int64_t esp_timer_get_time() { return static_cast<int64_t>(clock_ms * 1000); }
int xQueueSend(QueueHandle_t,const void* value,unsigned) { feedback.push_back(*static_cast<const nexus::UiEvent*>(value)); return pdTRUE; }
const esp_partition_t* esp_partition_find_first(esp_partition_type_t type,esp_partition_subtype_t subtype,const char*) {
    for (const auto* part : {&factory,&slot0,&slot1,&metadata}) if (part->type == type && part->subtype == subtype) return part;
    return nullptr;
}
const esp_partition_t* esp_ota_get_running_partition() { return running; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return running == &slot0 ? &slot1 : &slot0; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t* output) { *output = state; return ESP_OK; }
esp_err_t esp_ota_begin(const esp_partition_t* partition,size_t size,uint32_t* handle) {
    check(partition != running && size == OTA_WITH_SEQUENTIAL_WRITES,"OTA did not use sequential writes to an inactive slot");
    ++begin_count; *handle = 7; writing = true; flash_bytes.clear(); return ESP_OK;
}
esp_err_t esp_ota_write(uint32_t handle,const void* data,size_t size) {
    check(handle == 7 && writing,"OTA wrote an invalid handle"); if (write_error) return ESP_ERR_INVALID_STATE;
    ++write_count; const auto* bytes = static_cast<const uint8_t*>(data); flash_bytes.insert(flash_bytes.end(),bytes,bytes + size); return ESP_OK;
}
esp_err_t esp_ota_abort(uint32_t) { writing = false; ++abort_count; return ESP_OK; }
esp_err_t esp_ota_end(uint32_t) { writing = false; return end_error ? ESP_ERR_INVALID_SIZE : ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition) { check(!writing,"Image was committed before esp_ota_end"); selected = partition; ++boot_count; return ESP_OK; }
esp_err_t esp_ota_mark_app_valid_cancel_rollback() { state = ESP_OTA_IMG_VALID; ++valid_count; return ESP_OK; }
extern "C" const esp_app_desc_t* esp_app_get_description() { return &description; }
void esp_restart() { throw Reset{}; }
int main() {
    try { test_success_and_retry(); test_invalid_messages(); test_failures_and_cancellation(); test_boot_health(); }
    catch (const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
    return 0;
}
