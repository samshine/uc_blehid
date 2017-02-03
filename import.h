
#pragma once
#include <uccm/board.h>
#include <~sudachen/uc_irq/import.h>
#include <~sudachen/uc_waitfor/import.h>

#ifdef __nRF5x_UC__

#ifndef SOFTDEVICE_PRESENT
#error uc_hid module requires BLE Softdevice
#endif

#pragma uccm cflags+= -I "{NRF_BLE}/common"
#pragma uccm cflags+= -I "{NRF_BLE}/ble_advertising"
#pragma uccm cflags+= -I "{NRF_BLE}/peer_manager"
#pragma uccm cflags+= -I "{NRF_BLE}/nrf_ble_gatt"
#pragma uccm cflags+= -I "{NRF_LIBRARIES}/fds"
#pragma uccm cflags+= -I "{NRF_BLE}/ble_services"

#pragma uccm require(source)+= [@inc]/~sudachen/uc_hid/uc_hid_ble.c

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
#pragma uccm require(module)+= {SOFTDEVICE}/common/softdevice_handler/softdevice_handler.c
#pragma uccm require(module)+= {NRF_LIBRARIES}/util/app_util_platform.c
#pragma uccm require(module)+= {NRF_LIBRARIES}/util/app_error.c
#pragma uccm require(module)+= {NRF_LIBRARIES}/util/nrf_assert.c
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
#define FDS_VIRTUAL_PAGE_SIZE 256\n\
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
#endif\n\

#elif defined __stm32Fx_UC__

#pragma uccm require(source) += [@inc]/~sudachen/uc_hid/uc_hid_usb.c


#endif

#pragma uccm file(uccm_dynamic_defs.h) ~= #define UC_EVENT_ID_HID_REPORT ({#UC_EVENT_ID:1} + UC_EVENT_ID_FIRST)\n

typedef enum UcHidReportKind UcHidReportKind;
enum UcHidReportKind {
    UC_HID_NIL_REPORT = 0,
    UC_HID_INPUT_REPORT,
    UC_HID_OUTPUT_REPORT,
    UC_HID_FEATURE_REPORT,
    UC_HID_MAX_REPORT,
};

enum
{
    UC_HID_MAX_REPORT_SIZE = 64,
};

#define hidRawReport(Kind,Length,Id,...) \
    ( ( (uint32_t)(Kind << 24) )| \
      ( (uint32_t)(Id)<<16 )| \
      ( (uint32_t)(Length) ) )

#define hidRawInputReport(...) hidRawReport(UC_HID_INPUT_REPORT,__VA_ARGS__,0,0)
#define hidRawOutputReport(...) hidRawReport(UC_HID_OUTPUT_REPORT,__VA_ARGS__,0,0)
#define hidRawFeatureReport(...) hidRawReport(UC_HID_FEATURE_REPORT,__VA_ARGS__,0,0)

typedef struct UcHidReport UcHidReport;
struct UcHidReport
{
    UcEvent e;
    UcHidReportKind kind;
    uint8_t length;
    uint8_t id;
    uint8_t bf[UC_HID_MAX_REPORT_SIZE];
};

typedef enum UcHidError UcHidError;
enum UcHidError
{
    UC_HID_SUCCESS = 0,
    UC_HID_NONFATAL_ERROR,
    UC_HID_REPORT_ERROR,
};

void ucSetup_Hid(
    const char *deviceName,
    const char *vendorName,
    uint16_t vendorId,
    uint16_t productId,
    uint32_t count,
    ...);

UcHidError ucSend_HidReport(struct UcHidReport *report);
UcHidReport *ucAlloc_HidInputReport(uint8_t id);
UcHidReport *ucAlloc_HidFeatureReport(uint8_t id);
UcHidReport *ucGetIf_HidOutputReport(struct UcEvent *e);
UcHidReport *ucGetIf_HidFeatureReport(struct UcEvent *e);

#ifdef __nRF5x_UC__
void ucErase_BleBonds(void);
#endif
