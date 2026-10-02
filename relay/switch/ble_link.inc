/* Device discovery adapted from the validated v0.4.0 probe. */
static FILE *log_file;
static PadState pad;
static bool quit_requested;
static bool leave_requested;
static const u8 service_be[16] = {0x7b,0x61,0x23,0x8d,0x02,0x8a,0x4f,0x65,
                                 0x99,0xd8,0x7c,0x8f,0x22,0xa1,0x1d,0x4e};

static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *fmt, ...) {
    va_list ap, copy;
    va_start(ap, fmt); va_copy(copy, ap);
    vprintf(fmt, ap); putchar('\n');
    if (log_file) {
        long at=ftell(log_file);
        if(at>=0 && at<256*1024){
            char line[1024];int n=vsnprintf(line,sizeof(line),fmt,copy);
            if(n>0){size_t size=(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1;
                size_t left=256*1024-(size_t)at;if(size>left)size=left;
                fwrite(line,1,size,log_file);if(size<left)fputc('\n',log_file);fflush(log_file);
            }
        }
    }
    va_end(copy); va_end(ap); consoleUpdate(NULL);
}
static bool result(const char *name, Result rc) {
    note("%s: 0x%08x (%u-%u)%s", name, rc, R_MODULE(rc), R_DESCRIPTION(rc),
         R_SUCCEEDED(rc) ? " OK" : " FAILED");
    return R_SUCCEEDED(rc);
}
static bool running(void) {
    if (!appletMainLoop()) quit_requested = true;
    padUpdate(&pad);
    u64 buttons = padGetButtonsDown(&pad);
    if (buttons & HidNpadButton_Plus) quit_requested = true;
    if (buttons & HidNpadButton_B) leave_requested = true;
    return !quit_requested;
}
static BtdrvGattAttributeUuid make_uuid(bool reverse, bool characteristic) {
    BtdrvGattAttributeUuid uuid = {0};
    uuid.size = 16;
    for (int i = 0; i < 16; i++) uuid.uuid[reverse ? 15 - i : i] = service_be[i];
    if (characteristic) uuid.uuid[reverse ? 0 : 15] = 0x4f;
    return uuid;
}

typedef struct {
    bool initialized, scanning, connected;
    bool scan_event_ok, connection_event_ok, discovery_event_ok, gatt_event_ok;
    bool service_registered, char_registered, characteristic_ok, direct_gatt;
    bool notify_registered, notify_active, streaming, read_recovery;
    Event scan_event, connection_event, discovery_event, gatt_event;
    BtdrvGattAttributeUuid service_uuid, char_uuid;
    BtdrvAddress address;
    u32 handle;
    BtdevGattService service;
    BtdevGattCharacteristic characteristic;
    LrApprovedSession session;
} BleSession;

static void dump_attribute(const char *label, const void *data, size_t size) {
    /* Only our custom service/characteristic records: no peer addresses. */
    const u8 *bytes = data;
    char line[129] = {0};
    if (size > 64) size = 64;
    for (size_t i = 0; i < size; i++) snprintf(line + 2*i, 3, "%02x", bytes[i]);
    note("%s raw=%s", label, line);
}

static void release_ble(BleSession *b) {
    if (b->notify_registered) {
        BtdrvGattId service={.instance_id=b->service.instance_id,.uuid=b->service.attr.uuid};
        BtdrvGattId attr={.instance_id=b->characteristic.instance_id,.uuid=b->characteristic.attr.uuid};
        result("deregister BLE notifications",btLeClientDeregisterNotification(b->handle,b->service.primary_service,&service,&attr));
        b->notify_registered=b->notify_active=false;
    }
    if (b->connected) result("btdevDisconnectFromGattServer", btdevDisconnectFromGattServer(b->handle));
    if (b->char_registered) result("unregister characteristic path", btdevUnregisterGattOperationNotification(&b->char_uuid));
    if (b->service_registered) result("unregister service path", btdevUnregisterGattOperationNotification(&b->service_uuid));
    if (b->scanning) result("stop BLE scan", btdevStopBleScanSmartDevice());
    if (b->gatt_event_ok) eventClose(&b->gatt_event);
    if (b->discovery_event_ok) eventClose(&b->discovery_event);
    if (b->connection_event_ok) eventClose(&b->connection_event);
    if (b->scan_event_ok) eventClose(&b->scan_event);
    if (b->initialized) { btdevExit(); note("BLE released."); }
}

static bool discover_phone(BleSession *b) {
    /* The UUID byte order is verified by discovery, rather than assumed.
     * Both passes request the same specific 128-bit service in one order. */
    for (int order = 0; order < 2 && running(); order++) {
        b->service_uuid = make_uuid(order == 0, false);
        b->char_uuid = make_uuid(order == 0, true);
        note("Finding companion for 15s; UUID order=%s", order == 0 ? "little-endian" : "canonical");
        b->scanning = result("btdevStartBleScanSmartDevice", btdevStartBleScanSmartDevice(&b->service_uuid));
        if (!b->scanning) continue;
        bool found = false;
        for (int i = 0; i < 75 && running(); i++) {
            BtdrvBleScanResult entries[10] = {0};
            u8 count = 0;
            Result rc = btmuGetBleScanResultsForSmartDevice(entries, 10, &count);
            if (R_FAILED(rc)) { result("smart-device scan results", rc); break; }
            if (count) { b->address = entries[0].addr; found = true; note("Companion-filtered scan returned %u entry(s).", count); break; }
            svcSleepThread(200000000LL);
        }
        result("stop BLE scan", btdevStopBleScanSmartDevice());
        b->scanning = false;
        if (found) return true;
    }
    note("No companion discovered. Keep the companion app open and advertising.");
    return false;
}

static bool prepare_ble(BleSession *b) {
    b->initialized = result("btdevInitialize", btdevInitialize());
    if (!b->initialized) return false;
    b->scan_event_ok = result("scan event", btdevAcquireBleScanEvent(&b->scan_event));
    b->connection_event_ok = result("connection event", btdevAcquireBleConnectionStateChangedEvent(&b->connection_event));
    b->discovery_event_ok = result("service discovery event", btdevAcquireBleServiceDiscoveryEvent(&b->discovery_event));
    b->gatt_event_ok = result("GATT event", btdevAcquireBleGattOperationEvent(&b->gatt_event));
    if (!b->scan_event_ok || !b->connection_event_ok || !b->discovery_event_ok || !b->gatt_event_ok) return false;
    if (!discover_phone(b) || quit_requested) return false;
    b->service_registered = result("register service data path", btdevRegisterGattOperationNotification(&b->service_uuid));
    b->char_registered = result("register characteristic data path", btdevRegisterGattOperationNotification(&b->char_uuid));
    if (!b->service_registered && !b->char_registered) return false;
    if (!result("btdevConnectToGattServer", btdevConnectToGattServer(b->address))) return false;
    note("Waiting for BLE connection...");
    for (int i = 0; i < 50 && running(); i++) {
        BtdrvBleConnectionInfo peers[8] = {0};
        u8 count = 0;
        Result rc = btdevGetBleConnectionInfoList(peers, 8, &count);
        if (R_FAILED(rc)) { result("connection state", rc); break; }
        if (count > 8) count = 8;
        for (int j = 0; j < count; j++) {
            if (peers[j].connection_handle != UINT32_MAX && !memcmp(&peers[j].addr, &b->address, sizeof(b->address))) {
                b->handle = peers[j].connection_handle; b->connected = true; break;
            }
        }
        if (b->connected) break;
        svcSleepThread(200000000LL);
    }
    if (!b->connected) { note("BLE connection timed out."); return false; }
    note("BLE connected. Waiting for the relay characteristic...");
    bool discovery_seen = false, raw_seen = false;
    BtmGattCharacteristic previous = {0};
    for (int i = 0; i < 50 && running(); i++) {
        if (R_SUCCEEDED(eventWait(&b->discovery_event, 0))) {
            discovery_seen = true; note("Service discovery event observed.");
        }
        BtmGattService raw_service = {0};
        bool found = false;
        Result rc = btmuGetGattService(b->handle, &b->service_uuid, &raw_service, &found);
        if (R_SUCCEEDED(rc) && found) {
            BtmGattCharacteristic chars[100] = {0};
            u8 count = 0;
            rc = btmuGetGattCharacteristics(b->handle, raw_service.handle, chars, 100, &count);
            if (count > 100) count = 100;
            if (R_SUCCEEDED(rc)) for (u8 j = 0; j < count; j++) {
                if (!btdevGattAttributeUuidIsSame(&chars[j].uuid, &b->char_uuid)) continue;
                if (!raw_seen || memcmp(&previous, &chars[j], sizeof(previous))) {
                    dump_attribute("custom service", &raw_service, sizeof(raw_service));
                    dump_attribute("relay characteristic", &chars[j], sizeof(chars[j]));
                    previous = chars[j]; raw_seen = true;
                }
                btdevGattServiceCreate(&b->service, &raw_service.uuid, raw_service.handle,
                    b->handle, raw_service.instance_id, raw_service.end_group_handle, raw_service.primary_service);
                btdevGattCharacteristicCreate(&b->characteristic, &chars[j].uuid, chars[j].handle,
                    b->handle, chars[j].instance_id, chars[j].properties);
                b->characteristic_ok = true;
                break;
            }
            if (b->characteristic_ok && discovery_seen) break;
        }
        if (i == 49 && R_FAILED(rc)) result("GATT discovery", rc);
        svcSleepThread(200000000LL);
    }
    if (!b->characteristic_ok) { note("Relay characteristic unavailable."); return false; }
    note("Relay characteristic found; properties=0x%02x; discovery_event=%s", b->characteristic.properties, discovery_seen ? "yes" : "no");
    note("service_handle=%u service_instance=%u char_handle=%u char_instance=%u primary=%u",
         b->service.attr.handle, b->service.instance_id, b->characteristic.attr.handle,
         b->characteristic.instance_id, b->service.primary_service);
    if (b->service.instance_id > 255 || b->characteristic.instance_id > 255) {
        note("Unexpected instance ID; refusing to truncate it."); return false;
    }
    b->direct_gatt = (b->characteristic.properties & 0x0a) != 0x0a;
    if (b->direct_gatt) note("Metadata disagrees with the companion read/write service. Testing explicit bt requests; keeping reported properties unchanged.");
    return true;
}

/* Every connection, including diagnostic reconnects, needs a fresh local press.
 * No handshake or relay command is exchanged before approval. */
static bool approve_companion(void){
    note("Companion found. Approve this connection? Release A, then press A to approve; B rejects; + exits.");
    note("Keep only your intended companion app open. Approval expires in 60 seconds.");
    LrApprovalGate gate;lp_gate_start(&gate,armTicksToNs(armGetSystemTick())/1000000);
    while(running()){
        u64 buttons=padGetButtonsDown(&pad),held=padGetButtons(&pad);
        bool approved=lp_gate_poll(&gate,armTicksToNs(armGetSystemTick())/1000000,
            (buttons&HidNpadButton_A)!=0,(held&HidNpadButton_A)!=0,leave_requested);
        if(gate.closed){note(approved?"Connection approved on Switch.":"Connection rejected or approval timed out.");leave_requested=false;return approved;}
        svcSleepThread(10000000LL);
    }
    return false;
}

static bool relevant(const BtdrvLeEventInfo *e, const BtdrvGattAttributeUuid *uuid) {
    return btdevGattAttributeUuidIsSame(&e->uuid0, uuid) ||
           btdevGattAttributeUuidIsSame(&e->uuid1, uuid) ||
           btdevGattAttributeUuidIsSame(&e->uuid2, uuid);
}
