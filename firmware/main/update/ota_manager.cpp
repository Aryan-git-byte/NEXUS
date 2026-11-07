#include "ota_manager.hpp"
#include "app_model.hpp"
#include "bluetooth/ble_protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

namespace nexus {
namespace {
bool hex(const char* value, size_t length) {
    if (!value || std::strlen(value) != length) return false;
    for (size_t i = 0; i < length; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}
bool integer(const cJSON* root, const char* name, uint32_t& result) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(root,name);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > UINT32_MAX || std::floor(item->valuedouble) != item->valuedouble) return false;
    result = static_cast<uint32_t>(item->valuedouble); return true;
}
const char* text(const cJSON* root,const char* name) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(root,name);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}
bool version_text(const char* value) {
    if (!value || !value[0] || std::strlen(value) > 31) return false;
    for (const char* p = value; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') || *p == '.' || *p == '-' || *p == '_')) return false;
    return true;
}
}
OtaManager::~OtaManager() { abort(); }
bool OtaManager::supported() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,nullptr) &&
           esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,nullptr) &&
           esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,nullptr);
}
const char* OtaManager::running_slot() {
    const auto* part = esp_ota_get_running_partition();
    return part ? part->label : "unknown";
}
bool OtaManager::boot_confirmed() {
    const auto* part = esp_ota_get_running_partition();
    if (!part) return false;
    if (part->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return true;
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(part,&state) == ESP_OK && state == ESP_OTA_IMG_VALID;
}
void OtaManager::elf_digest(char* output,size_t capacity) {
    const auto* app = esp_app_get_description();
    if (!app || capacity < 65) { if (capacity) output[0] = 0; return; }
    for (unsigned i = 0; i < 32; ++i) std::snprintf(output + i * 2,3,"%02x",app->app_elf_sha256[i]);
}
void OtaManager::feedback(int progress,const char* message,uint32_t phase) {
    UiEvent event{}; event.kind = UiEventKind::Firmware; event.value = progress; event.sequence = phase;
    std::snprintf(event.text,sizeof(event.text),"%s",message); post_ui(event);
}
void OtaManager::reply(char* output,size_t capacity,uint32_t sequence,const char* session,
                       bool success,const char* message) {
    std::snprintf(output,capacity,
        "{\"type\":\"ota_ack\",\"sequence\":%lu,\"session\":\"%s\",\"status\":\"%s\",\"message\":\"%s\",\"offset\":%lu,\"max_chunk\":4096,\"slot\":\"%s\",\"restarting\":%s}",
        static_cast<unsigned long>(sequence),session,success ? "success" : "error",message,
        static_cast<unsigned long>(offset_),slot_,committed_ ? "true" : "false");
}
void OtaManager::abort(const char* reason) {
    const bool transferring = active_;
    if (active_) esp_ota_abort(handle_);
    active_ = false;
    if (hash_) {
        auto* context = static_cast<mbedtls_sha256_context*>(hash_);
        mbedtls_sha256_free(context); std::free(context); hash_ = nullptr;
    }
    if (reason && transferring && !committed_) feedback(-1,reason,3);
}
bool OtaManager::first_block(const uint8_t* bytes,size_t length) const {
    if (length < sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) return false;
    esp_image_header_t header; std::memcpy(&header,bytes,sizeof(header));
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != 9 || !header.segment_count || header.segment_count > 16) return false;
    esp_app_desc_t app;
    std::memcpy(&app,bytes + sizeof(header) + sizeof(esp_image_segment_header_t),sizeof(app));
    return app.magic_word == ESP_APP_DESC_MAGIC_WORD &&
           std::memchr(app.project_name,0,sizeof(app.project_name)) &&
           std::memchr(app.version,0,sizeof(app.version)) &&
           std::strcmp(app.project_name,"nexus_deck") == 0 && std::strcmp(app.version,version_) == 0;
}
bool OtaManager::handle(const cJSON* root,uint32_t sequence,const char* session,char* output,size_t capacity) {
    const char* type = text(root,"type"), *id = text(root,"transfer");
    auto reject = [&](const char* message) { reply(output,capacity,sequence,session,false,message); return false; };
    if (!type || !hex(id,32)) return reject("Invalid firmware transfer ID");
    if (std::strcmp(type,"ota_begin") == 0) {
        const char* fields[] = {"type","sequence","session","transfer","size","sha256","version"};
        uint32_t size; const char* digest = text(root,"sha256"), *version = text(root,"version");
        if (!json_fields(root,fields,7) || !integer(root,"size",size) || size < 512 ||
            !hex(digest,64) || !version_text(version)) return reject("Invalid firmware manifest");
        if (active_ || committed_) {
            if (std::strcmp(id,transfer_) || size != size_ || std::strcmp(digest,sha256_) || std::strcmp(version,version_))
                return reject("Another firmware transfer is active");
            activity_ = now_ms(); reply(output,capacity,sequence,session,true,"Firmware transfer already started"); return true;
        }
        if (!supported() || !boot_confirmed()) return reject("Firmware OTA partitions or boot validation are unavailable");
        const auto* part = esp_ota_get_next_update_partition(nullptr);
        const auto* running = esp_ota_get_running_partition();
        if (!part || !running || part->address == running->address || size > part->size ||
            part->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN || part->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MAX)
            return reject("Firmware does not fit an inactive OTA slot");
        hash_ = std::calloc(1,sizeof(mbedtls_sha256_context));
        if (!hash_) return reject("Not enough memory for a firmware update");
        auto* context = static_cast<mbedtls_sha256_context*>(hash_); mbedtls_sha256_init(context);
        if (mbedtls_sha256_starts(context,0) != 0 || esp_ota_begin(part,OTA_WITH_SEQUENTIAL_WRITES,&handle_) != ESP_OK) {
            abort(); return reject("Could not start writing the inactive OTA slot");
        }
        active_ = true; committed_ = false; partition_ = part; size_ = size; offset_ = 0;
        previous_size_ = 0; activity_ = now_ms();
        std::snprintf(transfer_,sizeof(transfer_),"%s",id); std::snprintf(sha256_,sizeof(sha256_),"%s",digest);
        std::snprintf(version_,sizeof(version_),"%s",version); std::snprintf(slot_,sizeof(slot_),"%s",part->label);
        feedback(0,"Receiving firmware over Bluetooth");
        reply(output,capacity,sequence,session,true,"Inactive OTA slot ready"); return true;
    }
    if (std::strcmp(id,transfer_)) return reject("Firmware transfer ID does not match");
    if (std::strcmp(type,"ota_abort") == 0) {
        const char* fields[] = {"type","sequence","session","transfer"};
        if (!json_fields(root,fields,4) || committed_) return reject("The update has already been committed");
        abort("Firmware update cancelled"); reply(output,capacity,sequence,session,true,"Firmware update cancelled"); return true;
    }
    if (std::strcmp(type,"ota_end") == 0) {
        const char* fields[] = {"type","sequence","session","transfer"};
        if (!json_fields(root,fields,4)) return reject("Invalid firmware completion message");
        if (committed_) { reply(output,capacity,sequence,session,true,"Firmware verified; restarting"); return true; }
        if (!active_ || offset_ != size_) return reject("Firmware is incomplete");
        uint8_t digest[32]; char digest_text[65]{};
        if (mbedtls_sha256_finish(static_cast<mbedtls_sha256_context*>(hash_),digest) != 0) {
            abort("Firmware hash failed"); return reject("Firmware hash failed");
        }
        for (unsigned i = 0; i < 32; ++i) std::snprintf(digest_text + i * 2,3,"%02x",digest[i]);
        if (std::strcmp(digest_text,sha256_)) { abort("Firmware checksum did not match"); return reject("Firmware checksum did not match"); }
        feedback(100,"Verifying firmware image");
        const esp_err_t verified = esp_ota_end(handle_); active_ = false; abort();
        if (verified != ESP_OK) { feedback(-1,"Firmware image was rejected",3); return reject("Firmware image was rejected"); }
        if (esp_ota_set_boot_partition(static_cast<const esp_partition_t*>(partition_)) != ESP_OK) {
            feedback(-1,"Could not select the new boot slot",3); return reject("Could not select the new boot slot");
        }
        committed_ = true; restart_at_ = now_ms() + 1500;
        feedback(100,"Firmware verified. Restarting the deck.",2);
        reply(output,capacity,sequence,session,true,"Firmware verified; restarting"); return true;
    }
    if (std::strcmp(type,"ota_chunk") != 0 || !active_) return reject("No active firmware transfer");
    const char* fields[] = {"type","sequence","session","transfer","offset","data"};
    uint32_t offset; const char* encoded = text(root,"data");
    if (!json_fields(root,fields,6) || !integer(root,"offset",offset) || !encoded ||
        std::strlen(encoded) > (kChunkBytes + 2) / 3 * 4) return reject("Invalid firmware chunk");
    size_t length = 0;
    if (mbedtls_base64_decode(buffer_.data(),buffer_.size(),&length,
        reinterpret_cast<const uint8_t*>(encoded),std::strlen(encoded)) != 0 || !length)
        return reject("Invalid firmware chunk encoding");
    const uint32_t crc = wire::crc32(buffer_.data(),length);
    if (previous_size_ && offset == previous_offset_ && length == previous_size_ && crc == previous_crc_) {
        activity_ = now_ms(); reply(output,capacity,sequence,session,true,"Firmware chunk already received"); return true;
    }
    if (offset != offset_ || length > size_ - offset_) return reject("Unexpected firmware chunk offset or length");
    if (!offset_ && !first_block(buffer_.data(),length)) {
        abort("Select a NEXUS DECK ESP32-S3 application image"); return reject("Select a NEXUS DECK ESP32-S3 application image");
    }
    if (esp_ota_write(handle_,buffer_.data(),length) != ESP_OK ||
        mbedtls_sha256_update(static_cast<mbedtls_sha256_context*>(hash_),buffer_.data(),length) != 0) {
        abort("Firmware write failed"); return reject("Firmware write failed");
    }
    previous_offset_ = offset; previous_size_ = length; previous_crc_ = crc;
    offset_ += length; activity_ = now_ms();
    feedback(static_cast<int>(static_cast<uint64_t>(offset_) * 100 / size_),"Receiving firmware over Bluetooth");
    reply(output,capacity,sequence,session,true,"Firmware chunk received"); return true;
}
void OtaManager::poll(bool healthy) {
    const uint64_t now = now_ms();
    if (restart_at_ && now >= restart_at_) esp_restart();
    if (active_ && now - activity_ > 120000) abort("Firmware transfer timed out");
    if (boot_checked_) return;
    if (!boot_started_) boot_started_ = now;
    if (boot_confirmed()) { boot_checked_ = true; return; }
    if (!healthy) healthy_since_ = 0;
    else if (!healthy_since_) healthy_since_ = now;
    if (healthy_since_ && now - healthy_since_ >= 20000 && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        boot_checked_ = true; return;
    }
    // An unresponsive pending image returns to the previous bootable image.
    if (now - boot_started_ > 45000) esp_restart();
}
}
