
#include <~sudachen/uc_hid/import.h>
#include <stdarg.h>
#include <ble_srv_common.h>
#include <ble_advertising.h>
#include <ble_advdata.h>
#include <ble_hids/ble_hids.h>
#include <ble_bas/ble_bas.h>
#include <ble_dis/ble_dis.h>
#include <ble_conn_params.h>
#include <ble_conn_state.h>
#include <peer_manager.h>
#include <softdevice_handler.h>
#include <app_util.h>
#include <fstorage.h>
#include <fds.h>
#include <nrf_log.h>

#define INFO(...) ucPrint("uc_hid:" __VA_ARGS__)

#define UC_HID_REPORTS_HEAP_MAX_COUNT       10+3
#define UC_HID_REPORTS_DEF_MAX_COUNT        5
#define UC_HID_REPORT_MAP_MAX_LENGTH        128

#define CENTRAL_LINK_COUNT                  0
#define PERIPHERAL_LINK_COUNT               1
#define FIRST_CONN_PARAMS_UPDATE_DELAY      APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)
#define NEXT_CONN_PARAMS_UPDATE_DELAY       APP_TIMER_TICKS(30000, APP_TIMER_PRESCALER)
#define MAX_CONN_PARAMS_UPDATE_COUNT        3

#define APP_ADV_FAST_INTERVAL               0x0028  // in units of 0.625 ms. ~ 25 ms
#define APP_ADV_SLOW_INTERVAL               0x0C80  // in units of 0.625 ms. ~ 2 seconds
#define APP_ADV_FAST_TIMEOUT                30      // in seconds
#define APP_ADV_SLOW_TIMEOUT                180     // in seconds

#define MIN_CONN_INTERVAL                   MSEC_TO_UNITS(7.5, UNIT_1_25_MS) // 7.5 ms
#define MAX_CONN_INTERVAL                   MSEC_TO_UNITS(30, UNIT_1_25_MS)  // 30 ms
#define SLAVE_LATENCY                       6                                //
#define CONN_SUP_TIMEOUT                    MSEC_TO_UNITS(430, UNIT_10_MS)   // 430 ms

#define SEC_PARAM_BOND                      1
#define SEC_PARAM_MITM                      0
#define SEC_PARAM_LESC                      0
#define SEC_PARAM_KEYPRESS                  0
#define SEC_PARAM_IO_CAPABILITIES           BLE_GAP_IO_CAPS_NONE
#define SEC_PARAM_OOB                       0
#define SEC_PARAM_MIN_KEY_SIZE              7
#define SEC_PARAM_MAX_KEY_SIZE              16

static uint8_t      reportMap[UC_HID_REPORT_MAP_MAX_LENGTH];

static uint32_t     dfInrep[UC_HID_REPORTS_DEF_MAX_COUNT] = {0,};
static uint32_t     dfOutrep[UC_HID_REPORTS_DEF_MAX_COUNT] = {0,};
static uint32_t     dfFerep[UC_HID_REPORTS_DEF_MAX_COUNT] = {0,};
static UcHidReport  rHeap[UC_HID_REPORTS_HEAP_MAX_COUNT];

static ble_hids_t   hids;
static ble_bas_t    bas;
static uint16_t     connHandle = BLE_CONN_HANDLE_INVALID;

static pm_peer_id_t peerId;
static pm_peer_id_t whitelistPeers[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
static uint32_t     whitelistPeerCnt;
static bool         isWlChanged = false;

static ble_uuid_t   advUuids[] = {
    {
        BLE_UUID_HUMAN_INTERFACE_DEVICE_SERVICE,
        BLE_UUID_TYPE_BLE
    }
};

#define REPORT_KIND(df) ((uint8_t)((df) >> 24))
#define REPORT_ID(df) ((uint8_t)((df) >> 16))
#define REPORT_LEN(df) ((uint8_t)(df))

static bool dfIsValid(uint32_t df)
{
    uint8_t kind = REPORT_KIND(df);
    uint8_t len  = REPORT_LEN(df);
    return kind > UC_HID_NIL_REPORT && kind < UC_HID_MAX_REPORT && len < UC_HID_MAX_REPORT_SIZE;
}

static UcHidReport *rAllocate(uint32_t df)
{
    UcHidReport *r = NULL;

    __Critical
    {
        for ( size_t i = 0; i < UC_HID_REPORTS_HEAP_MAX_COUNT; ++i )
        {
            if ( rHeap[i].kind == UC_HID_NIL_REPORT )\
            {
                r = &rHeap[i];
                r->kind = (UcHidReportKind)REPORT_KIND(df);
            }
        }
    }

    if ( r != NULL )
    {
        __Assert( ucIsUnlisted_Event(&r->e) );

        r->id = REPORT_ID(df);
        r->length = REPORT_LEN(df);

        memset(r->bf,0,sizeof(r->bf));
    }

    return r;
}

static uint32_t dfFindInDfs(uint8_t id, uint32_t *dfs)
{
    for ( size_t i = 0; i < UC_HID_REPORTS_DEF_MAX_COUNT && dfs[i]; ++i )
    {
        uint32_t df = dfs[i];
        if ( REPORT_ID(df) == id )
            return df;
    }

    return 0;
}

static void dfAppendToDfs(uint32_t df,uint32_t *dfs)
{
    size_t i = 0;
    __Assert( dfIsValid(df) );

    while ( i < UC_HID_REPORTS_DEF_MAX_COUNT && dfs[i] ) ++i;

    __Assert_S( i < UC_HID_REPORTS_DEF_MAX_COUNT, "array of report definitions is full" );
    if ( i < UC_HID_REPORTS_DEF_MAX_COUNT )
    {
        dfs[i] = df;
    }
}

static uint32_t dfFind(uint8_t id, UcHidReportKind kind)
{
    switch (kind)
    {
        case UC_HID_INPUT_REPORT: return dfFindInDfs(id,dfInrep);
        case UC_HID_OUTPUT_REPORT: return dfFindInDfs(id,dfOutrep);
        case UC_HID_FEATURE_REPORT: return dfFindInDfs(id,dfFerep);
        default: __Assert_S(0, "unknown type of report");
    }

    return 0;
}

static void dfAppend(uint32_t df)
{
    uint8_t kind = (uint8_t)(df >> 24);
    switch (kind)
    {
        case UC_HID_INPUT_REPORT: dfAppendToDfs(df,dfInrep); return;
        case UC_HID_OUTPUT_REPORT: dfAppendToDfs(df,dfOutrep); return;
        case UC_HID_FEATURE_REPORT: dfAppendToDfs(df,dfFerep); return;
        default: __Assert_S(0, "unknown type of report");
    }
}

static void peerListGet(pm_peer_id_t *peers, uint32_t *size)
{
    pm_peer_id_t id;
    uint32_t     toCopy;

    toCopy = (*size < BLE_GAP_WHITELIST_ADDR_MAX_COUNT) ?
                   *size : BLE_GAP_WHITELIST_ADDR_MAX_COUNT;

    id = pm_next_peer_id_get(PM_PEER_ID_INVALID);
    *size = 0;

    while ((id != PM_PEER_ID_INVALID) && (toCopy--))
    {
        peers[(*size)++] = id;
        id = pm_next_peer_id_get(id);
    }
}

static void startAdvertising(void)
{
    memset(whitelistPeers, PM_PEER_ID_INVALID, sizeof(whitelistPeers));
    whitelistPeerCnt = (sizeof(whitelistPeers) / sizeof(pm_peer_id_t));
    peerListGet(whitelistPeers, &whitelistPeerCnt);
    __Nrf_Success pm_whitelist_set(whitelistPeers, whitelistPeerCnt);
    __Nrf_Success pm_device_identities_list_set(whitelistPeers, whitelistPeerCnt);
    isWlChanged = false;
    (void)isWlChanged;
    __Nrf_Success ble_advertising_start(BLE_ADV_MODE_FAST);
}

static void initGapParams(const char *deviceName)
{
    ble_gap_conn_params_t   gapConnParams = {0,};
    ble_gap_conn_sec_mode_t secMode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&secMode);

    __Nrf_Success sd_ble_gap_device_name_set(&secMode,(const uint8_t*)deviceName,strlen(deviceName));
    __Nrf_Success sd_ble_gap_appearance_set(BLE_APPEARANCE_GENERIC_HID);

    gapConnParams.min_conn_interval = MIN_CONN_INTERVAL;
    gapConnParams.max_conn_interval = MAX_CONN_INTERVAL;
    gapConnParams.slave_latency     = SLAVE_LATENCY;
    gapConnParams.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    __Nrf_Success sd_ble_gap_ppcp_set(&gapConnParams);
}

static void initDis(uint16_t vendorId, uint16_t prodId, const char *vendorName)
{
    ble_dis_init_t   disInitObj = {0,};
    ble_dis_pnp_id_t pnpId;

    pnpId.vendor_id_source = 2;
    pnpId.vendor_id        = vendorId;
    pnpId.product_id       = prodId;
    pnpId.product_version  = 0x0101;

    ble_srv_ascii_to_utf8(&disInitObj.manufact_name_str,(char*)vendorName);
    disInitObj.p_pnp_id = &pnpId;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&disInitObj.dis_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&disInitObj.dis_attr_md.write_perm);

    __Nrf_Success ble_dis_init(&disInitObj);
}

static void initBas(void)
{
    ble_bas_init_t basInitObj = {0,};

    basInitObj.evt_handler          = NULL;
    basInitObj.support_notification = true;
    basInitObj.p_report_ref         = NULL;
    basInitObj.initial_batt_level   = 100;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&basInitObj.battery_level_char_attr_md.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&basInitObj.battery_level_char_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&basInitObj.battery_level_char_attr_md.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&basInitObj.battery_level_report_read_perm);

    __Nrf_Success ble_bas_init(&bas, &basInitObj);
}

static void pmEvtHandler(pm_evt_t const *e);

static void initPeerManager(void)
{
    ble_gap_sec_params_t secParam = {0,};

    __Nrf_Success pm_init();

    secParam.bond           = SEC_PARAM_BOND;
    secParam.mitm           = SEC_PARAM_MITM;
    secParam.lesc           = SEC_PARAM_LESC;
    secParam.keypress       = SEC_PARAM_KEYPRESS;
    secParam.io_caps        = SEC_PARAM_IO_CAPABILITIES;
    secParam.oob            = SEC_PARAM_OOB;
    secParam.min_key_size   = SEC_PARAM_MIN_KEY_SIZE;
    secParam.max_key_size   = SEC_PARAM_MAX_KEY_SIZE;
    secParam.kdist_own.enc  = 1;
    secParam.kdist_own.id   = 1;
    secParam.kdist_peer.enc = 1;
    secParam.kdist_peer.id  = 1;

    __Nrf_Success pm_sec_params_set(&secParam);
    __Nrf_Success pm_register(pmEvtHandler);

}

static void advEvtHandler(ble_adv_evt_t ble_adv_evt);

static void initAdvertising(void)
{
    uint8_t                flags;
    ble_advdata_t          advdata;
    ble_adv_modes_config_t options = {0,};

    memset(&advdata,0,sizeof(advdata));

    flags                           = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance      = true;
    advdata.flags                   = flags;
    advdata.uuids_complete.uuid_cnt = sizeof(advUuids) / sizeof(advUuids[0]);
    advdata.uuids_complete.p_uuids  = advUuids;

    options.ble_adv_whitelist_enabled      = true;
    options.ble_adv_directed_enabled       = true;
    options.ble_adv_directed_slow_enabled  = false;
    options.ble_adv_directed_slow_interval = 0;
    options.ble_adv_directed_slow_timeout  = 0;
    options.ble_adv_fast_enabled           = true;
    options.ble_adv_fast_interval          = APP_ADV_FAST_INTERVAL;
    options.ble_adv_fast_timeout           = APP_ADV_FAST_TIMEOUT;
    options.ble_adv_slow_enabled           = true;
    options.ble_adv_slow_interval          = APP_ADV_SLOW_INTERVAL;
    options.ble_adv_slow_timeout           = APP_ADV_SLOW_TIMEOUT;

    __Nrf_Success  ble_advertising_init(&advdata,
                                        NULL,
                                        &options,
                                        advEvtHandler,
                                        ucNrfErrorHandler);
}

static void initConnParams(void)
{
    ble_conn_params_init_t cp = {0,};

    cp.p_conn_params                  = NULL;
    cp.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp.disconnect_on_fail             = false;
    cp.evt_handler                    = NULL;
    cp.error_handler                  = ucNrfErrorHandler;

    __Nrf_Success ble_conn_params_init(&cp);
}

static void hidsEvtHandler(ble_hids_t *hids, ble_hids_evt_t *e);

static void initHids()
{
    ble_hids_init_t             hidsInitObj = {0,};
    ble_hids_inp_rep_init_t     inrep[UC_HID_REPORTS_DEF_MAX_COUNT] = {0,};
    size_t                      inrepCount;
    ble_hids_outp_rep_init_t    outrep[UC_HID_REPORTS_DEF_MAX_COUNT] = {0,};
    size_t                      outrepCount;
    ble_hids_feature_rep_init_t ferep[UC_HID_REPORTS_DEF_MAX_COUNT]  = {0,};
    size_t                      ferepCount;
    uint8_t                     hidInfoFlags;

    for (inrepCount = 0; inrepCount < UC_HID_REPORTS_DEF_MAX_COUNT && dfInrep[inrepCount]; ++inrepCount )
    {
        uint32_t df = dfInrep[inrepCount];
        ble_hids_inp_rep_init_t *r = inrep + inrepCount;
        r->max_len = REPORT_LEN(df);
        r->rep_ref.report_id = REPORT_ID(df);
        r->rep_ref.report_type = BLE_HIDS_REP_TYPE_INPUT;
        BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&r->security_mode.cccd_write_perm);
        BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&r->security_mode.read_perm);
        BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&r->security_mode.write_perm);
    }

    for (outrepCount = 0; outrepCount < UC_HID_REPORTS_DEF_MAX_COUNT && dfInrep[outrepCount]; ++outrepCount )
    {
        uint32_t df = dfOutrep[outrepCount];
        ble_hids_outp_rep_init_t *r = outrep + outrepCount;
        r->max_len = REPORT_LEN(df);
        r->rep_ref.report_id = REPORT_ID(df);
        r->rep_ref.report_type = BLE_HIDS_REP_TYPE_OUTPUT;
        BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&r->security_mode.read_perm);
        BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&r->security_mode.write_perm);
    }

    for (ferepCount = 0; ferepCount < UC_HID_REPORTS_DEF_MAX_COUNT && dfFerep[ferepCount]; ++ferepCount )
    {
        uint32_t df = dfFerep[ferepCount];
        ble_hids_feature_rep_init_t *r = ferep + ferepCount;
        r->max_len = REPORT_LEN(df);
        r->rep_ref.report_id = REPORT_ID(df);
        r->rep_ref.report_type = BLE_HIDS_REP_TYPE_FEATURE;
        BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&r->security_mode.cccd_write_perm);
        BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&r->security_mode.read_perm);
        BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&r->security_mode.write_perm);
    }

    hidInfoFlags = HID_INFO_FLAG_REMOTE_WAKE_MSK | HID_INFO_FLAG_NORMALLY_CONNECTABLE_MSK;

    hidsInitObj.evt_handler                    = hidsEvtHandler;
    hidsInitObj.error_handler                  = ucNrfErrorHandler;
    hidsInitObj.is_kb                          = false;
    hidsInitObj.is_mouse                       = false;
    hidsInitObj.inp_rep_count                  = inrepCount;
    hidsInitObj.p_inp_rep_array                = inrep;
    hidsInitObj.outp_rep_count                 = outrepCount;
    hidsInitObj.p_outp_rep_array               = outrep;
    hidsInitObj.feature_rep_count              = ferepCount;
    hidsInitObj.p_feature_rep_array            = ferep;
    hidsInitObj.rep_map.p_data                 = reportMap;
    hidsInitObj.hid_information.bcd_hid        = 0x0101;
    hidsInitObj.hid_information.b_country_code = 0;
    hidsInitObj.hid_information.flags          = hidInfoFlags;
    hidsInitObj.included_services_count        = 0;
    hidsInitObj.p_included_services_array      = NULL;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hidsInitObj.rep_map.security_mode.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&hidsInitObj.rep_map.security_mode.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hidsInitObj.hid_information.security_mode.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&hidsInitObj.hid_information.security_mode.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hidsInitObj.security_mode_protocol.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hidsInitObj.security_mode_protocol.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&hidsInitObj.security_mode_ctrl_point.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hidsInitObj.security_mode_ctrl_point.write_perm);

    size_t len = 7;
    memcpy(reportMap,"\x06\x00\xff\x09\x01\xa1\x01",len);
    // Usage Page (Generic Desktop)
    // Usage (Vendor Defined)
    // Collection (Application)

    uint32_t *dfs[3] = {dfInrep,dfOutrep,dfFerep};
    uint8_t   tp[3]  = {0x81,0x91,0xb1};

    for ( size_t q = 0; q < 3; ++q)
    {
        for ( size_t i = 0; i < UC_HID_REPORTS_DEF_MAX_COUNT && dfs[q][i]; ++i )
        {
            uint32_t df = dfs[q][i];

            if ( REPORT_ID(df) )
            {
                __Assert(len+2+13 < UC_HID_REPORT_MAP_MAX_LENGTH);
                reportMap[len++] = 0x85; // Report ID
                reportMap[len++] = REPORT_ID(df);
            }

            __Assert(len+13 < UC_HID_REPORT_MAP_MAX_LENGTH);
            reportMap[len++] = 0x09;  // Vendor Usage
            reportMap[len++] = 0x01;
            reportMap[len++] = 0x15;  // LOGICAL_MINIMUM
            reportMap[len++] = 0x00;  // 0
            reportMap[len++] = 0x26;  // LOGICAL_MAXIMUM
            reportMap[len++] = 0xff;  // 255
            reportMap[len++] = 0x00;
            reportMap[len++] = 0x75;  // Report Size
            reportMap[len++] = 0x08;  // 8-bit
            reportMap[len++] = 0x95;  // Report Count
            reportMap[len++] = REPORT_LEN(df);
            reportMap[len++] = tp[q]; // Input / Output / Feature
            reportMap[len++] = 0x82;  // (Data, Variable, Absolute)
        }
    }

    __Assert(len < UC_HID_REPORT_MAP_MAX_LENGTH);
    reportMap[len++] = 0xc0;
    hidsInitObj.rep_map.data_len = len;

    __Nrf_Success ble_hids_init(&hids,&hidsInitObj);
}

static void bleDispatch(ble_evt_t *e);
static void sysDispatch(uint32_t e);

void ucSetup_Hid(
    const char *deviceName,
    const char *vendorName,
    uint16_t vendorId,
    uint16_t productId,
    uint32_t count,
    ...)
{
    va_list ap;

    va_start(ap,count);

    for ( uint32_t i = 0; i < count; ++i )
    {
        uint32_t df = va_arg(ap,uint32_t);
        __Assert( dfIsValid(df) );
        dfAppend(df);
    }

    va_end(ap);

    ble_enable_params_t bleEnableParams;
    __Nrf_Success softdevice_enable_get_default_config(CENTRAL_LINK_COUNT,
                                                    PERIPHERAL_LINK_COUNT,
                                                    &bleEnableParams);
    CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT, PERIPHERAL_LINK_COUNT);
#if (NRF_SD_BLE_API_VERSION == 3)
    bleEnableParams.gatt_enable_params.att_mtu = NRF_BLE_MAX_MTU_SIZE;
#endif

    __Nrf_Success softdevice_enable(&bleEnableParams);
    __Nrf_Success softdevice_ble_evt_handler_set(bleDispatch);
    __Nrf_Success softdevice_sys_evt_handler_set(sysDispatch);

    initPeerManager();
    initGapParams(deviceName);
    initAdvertising();
    initBas();
    initDis(vendorId,productId,vendorName);
    initHids();
    initConnParams();
    startAdvertising();
}

UcHidError ucSend_HidReport(UcHidReport *report)
{
    return UC_HID_SUCCESS;
}

UcHidReport *ucAlloc_HidInputReport(uint8_t id)
{
    uint32_t df;
    if (( df = dfFind(id,UC_HID_INPUT_REPORT) ))
        return rAllocate(df);
    return NULL;
}

UcHidReport *ucAlloc_HidFeatureReport(uint8_t id)
{
    uint32_t df;
    if (( df = dfFind(id,UC_HID_FEATURE_REPORT) ))
        return rAllocate(df);
    return NULL;
}

UcHidReport *ucGetIf_HidOutputReport(UcEvent *e)
{
    if ( e >= &rHeap[0].e && e <= &rHeap[UC_HID_REPORTS_HEAP_MAX_COUNT-1].e )
    {
        UcHidReport *r = (UcHidReport *)e;
        if ( r->kind == UC_HID_OUTPUT_REPORT )
            return r;
    }
    return NULL;
}

UcHidReport *ucGetIf_HidFeatureReport(UcEvent *e)
{
    if ( e >= &rHeap[0].e && e <= &rHeap[UC_HID_REPORTS_HEAP_MAX_COUNT-1].e )
    {
        UcHidReport *r = (UcHidReport *)e;
        if ( r->kind == UC_HID_FEATURE_REPORT )
            return r;
    }
    return NULL;
}

void ucUpdate_BatteryLevel(uint8_t level)
{
    uint32_t err = ble_bas_battery_level_update(&bas, level);
    if ((err != NRF_SUCCESS) &&
        (err != NRF_ERROR_INVALID_STATE) &&
        (err != BLE_ERROR_NO_TX_PACKETS) &&
        (err != BLE_ERROR_GATTS_SYS_ATTR_MISSING)
       )
        __Nrf_Success err;
}

static void bleEvtHandler(const ble_evt_t *e);

void bleDispatch(ble_evt_t *e)
{
    ble_conn_state_on_ble_evt(e);
    pm_on_ble_evt(e);
    bleEvtHandler(e);
    ble_advertising_on_ble_evt(e);
    ble_conn_params_on_ble_evt(e);
    ble_hids_on_ble_evt(&hids,e);
    ble_bas_on_ble_evt(&bas,e);
}

void sysDispatch(uint32_t e)
{
    fs_sys_event_handler(e);
    ble_advertising_on_sys_evt(e);
}

void pmEvtHandler(const pm_evt_t *e)
{
    uint32_t err;

    switch ( e->evt_id )
    {
        case PM_EVT_BONDED_PEER_CONNECTED:
            INFO("connected to a previously bonded device");
            break;

        case PM_EVT_CONN_SEC_SUCCEEDED:
            INFO("connection secured. Role %?, conn_handle %?, Procedure %?",
                    $u(ble_conn_state_role(e->conn_handle)),
                    $u(e->conn_handle),
                    $u(e->params.conn_sec_succeeded.procedure));
            peerId = e->peer_id;
            if ( e->params.conn_sec_succeeded.procedure == PM_LINK_SECURED_PROCEDURE_BONDING )
            {
                INFO("new bond, add the peer to whitelist");
                INFO("whitelist peer's count %?, maximum %?",
                     $u(whitelistPeerCnt),
                     $u(BLE_GAP_WHITELIST_ADDR_MAX_COUNT));
                if ( whitelistPeerCnt < BLE_GAP_WHITELIST_ADDR_MAX_COUNT )
                {
                    whitelistPeers[whitelistPeerCnt++] = peerId;
                    isWlChanged = true;
                    (void)isWlChanged;
                }
            }
            break;

        case PM_EVT_CONN_SEC_FAILED: // nothing yet currently
            break;

        case PM_EVT_CONN_SEC_CONFIG_REQ:
        {
            pm_conn_sec_config_t c = {.allow_repairing = false };
            pm_conn_sec_config_reply(e->conn_handle,&c);
            break;
        }

        case PM_EVT_STORAGE_FULL:
            err = fds_gc();
            if ( err == FDS_ERR_BUSY || err == FDS_ERR_NO_SPACE_IN_QUEUES )
                ;// retry
            else
                __Nrf_Success err;
            break;

        case PM_EVT_PEERS_DELETE_SUCCEEDED:
            startAdvertising();
            break;

        case PM_EVT_LOCAL_DB_CACHE_APPLY_FAILED:
            pm_local_database_has_changed();
            break;

        case PM_EVT_PEER_DATA_UPDATE_FAILED:
            __Nrf_Success e->params.peer_data_update_failed.error;
            break;
        case PM_EVT_PEER_DELETE_FAILED:
            __Nrf_Success e->params.peer_delete_failed.error;
            break;
        case PM_EVT_PEERS_DELETE_FAILED:
            __Nrf_Success e->params.peers_delete_failed_evt.error;
            break;
        case PM_EVT_ERROR_UNEXPECTED:
            __Nrf_Success e->params.error_unexpected.error;
            break;

        case PM_EVT_CONN_SEC_START:
        case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
        case PM_EVT_PEER_DELETE_SUCCEEDED:
        case PM_EVT_LOCAL_DB_CACHE_APPLIED:
        case PM_EVT_SERVICE_CHANGED_IND_SENT:
        case PM_EVT_SERVICE_CHANGED_IND_CONFIRMED:
        default:
            break;
    }
}

void advEvtHandler(ble_adv_evt_t e)
{
    uint32_t err;

    switch ( e )
    {
        case BLE_ADV_EVT_DIRECTED:
            INFO("evt => BLE_ADV_EVT_DIRECTED");
            break;
        case BLE_ADV_EVT_FAST:
            INFO("evt => BLE_ADV_EVT_FAST");
            break;
        case BLE_ADV_EVT_FAST_WHITELIST:
            INFO("evt => BLE_ADV_EVT_FAST_WHITELIST");
            break;
        case BLE_ADV_EVT_SLOW_WHITELIST:
            INFO("evt => BLE_ADV_EVT_SLOW_WHITELIST");
            break;
        case BLE_ADV_EVT_IDLE:
            INFO("evt => BLE_ADV_EVT_IDLE");
            //sleep_mode_enter();
            break;
        case BLE_ADV_EVT_WHITELIST_REQUEST:
        {
            ble_gap_addr_t addrs[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
            ble_gap_irk_t  irks[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
            uint32_t       addrCnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;
            uint32_t       irkCnt  = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;
            __Nrf_Success pm_whitelist_get(addrs,&addrCnt,irks,&irkCnt);
            __Nrf_Success ble_advertising_whitelist_reply(addrs,addrCnt,irks,irkCnt);
            break;
        }

        case BLE_ADV_EVT_PEER_ADDR_REQUEST:
        {
            pm_peer_data_bonding_t p;

            if ( peerId != PM_PEER_ID_INVALID )
            {
                err = pm_peer_data_bonding_load(peerId,&p);
                if ( err != NRF_ERROR_NOT_FOUND )
                {
                    __Nrf_Success err;
                    ble_gap_addr_t *a = &p.peer_ble_id.id_addr_info;
                    __Nrf_Success ble_advertising_peer_addr_reply(a);
                }
            }
            break;
        }

        default: break;
    }
}

void hidsEvtHandler(ble_hids_t *hids, ble_hids_evt_t *e)
{
    switch ( e->evt_type )
    {
        case BLE_HIDS_EVT_BOOT_MODE_ENTERED: break;
        case BLE_HIDS_EVT_REPORT_MODE_ENTERED: break;
        case BLE_HIDS_EVT_NOTIF_ENABLED: break;
        case BLE_HIDS_EVT_REP_CHAR_WRITE:
            if (e->params.char_write.char_id.rep_type == BLE_HIDS_REP_TYPE_OUTPUT
              ||e->params.char_write.char_id.rep_type == BLE_HIDS_REP_TYPE_FEATURE )
            {
                uint8_t rId = e->params.char_write.char_id.rep_index;
                // fill report from event
                (void)rId;
            }
            break;
        default: break;
    }
}

void bleEvtHandler(const ble_evt_t *e)
{
    uint32_t err;

    switch ( e->header.evt_id )
    {
        case BLE_GAP_EVT_CONNECTED:
            INFO("connected");
            connHandle = e->evt.gap_evt.conn_handle;
            break;

        case BLE_EVT_TX_COMPLETE:
            // send next report

            break;

        case BLE_GAP_EVT_DISCONNECTED:
            INFO("disconnected");
            // drop awating reports

            connHandle = BLE_CONN_HANDLE_INVALID;
            __Nrf_Success pm_whitelist_set(whitelistPeers,whitelistPeerCnt);
            err = pm_device_identities_list_set(whitelistPeers,whitelistPeerCnt);
            if ( err != NRF_ERROR_NOT_SUPPORTED )
                __Nrf_Success err;

            isWlChanged = false;
            break;

        case BLE_GATTC_EVT_TIMEOUT:
            INFO("GATT client timeout");
            __Nrf_Success sd_ble_gap_disconnect(e->evt.gattc_evt.conn_handle,
                                                BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            INFO("GATT server timeout");
            __Nrf_Success sd_ble_gap_disconnect(e->evt.gatts_evt.conn_handle,
                                                BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            break;

        case BLE_EVT_USER_MEM_REQUEST:
            __Nrf_Success sd_ble_user_mem_reply(connHandle, NULL);
            break;

        case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
        {
            ble_gatts_evt_rw_authorize_request_t  req;
            ble_gatts_rw_authorize_reply_params_t reply;

            req = e->evt.gatts_evt.params.authorize_request;

            if ( req.type != BLE_GATTS_AUTHORIZE_TYPE_INVALID )
            {
                if ((req.request.write.op == BLE_GATTS_OP_PREP_WRITE_REQ)     ||
                    (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) ||
                    (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL))
                {
                     reply.type = req.type;
                     // feature not supported
                     reply.params.write.gatt_status = BLE_GATT_STATUS_ATTERR_APP_BEGIN + 2;
                     __Nrf_Success sd_ble_gatts_rw_authorize_reply(e->evt.gatts_evt.conn_handle,&reply);
                }
            }

            break;
        }

#if (NRF_SD_BLE_API_VERSION == 3)
        case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
            __Nrf_Success sd_ble_gatts_exchange_mtu_reply(e->evt.gatts_evt.conn_handle,
                                                       NRF_BLE_MAX_MTU_SIZE);
            break;
#endif

        default: break;

    }
}
