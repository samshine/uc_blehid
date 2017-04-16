
#pragma once
#include <uccm/board.h>
#include <~sudachen/uc_irq/import.h>
#include <~sudachen/uc_waitfor/import.h>

#if !defined(__nRF5x_UC__) || !defined(SOFTDEVICE_PRESENT)
#error uc_blehid module requires BLE
#endif

#pragma uccm cflags+= -I "{NRF_BLE}/common"
#pragma uccm cflags+= -I "{NRF_BLE}/ble_advertising"
#pragma uccm cflags+= -I "{NRF_BLE}/peer_manager"
#pragma uccm cflags+= -I "{NRF_BLE}/nrf_ble_gatt"
#pragma uccm cflags+= -I "{NRF_LIBRARIES}/fds"
#pragma uccm cflags+= -I "{NRF_BLE}/ble_services"

#pragma uccm require(source)+= [@inc]/~sudachen/uc_blehid/uc_blehid.c

#pragma uccm require(module)+= {NRF_BLE}/ble_services/ble_bas/ble_bas.c
#pragma uccm require(module)+= {NRF_BLE}/ble_services/ble_dis/ble_dis.c
#pragma uccm require(module)+= {NRF_BLE}/ble_services/ble_hids/ble_hids.c
#pragma uccm require(module)+= {NRF_BLE}/ble_advertising/ble_advertising.c
#pragma uccm require(module)+= {NRF_BLE}/common/ble_advdata.c
#pragma uccm require(module)+= {NRF_BLE}/common/ble_conn_params.c
#pragma uccm require(module)+= {NRF_BLE}/common/ble_conn_state.c
#pragma uccm require(module)+= {NRF_BLE}/common/ble_srv_common.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/peer_manager.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/id_manager.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/security_manager.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/security_dispatcher.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/gatt_cache_manager.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/gatts_cache_manager.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/peer_data_storage.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/peer_database.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/peer_id.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/pm_buffer.c
#pragma uccm require(module)+= {NRF_BLE}/peer_manager/pm_mutex.c
#pragma uccm require(module)+= {NRF_LIBRARIES}/util/sdk_mapped_flags.c
#pragma uccm require(module)+= {NRF_LIBRARIES}/fstorage/fstorage.c
#pragma uccm require(module)+= {NRF_LIBRARIES}/fds/fds.c

#pragma uccm file(sdk_config_1.h) += \n\
#ifndef BLE_ADVERTISING_ENABLED\n\
#define BLE_ADVERTISING_ENABLED 1\n\
#endif \n\
#ifndef PEER_MANAGER_ENABLED\n\
#define PEER_MANAGER_ENABLED 1\n\
#endif \n\
#ifndef BLE_DIS_ENABLED\n\
#define BLE_DIS_ENABLED 1\n\
#endif \n\
#ifndef BLE_BAS_ENABLED\n\
#define BLE_BAS_ENABLED 1\n\
#endif \n\
#ifndef BLE_HIDS_ENABLED\n\
#define BLE_HIDS_ENABLED 1\n\
#endif \n\
#ifndef FSTORAGE_ENABLED\n\
#define FSTORAGE_ENABLED 1\n\
#endif \n\
#ifndef FDS_ENABLED\n\
#define FDS_ENABLED 1\n\
#endif \n\
#ifndef FS_QUEUE_SIZE\n\
#define FS_QUEUE_SIZE 4\n\
#endif\n\
#ifndef FS_MAX_WRITE_SIZE_WORDS\n\
#define FS_MAX_WRITE_SIZE_WORDS 256\n\
#endif\n\
#ifndef FS_OP_MAX_RETRIES\n\
#define FS_OP_MAX_RETRIES 3\n\
#endif\n\
#ifndef FDS_VIRTUAL_PAGE_SIZE\n\
#ifdef NRF51\n\
#define FDS_VIRTUAL_PAGE_SIZE 256\n\
#else\n\
#define FDS_VIRTUAL_PAGE_SIZE 1024\n\
#endif\n\
#endif\n\
#ifndef FDS_VIRTUAL_PAGES\n\
#define FDS_VIRTUAL_PAGES 3\n\
#endif\n\
#ifndef FDS_MAX_USERS\n\
#define FDS_MAX_USERS 8\n\
#endif\n\
#ifndef FDS_CHUNK_QUEUE_SIZE\n\
#define FDS_CHUNK_QUEUE_SIZE 8\n\
#endif\n\
#ifndef FDS_OP_QUEUE_SIZE\n\
#define FDS_OP_QUEUE_SIZE 4\n\
#endif\n

#pragma uccm file(uccm_dynamic_defs.h) ~= #define EVENT_ID_BLEHID_REPORT ({#EVENT_ID:1} + EVENT_ID_FIRST)\n
#define EVENT_IS_BLEHID_REPORT(e) ((e)->o.id == EVENT_ID_BLEHID_REPORT)
#pragma uccm file(uccm_dynamic_defs.h) ~= #define EVENT_ID_BLEADV_IDLE ({#EVENT_ID:1} + EVENT_ID_FIRST)\n
#define EVENT_IS_BLEADV_IDLE(e) ((e)->o.id == EVENT_ID_BLEADV_IDLE)
#pragma uccm file(uccm_dynamic_defs.h) ~= #define EVENT_ID_BLEHID_CONNECT ({#EVENT_ID:1} + EVENT_ID_FIRST)\n
#define EVENT_IS_BLEHID_CONNECT(e) ((e)->o.id == EVENT_ID_BLEHID_CONNECT)

typedef enum BleHidReportKind BleHidReportKind;
enum BleHidReportKind {
    BLEHID_NIL_REPORT = 0,
    BLEHID_INPUT_REPORT,
    BLEHID_OUTPUT_REPORT,
    BLEHID_FEATURE_REPORT,
    BLEHID_MAX_REPORT,
};

enum
{
    BLEHID_MAX_REPORT_SIZE = 22,
};

#define BLEHID_REPORT(Kind,Length,Id,...) \
    ( ( (uint32_t)(Kind << 24) )| \
      ( (uint32_t)(Id)<<16 )| \
      ( (uint32_t)(Length) ) )

#define BLEHID_INPUT_REPORT(...) BLEHID_REPORT(BLEHID_INPUT_REPORT,__VA_ARGS__,0,0)
#define BLEHID_OUTPUT_REPORT(...) BLEHID_REPORT(BLEHID_OUTPUT_REPORT,__VA_ARGS__,0,0)
#define BLEHID_FEATURE_REPORT(...) BLEHID_REPORT(BLEHID_FEATURE_REPORT,__VA_ARGS__,0,0)

typedef struct BleHidReport BleHidReport;
struct BleHidReport
{
    BleHidReportKind kind;
    uint8_t length;
    uint8_t id;
    uint8_t bf[BLEHID_MAX_REPORT_SIZE];
};

typedef enum BleHidError BleHidError;
enum BleHidError
{
    BLEHID_SUCCESS = 0,
    BLEHID_NONFATAL_ERROR,
    BLEHID_REPORT_ERROR,
};

void setup_blehid(
    const char *deviceName,
    const char *vendorName,
    uint16_t vendorId,
    uint16_t productId,
    uint32_t count,
    ...);

BleHidReport *getIf_blehidReport(Event* e);
BleHidReport *use_blehidReport(uint8_t id);
void send_blehidReport(void);
void erase_bleBonds(void);
void start_blehidAdvertising(void);
void update_blehidBatteryLevel(uint8_t level);
bool is_blehidConnected(void);
