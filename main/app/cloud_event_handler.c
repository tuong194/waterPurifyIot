#include "cloud_event_handler.h"
#include "IOT_ErrorManager.h"
#include "IOT_Log.h"
#include "IOT_Platform.h"
#include "IOT_Core.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_CoreReporter.h"
#include "IOT_TimerMgmt.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "IOT_AppManager.h"
#include "esp_err.h"


static const char *TAG = "CORE_EVT";

uint8_t g_cloud_is_connected = 0;

static void app_device_set_state(uint16_t elemId, uint16_t attrId, uint8_t *attrValue, uint8_t lenAttrValue);

static iot_err_t resolveSetTargetEid(const IOT_EventStateSetRequest_t *setData, uint16_t *outEid, uint16_t *numChanged, uint16_t **changeId)
{
    if (setData == NULL || outEid == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    switch (setData->targetType)
    {
    case IOT_SET_TARGET_DEVICE:
    {
        IOT_LOGI(TAG, "==> ^-^ Control by unicast");
        dev_set_info_t devSetInfo = setData->devSetInfo;
        *outEid = setData->target.device.eid;
        IOT_LOGI(TAG, "Target EID: %04x,  Target Device Type: %d", *outEid, setData->targetDeviceType);
        for (int i = 0; i < devSetInfo.numOfElems; i++)
        {
            IOT_LOGI(TAG, "  Element ID: %04x", devSetInfo.elemList[i].elemId);
        }
        iot_log_hex_dump(LOG_LEVEL_INFO, devSetInfo.attrValue, devSetInfo.attrSize, "  Attribute Value Data:");
        *numChanged = devSetInfo.numOfElems;
        /* Update state for all requested elements */
        for (int i = 0; i < devSetInfo.numOfElems; i++)
        {
            app_device_set_state(devSetInfo.elemList[i].elemId, devSetInfo.attrId, devSetInfo.attrValue, devSetInfo.attrSize);
            (*changeId)[i] = devSetInfo.elemList[i].elemId;
        }
        
        return IOT_OK;
    }
    case IOT_SET_TARGET_LOCATION:
    {
        IOT_LOGI(TAG, "==> ^-^ Control by location");
        IOT_LOGW(TAG, "Group SET_STATE target %04x needs group DAO expansion", setData->target.group.groupEid);
        dev_set_info_t devSetInfo = setData->devSetInfo;

        uint16_t eid = 0;
        if (IOT_CoreGetRootEid(&eid) != IOT_OK)
        {
            IOT_LOGE(TAG, "Get Eid Failed");
            return IOT_ERR_INVALID_ARG;
        }
        *outEid = eid;

        IOT_DevCommonInfo_t outDevCommonInfo = {0};
        iot_err_t err_check = IOT_DaoDeviceGetCommonInfo(eid, &outDevCommonInfo);
        if(err_check != IOT_OK)
        {
            return IOT_ERR_INVALID_ARG;
        }else{
            printf("outDevCommonInfo.deviceType: %d, setData->targetDeviceType: %d \n", outDevCommonInfo.deviceType, setData->targetDeviceType);
            if(outDevCommonInfo.deviceType != (uint16_t)setData->targetDeviceType)
            {
                IOT_LOGE(TAG, "Unknown target device type");
                return IOT_ERR_INVALID_ARG;
            }
        }

        *numChanged = 1;
        uint8_t index = 0;
        for (size_t i = 0; i < 1; i++)
        {
            uint16_t elmId = i+1;
            (*changeId)[index++] = elmId;
            app_device_set_state(elmId, devSetInfo.attrId, devSetInfo.attrValue, devSetInfo.attrSize);
        }
        return IOT_OK;
    }

    case IOT_SET_TARGET_VOICE:
        IOT_LOGW(TAG, "Voice SET_STATE target needs devId-to-EID DAO lookup");
        return IOT_ERR_NOT_SUPPORTED;
    default:
        return IOT_ERR_INVALID_ARG;
    }
}
// ============================================================================
// STATE Event Handler
// ============================================================================

static iot_err_t IOT_ExCoreEventHandleState(IOT_CoreStateEvent_t *stateEvent)
{
    if (stateEvent == NULL)
    {
        IOT_LOGE(TAG, "Invalid stateEvent pointer");
        return IOT_ERR_INVALID_ARG;
    }

    iot_err_t iot_err = IOT_OK;

    switch (stateEvent->type)
    {
    case IOT_EVENT_TYPE_STATE_SET:
    {
        IOT_LOGW(TAG, "STATE event: SET_STATE");
        IOT_EventStateSetRequest_t *setData = &(stateEvent->data.setRequest);
        dev_set_info_t devSetInfo = setData->devSetInfo;

        uint16_t targetEid = 0;
        // Collect changed element IDs
        uint16_t changedIds[1];
        uint16_t *p = &changedIds[0];
        uint16_t numChanged = 0;
        iot_err = resolveSetTargetEid(setData, &targetEid, &numChanged, &p);
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to resolve set target");
            break;
        }

        IOT_DevIdentityInfo_t info = {0};
        iot_err = IOT_DaoDeviceGetIdentityInfo(targetEid, &info);
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to get device identity info from DAO for EID %04x", setData->devSetInfo.eid);
            break;
        }
        
        // Build full state with 2 elements for the report
        elemStateAttr_t elmAttr = {.attrId = 0x0001, .attrSize = 2, .attrValue = setData->devSetInfo.attrValue};
        devStateElemState_t elems = {.elemId = 0x0001, .elemType = 0x0001, .numOfAttr = 1, .attrList = &elmAttr};

        devStateElemsInfo_t fullState = {.numOfElem = 1, .elemList = &elems};

        IOT_CoreStateChangeDeviceData_t devData = {
            .eid = targetEid,
            .mac = info.mac,
            .macLen = info.macSize,
            .devId = info.devId,
            .devIdLen = DEVICE_ID_LEN,
            .fullState = &fullState,
            .numChangedElems = numChanged,
            .changedElemIds = changedIds,
            .changedAttrId = devSetInfo.attrId,
            .pushNotify = false,
        };

        IOT_CoreStateChangeReport_t report = {
            .devices = &devData,
            .numDevices = 1,
            .ctx = setData->ctx,
        };

        iot_err = IOT_CoreRequestReportStateChanged(&report); 
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to send state change report");
        }

        IOT_DaoDeviceFreeIdentityInfo(&info);

        // uint16_t newValue = devSetInfo.attrValue[0] << 8 | devSetInfo.attrValue[1];
        // A cloud SET is a local change to this device's attributes. If any of
        // the changed elements is in a sync-on-off group (subSmartType 71),
        // propagate the new value to our peers so they mirror it.
        // for (int i = 0; i < devSetInfo.numOfElems; i++)
        // {
        //     app_sync_broadcast_local(devSetInfo.elemList[i].elemId, newValue);
        // }
        break;
    }
    case IOT_EVENT_TYPE_STATE_GET:
    {
        IOT_LOGW(TAG, "STATE event: GET_STATE");
        break;
    }
    default:
        IOT_LOGW(TAG, "Unknown STATE event type: %d", stateEvent->type);
        iot_err = IOT_ERR_INVALID_ARG;
        break;
    }

    return iot_err;
}

// ============================================================================
// SMART Event Handler
// ============================================================================
// Automation events — triggers, schedules, bindings.
// These require your own DAO layer to persist trigger/schedule configurations.

static iot_err_t IOT_ExCoreEventHandleSmart(IOT_CoreSmartEvent_t *smartEvent)
{
    if (smartEvent == NULL)
    {
        IOT_LOGE(TAG, "Invalid smartEvent pointer");
        return IOT_ERR_INVALID_ARG;
    }

    iot_err_t iot_err = IOT_OK;
    esp_err_t esp_err = ESP_OK;

    switch (smartEvent->type)
    {
    case SMART_EVENT_ENABLE_DISABLE:
    {
        IOT_CoreSmartEventEnableDisableData_t *data = &smartEvent->data.enableDisable;
        IOT_LOGI(TAG, "SMART event SMART_EVENT_ENABLE_DISABLE, smid: %04x, en/dis: %d", data->smid, data->enabled);

        // REQUIRED: call after DAO update — core sends cloud ack from here
        IOT_CoreSmartCmdResponse_t resp = {
            .cmdType = IOT_SMART_CMD_ENABLE_DISABLE,
            .cfm     = 0,
            .result  = IOT_OK,
            .ctx     = data->ctx,
        };
        IOT_CoreRespondSmartCmd(&resp);
        break;
    }
    case SMART_EVENT_REMOVE_ANNOUNCE:
    {
        IOT_CoreSmartEventRemoveAnnounceData_t *data = &smartEvent->data.removeAnnounce;
        IOT_LOGI(TAG, "SMART event: REMOVE_ANNOUNCE, SMID=%04x", data->smid);
        // TODO: Remove automation with this SMID from your DAO

        break;
    }
    case SMART_EVENT_ACTIVE_BY_USER:
    {
        IOT_CoreSmartEventActiveByUserData_t *data = &smartEvent->data.activeByUser;
        IOT_LOGI(TAG, "SMART event: ACTIVE_BY_USER, SMID=%04x", data->smid);
        // TODO: Execute automation commands for this SMID

        break;
    }
    case SMART_EVENT_ACTIVE_BY_SCHEDULE:
    {
        IOT_CoreSmartEventActiveByScheduleData_t *data = &smartEvent->data.activeBySchedule;
        IOT_LOGI(TAG,
                 "SMART event: ACTIVE_BY_SCHEDULE, SMID=%04x, MinuteOfWeek=%u, UUID_Len=%u",
                 data->smid,
                 data->minuteOfWeekScheduleUTC0,
                 data->uuidv4Len);
        if (data->uuidv4 != NULL && data->uuidv4Len > 0)
        {
            iot_log_hex_dump(LOG_LEVEL_INFO, data->uuidv4, data->uuidv4Len, "Schedule UUID:");
        }
        // TODO: Execute scheduled automation commands

        break;
    }
    case SMART_EVENT_ACTIVE_BY_TRIGGER:
    {
        IOT_CoreSmartEventActiveByTriggerData_t *data = &smartEvent->data.activeByTrigger;
        IOT_LOGI(TAG,
                 "SMART event: ACTIVE_BY_TRIGGER, SMID=%04x, TriggerType=%04x, AttrSize=%u, ActiveSection=%02x, "
                 "ControlCount=%u",
                 data->smid,
                 data->triggerType,
                 data->attrValue.attrSize,
                 data->activeSection,
                 data->controlCount);
        IOT_LOGI(TAG,
                 "  TriggerKey: ownerEid=%04x (count=%u), extEid=%04x (count=%u)",
                 data->triggerKey.ownerEid,
                 data->triggerKey.ownerEidCount,
                 data->triggerKey.extEid,
                 data->triggerKey.extEidCount);
        IOT_LOGI(TAG, "  TimeConfig: type=%04x, value=%04x", data->timeConfig.timCfgType, data->timeConfig.timCfgValue);
        if (data->attrValue.attrValue != NULL && data->attrValue.attrSize > 0)
        {
            iot_log_hex_dump(
                LOG_LEVEL_INFO, data->attrValue.attrValue, data->attrValue.attrSize, "Trigger Attr Value:");
        }
        // TODO: Evaluate trigger condition and execute commands if matched

        break;
    }
    case SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE:
    {
        IOT_CoreSmartEventTriggerUpdateAnnounceData_t *data = &smartEvent->data.triggerUpdateAnnounce;
        IOT_LOGI(TAG,
                 "SMART event: TRIGGER_UPDATE_ANNOUNCE, EID=%04x, RootEID=%04x, ElemID=%04x, ElemType=%04x",
                 data->eid,
                 data->rootEid,
                 data->elemId,
                 data->elemType);
        IOT_LOGI(TAG,
                 "  TriggerKey: ownerEid=%04x (count=%u), extEid=%04x (count=%u)",
                 data->triggerKey.ownerEid,
                 data->triggerKey.ownerEidCount,
                 data->triggerKey.extEid,
                 data->triggerKey.extEidCount);
        // TODO: Update trigger configuration in your DAO
        break;
    }
    case SMART_EVENT_TRIGGER_MODE:
    {
        IOT_CoreSmartEventTriggerModeData_t *data = &smartEvent->data.triggerMode;
        IOT_LOGI(TAG, "SMART event: TRIGGER_MODE, Type=%d, Mode=%02x", data->type, data->mode);

        switch (data->type)
        {
        case IOT_CORE_SMART_TRIGGER_MODE_SET_TRIGGER:
            IOT_LOGI(TAG,
                     "  SetTrigger: minuteDisable=%u, SMID=%04x",
                     data->data.setTrigger.minuteDisable,
                     data->data.setTrigger.smid);
            break;
        case IOT_CORE_SMART_TRIGGER_MODE_SCHEDULE:
            IOT_LOGI(TAG, "  Schedule: SMID=%04x", data->data.schedule.smid);
            break;
        case IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_AUTOMATION:
        case IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_ALL_AUTOMATION:
            IOT_LOGI(TAG, "  EnableDisable: EID=%04x", data->data.enableDisableAutomation.eid);
            break;
        default:
            IOT_LOGW(TAG, "  Unknown trigger mode type: %d", data->type);
            break;
        }
        // TODO: Update trigger mode in your DAO
        // REQUIRED: call after DAO update — core sends cloud ack from here
        IOT_CoreSmartCmdResponse_t resp = {
            .cmdType = IOT_SMART_CMD_TRIGGER_MODE,
            .cfm     = 0,
            .result  = IOT_OK,
            .ctx     = data->ctx,
        };
        IOT_CoreRespondSmartCmd(&resp);
        break;
    }
    case SMART_EVENT_BIND_TRIGGER:
    {
        IOT_CoreSmartEventBindTriggerData_t *data = &smartEvent->data.bindTrigger;
        IOT_LOGI(
            TAG, "SMART event: BIND_TRIGGER, IsDeviceUTC=%d, TimeZone=%.2f", data->isDeviceUtc, data->timeZoneFraction);
        IOT_LOGI(TAG,
                 "  BindTriggerKey: ownerEid=%04x (count=%u), extEid=%04x (count=%u)",
                 data->bindTriggerKey.ownerEid,
                 data->bindTriggerKey.ownerEidCount,
                 data->bindTriggerKey.extEid,
                 data->bindTriggerKey.extEidCount);
        IOT_LOGI(TAG,
                 "  TriggerElmInfo: type=%04x, smid=%04x, smartType=%04x, subSmartType=%04x, eid=%04x, elmId=%04x, condition=%d",
                 data->bindTriggerElmInfo.type,
                 data->bindTriggerElmInfo.smid,
                 data->bindTriggerElmInfo.smartType,
                 data->bindTriggerElmInfo.subSmartType,
                 data->bindTriggerElmInfo.eid,
                 data->bindTriggerElmInfo.elmId,
                 data->bindTriggerElmInfo.condition);
        IOT_LOGI(TAG,
                 "  PrimaryAttr: attrId=%04x, attrSize=%u",
                 data->primaryAttrValue.attrId,
                 data->primaryAttrValue.attrSize);
        IOT_LOGI(TAG,
                 "  SecondaryAttr: elmId=%04x, attrId=%04x, attrSize=%u",
                 data->elmId,
                 data->secondaryAttrValue.attrId,
                 data->secondaryAttrValue.attrSize);
        IOT_LOGI(TAG,
                 "  TriggerMix: eidMix=%04x, rootEidMix=%04x, elemIdMix=%04x, timeStart=%u, timeStop=%u, weekday=%d",
                 data->eidMix,
                 data->rootEidMix,
                 data->elemIdMix,
                 data->timeStart,
                 data->timeStop,
                 data->weekDay);
        IOT_LOGI(TAG,
                 "  TimeCfgMix: timCfgType=%04x, timCfgValue=%04x",
                 data->timeConfigMix.timCfgType,
                 data->timeConfigMix.timCfgValue);
        if (data->primaryAttrValue.attrValue != NULL && data->primaryAttrValue.attrSize > 0)
        {
            iot_log_hex_dump(LOG_LEVEL_INFO,
                             data->primaryAttrValue.attrValue,
                             data->primaryAttrValue.attrSize,
                             "Primary Attr Value:");
        }
        if (data->secondaryAttrValue.attrValue != NULL && data->secondaryAttrValue.attrSize > 0)
        {
            iot_log_hex_dump(LOG_LEVEL_INFO,
                             data->secondaryAttrValue.attrValue,
                             data->secondaryAttrValue.attrSize,
                             "Secondary Attr Value:");
        }
        // TODO: Store trigger binding in your DAO (IOT_DaoSmartTriggerMgmt equivalent)

        // REQUIRED: call after DAO save — core sends cloud ack from here
        IOT_CoreSmartCmdResponse_t resp = {
            .cmdType = IOT_SMART_CMD_BIND_TRIGGER,
            .cfm     = 1,    // replace with your device-owned cfm computed above
            .result  = IOT_OK,
            .ctx     = data->ctx,
        };
        IOT_CoreRespondSmartCmd(&resp);
        break;
    }
    case SMART_EVENT_UNBOUND_TRIGGER:
    {
        IOT_CoreSmartEventUnboundTriggerData_t *data = &smartEvent->data.unboundTrigger;
        IOT_LOGI(TAG, "SMART event: UNBOUND_TRIGGER, EID=%04x, SMID=%04x", data->eid, data->smid);
        // TODO: Remove trigger binding from your DAO

        // REQUIRED: call after DAO delete — core sends cloud ack from here
        IOT_CoreSmartCmdResponse_t resp = {
            .cmdType = IOT_SMART_CMD_UNBOUND_TRIGGER,
            .cfm     = 0,
            .result  = IOT_OK,
            .ctx     = data->ctx,
        };
        IOT_CoreRespondSmartCmd(&resp);
        break;
    }
    case SMART_EVENT_BIND_CMD:
    {
        IOT_CoreSmartEventBindCmdData_t *data = &smartEvent->data.bindCmd;
        IOT_LOGI(TAG,
                 "SMART event: BIND_CMD, SMID=%04x, EID=%04x, RootEID=%04x, Protocol=%04x, Filter=%04x, CmdSize=%u",
                 data->smid,
                 data->eid,
                 data->rootEid,
                 data->prtc,
                 data->filter,
                 data->cmdSize);
        if (data->linkedId != NULL)
        {
            IOT_LOGI(TAG, "  LinkedID: %s", data->linkedId);
        }
        if (data->attrValueList != NULL && data->cmdSize > 0)
        {
            for (uint16_t i = 0; i < data->cmdSize; i++)
            {
                IOT_CoreSmartBlockAttrValueReverseDelayElm_t *item = &data->attrValueList[i];
                IOT_LOGI(TAG,
                         "  Cmd[%u]: attrId=%04x, attrSize=%u, reverse=%04x, delay=%u, elmId=%04x",
                         i,
                         item->attrValue.attrId,
                         item->attrValue.attrSize,
                         item->reverse,
                         item->delay,
                         item->elmId);
                if (item->attrValue.attrValue != NULL && item->attrValue.attrSize > 0)
                {
                    iot_log_hex_dump(
                        LOG_LEVEL_INFO, item->attrValue.attrValue, item->attrValue.attrSize, "  Attr Value:");
                }
            }
        }
        // TODO: Store command binding in your DAO (IOT_DaoSmartMgmt equivalent)

        // REQUIRED: call after DAO save — core sends cloud ack from here
        IOT_CoreSmartCmdResponse_t resp = {
            .cmdType = IOT_SMART_CMD_BIND_CMD,
            .cfm     = 1,    // replace with your device-owned cfm computed above
            .result  = IOT_OK,
            .ctx     = data->ctx,
        };
        IOT_CoreRespondSmartCmd(&resp);
        break;
    }
    case SMART_EVENT_UNBOUND_CMD:
    {
        IOT_CoreSmartEventUnboundCmdData_t *data = &smartEvent->data.unboundCmd;
        IOT_LOGI(TAG, "SMART event: UNBOUND_CMD, SMID=%04x, EID=%04x", data->smid, data->eid);
        // TODO: Remove command binding from your DAO

        // REQUIRED: call after DAO delete — core sends cloud ack from here
        IOT_CoreSmartCmdResponse_t resp = {
            .cmdType = IOT_SMART_CMD_UNBOUND_CMD,
            .cfm     = 0,
            .result  = IOT_OK,
            .ctx     = data->ctx,
        };
        IOT_CoreRespondSmartCmd(&resp);
        break;
    }
    case SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE:
    {
        IOT_CoreSmartEventScheduleUpdateAnnounceData_t *data = &smartEvent->data.scheduleUpdateAnnounce;
        IOT_LOGI(TAG, "SMART event: SCHEDULE_UPDATE_ANNOUNCE, SMID=%u", data->smid);
        // TODO: Store/update schedule in your DAO (IOT_DaoSmartScheduleMgmt equivalent)
        break;
    }
    case SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE:
    {
        IOT_LOGI(TAG, "SMART event: SCHEDULE_REMOVE_ANNOUNCE");
        // TODO: Remove schedule from your DAO
        break;
    }
    case SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF:
    {
        // Sync-on-off (subSmartType 71): a peer device that shares this SMID
        // broadcast a new ON/OFF value on the location command topic. Mirror the
        // value onto EVERY element your DAO bound to this SMID — one-to-many,
        // with no cloud round-trip.
        //
        // Core gives you three things:
        //   data->smid                 — which sync-on-off group this belongs to
        //   data->attrValue.attrId     — which attribute to update (ON/OFF)
        //   data->attrValue.attrValue  — the new value bytes (borrowed pointer,
        //   data->attrValue.attrSize     valid only for this synchronous call)
        //   data->senderEid            — root EID of the broadcaster (0 = legacy sender)
        IOT_CoreSmartEventActiveBySyncOnOffData_t *data = &smartEvent->data.activeBySyncOnOff;
        IOT_LOGI(TAG,
                 "SMART event: ACTIVE_BY_SYNC_ON_OFF, SMID=%04x, AttrId=%04x, AttrSize=%u, SenderEID=%04x",
                 data->smid,
                 data->attrValue.attrId,
                 data->attrValue.attrSize,
                 data->senderEid);

        // Echo suppression: this broadcast lands on every device in the location,
        // including the one that sent it. Drop it if we are the originator.

        break;
    }
    case SMART_EVENT_CHECK_SYNC:
    {
        IOT_CoreSmartEventCheckSyncData_t *data = &smartEvent->data.checkSync;
        IOT_LOGI(TAG, "SMART event: CHECK_SYNC, SMID=%04x", data->smid);
        // TODO: Look up trigger from your DAO by smid
        // REQUIRED: call after DAO lookup — core sends cloud ack from here
        IOT_CoreSmartCheckSyncResponse_t resp = {
            .smid       = data->smid,
            .exists     = false,  // set true and fill triggerEid/enabled/cfm if found in DAO
            .triggerEid = 0,
            .enabled    = false,
            .cfm        = 0,
            .ctx        = data->ctx,
        };
        IOT_CoreRespondSmartCheckSync(&resp);
        break;
    }
    case SMART_EVENT_REPORT_COND_STATE:
    {
        // Cond-group (subSmartType 2048): a peer that owns one seat of a K-of-N
        // group announced its own condition bit. Core carries the message; the
        // scoreboard (mask / N / K / version) is YOURS to keep and persist.
        //
        //   data->smid          — which group
        //   data->condIndex     — the SENDER's seat (0..7)
        //   data->mask          — the sender's latched mask; only the sender's OWN
        //                         bit, (mask >> condIndex) & 1, is authoritative
        //   data->condCount/N, data->fireCount/K, data->version — the sender's shape
        //   data->departing     — the sender is LEAVING this group
        IOT_CoreSmartCondStatus_t *data = &smartEvent->data.reportCondState;
        IOT_LOGI(TAG,
                 "SMART event: REPORT_COND_STATE, SMID=%04x, seat=%u, mask=%02x, N=%u, K=%u, ver=%u, departing=%u",
                 data->smid,
                 data->condIndex,
                 data->mask,
                 data->condCount,
                 data->fireCount,
                 data->version,
                 data->departing);

        // Everything the merge needs is in app_cond_merge_peer(): echo suppression,
        // the version rule, own-bit-only, seat retirement, and the fire check.

        break;
    }
    case SMART_EVENT_SYNC_COND_REQUEST:
    {
        // A peer rebooted and is asking + announcing in one message. Answer ONLY
        // for the groups where your state differs from theirs — unicast, so the
        // whole location is not woken up by every reboot.
        IOT_CoreSmartEventSyncCondRequestData_t *data = &smartEvent->data.syncCondRequest;
        IOT_LOGI(TAG,
                 "SMART event: SYNC_COND_REQUEST, groups=%u, requesterEID=%04x",
                 data->groupCount,
                 data->requesterEid);

        IOT_CoreSmartReportCondState_t answers[4];
        uint16_t answerCount = 0;

        if (answerCount > 0)
        {
            // Unicast back to the asker — not the whole location.
            IOT_CoreRequestSmartSyncCondResponse(answers, answerCount, data->requesterEid);
            IOT_LOGI(TAG, "  answered %u group(s) to EID=%04x", answerCount, data->requesterEid);
        }
        break;
    }
    case SMART_EVENT_COND_NOT_MEMBER:
    {
        // A peer answered our sync request with "I hold no trigger for this SMID".
        IOT_CoreSmartEventCondNotMemberData_t *data = &smartEvent->data.condNotMember;
        IOT_LOGI(TAG, "SMART event: COND_NOT_MEMBER, SMID=%04x, responderEID=%04x", data->smid, data->responderEid);

        // Positive confirmation that a seat is gone — as opposed to silence, which
        // only means the peer is offline. The reply does NOT carry the seat index
        // (the responder deleted its trigger and no longer knows it), so a real
        // implementation maps responderEid -> seat from its own stored membership.

        break;
    }
    default:
        IOT_LOGW(TAG, "Unknown SMART event type: %d", smartEvent->type);
        iot_err = IOT_ERR_INVALID_ARG;
        break;
    }

    return iot_err;
}

// ============================================================================
// DEVICE Event Handler
// ============================================================================

static iot_err_t IOT_ExCoreEventHandleDevice(IOT_CoreDeviceEvent_t *deviceEvent)
{
    static uint16_t groupId = 0;
    if (deviceEvent == NULL)
    {
        IOT_LOGE(TAG, "Invalid deviceEvent pointer");
        return IOT_ERR_INVALID_ARG;
    }

    iot_err_t iot_err = IOT_OK;

    switch (deviceEvent->type)
    {
    case DEV_EVENT_BOOTED:
    {
        IOT_LOGI(TAG, "DEVICE event: BOOTED");
        // TODO: Restore hardware state from your DAO if device is provisioned
        break;
    }
    case DEV_EVENT_IDENTIFICATION:
    {
        IOT_LOGI(TAG, "DEVICE event: IDENTIFICATION — blink LED to identify device");
        // TODO: Blink indicator LED or make device identifiable

        break;
    }
    case DEV_EVENT_PROVISION_COMPLETED:
    {
        IOT_LOGI(TAG, "DEVICE event: PROVISION_COMPLETED — device is cloud-connected");
        // TODO: Initialize device state, set default values
        break;
    }
    case DEV_EVENT_NEW_DEVICE_JOINED:
    {
        IOT_LOGI(TAG, "DEVICE event: NEW_DEVICE_JOINED");
        IOT_LOGW(TAG, "  New Device Joined Event: handle new device joined if needed.");
        IOT_CoreDeviceEventNewDeviceJoinedData_t *joinData = &deviceEvent->data.newDeviceJoined;
        switch (joinData->phase)
        {
        case IOT_DEV_JOIN_PHASE_DEVICE_INFO:
        {
            IOT_LOGI(TAG,
                     "DEVICE event: NEW DEVICE JOINED, EID: %04x, PHASE: DEVICE_INFO",
                     joinData->data.deviceInfo.eid);

            uint16_t eid = joinData->data.deviceInfo.eid;
            if (iot_err != IOT_OK)
            {
                IOT_LOGE(TAG, "Failed to save device EID to DAO");
            }
            printf("set ele ID : %04x\n", eid);

            // Set device common info
            IOT_DevCommonInfo_t commonInfo = {
                .deviceType = joinData->data.deviceInfo.deviceType,
                .protocol = joinData->data.deviceInfo.protocol,
                .nwkAddr = joinData->data.deviceInfo.nwkAddr,
                .rootEid = joinData->data.deviceInfo.rootEid,
            };
            iot_err = IOT_DaoDeviceSetCommonInfo(eid, &commonInfo);
            if (iot_err != IOT_OK)
            {
                IOT_LOGE(TAG, "Failed to store common info for EID %04x", eid);
            }

            // Set device identity info
            uint8_t macWithPrefix[8];
            macWithPrefix[0] = joinData->data.deviceInfo.macType;
            macWithPrefix[1] = 6;
            memcpy(&macWithPrefix[2], joinData->data.deviceInfo.mac, 6);
            IOT_DevIdentityInfo_t identityInfo = {
                .macType = joinData->data.deviceInfo.macType,
                .mac = macWithPrefix,
                .macSize = 6,
            };
            memcpy(identityInfo.devId, joinData->data.deviceInfo.devId, DEVICE_ID_LEN);
            iot_err_t identityErr = IOT_DaoDeviceSetIdentityInfo(eid, &identityInfo);
            if (identityErr != IOT_OK)
            {
                IOT_LOGE(TAG, "Failed to store identity info for EID %04x", eid);
                if (iot_err == IOT_OK)
                {
                    iot_err = identityErr;
                }
            }
            IOT_DaoDeviceSetCommonInfo(eid, &commonInfo);

            // Add to groups
            groupId = joinData->data.deviceInfo.groupId;
            

            // REQUIRED: call after each DAO phase — core sends cloud ack from here
            IOT_CoreDeviceCmdResponse_t resp = {
                .cmdType = IOT_DEVICE_CMD_SYNC_DEV_JOINED,
                .result  = iot_err,
                .cmdData2 = joinData->cmdData2,
                .ctx     = joinData->ctx,
            };
            IOT_CoreRespondDeviceCmd(&resp);
            break;
        }
        case IOT_DEV_JOIN_PHASE_ELM_INFO:
        {
            IOT_LOGI(TAG,
                     "DEVICE event: NEW DEVICE JOINED, EID: %04x, PHASE: ELM_INFO",
                     joinData->data.elmInfo.eid);
            uint16_t eid = joinData->data.elmInfo.eid;
            IOT_LOGI(TAG, "DEVICE event: NEW DEVICE JOINED, EID: %04x, PHASE: ELM_INFO", eid);
            IOT_LOGW(TAG, "  Number of Elements, ELM_STATE MUST BE HANDLE SAVING TO DAO");

            // REQUIRED: call after DAO save — core sends cloud ack from here
            IOT_CoreDeviceCmdResponse_t resp = {
                .cmdType = IOT_DEVICE_CMD_SYNC_DEV_JOINED,
                .result  = IOT_OK,
                .cmdData2 = joinData->cmdData2,
                .ctx     = joinData->ctx,
            };
            IOT_CoreRespondDeviceCmd(&resp);
            break;
        }
        case IOT_DEV_JOIN_PHASE_SYNC_COMPLETE:
        {
            // to be updated.

            break;
        }
        default:
            break;
        }
        break;
    }
    case DEV_EVENT_DEVICE_REMOVED:
    {
        uint16_t eid = deviceEvent->data.deviceRemoved.eid;
        bool isRootDevice = deviceEvent->data.deviceRemoved.isRootDevice;

        IOT_LOGI(TAG, "DEVICE event: DEVICE_REMOVED, EID: %04x, isRoot: %d", eid, isRootDevice);

        if (isRootDevice)
        {
            IOT_LOGI(TAG, "Root device removed, restarting chip...");
            // TODO: Clear all application state, reset hardware
        }
        else
        {
            // Non-root device - remove from DAO storage
            // REQUIRED: call after DAO delete — core sends cloud ack from here
            IOT_CoreDeviceCmdResponse_t resp = {
                .cmdType = IOT_DEVICE_CMD_SYNC_DEV_REMOVED,
                .result  = IOT_OK,
                .cmdData2 = deviceEvent->data.deviceRemoved.cmdData2,
                .ctx     = deviceEvent->data.deviceRemoved.ctx,
            };
            IOT_CoreRespondDeviceCmd(&resp);
        }
        // RAL_NOTE: KICK_OUT
        iot_err = IOT_ApplicationRestartChip();
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to restart chip");
        }

        break;
    }
    case DEV_EVENT_GROUP_BIND:
    {
        IOT_CoreDeviceEventGroupBindData_t *bindData = &deviceEvent->data.groupBind;
        IOT_LOGI(TAG, "DEVICE event: GROUP_BIND, EID=%04x, newGroup=%04x, oldGroup=%04x, elmLen=%u",
                 bindData->eid, bindData->newGroupId, bindData->oldGroupId, bindData->elemDataLen);

        // TODO: Remove EID from oldGroupId in your DAO (if oldGroupId != 0)
        // TODO: Add EID to newGroupId in your DAO, with optional element bindings (elemData/elemDataLen)
        //
        // REQUIRED: call after DAO update — core sends cloud ack from here
        bool hasElms = (bindData->elemDataLen > 0);
        IOT_CoreDeviceCmdResponse_t resp = {
            .cmdType  = hasElms ? IOT_DEVICE_CMD_BIND_GRP_ELMS : IOT_DEVICE_CMD_BIND_GRP,
            .cmdData2 = bindData->cmdData2,
            .result   = IOT_OK,
            .ctx      = bindData->ctx,
        };
        IOT_CoreRespondDeviceCmd(&resp);
        break;
    }
    case DEV_EVENT_GROUP_UNBIND:
    {
        IOT_CoreDeviceEventGroupUnbindData_t *unbindData = &deviceEvent->data.groupUnbind;
        IOT_LOGI(TAG, "DEVICE event: GROUP_UNBIND, EID=%04x, group=%04x",
                 unbindData->eid, unbindData->groupId);

        // TODO: Remove EID from groupId in your DAO
        //
        // REQUIRED: call after DAO delete — core sends cloud ack from here
        IOT_CoreDeviceCmdResponse_t resp = {
            .cmdType  = IOT_DEVICE_CMD_UNBOUND_GRP,
            .cmdData2 = unbindData->cmdData2,
            .result   = IOT_OK,
            .ctx      = unbindData->ctx,
        };
        IOT_CoreRespondDeviceCmd(&resp);
        break;
    }

    case DEV_EVENT_DEVICE_ATTR_SET:
    {
        IOT_CoreDeviceEventAttrSetData_t *attrSet = &deviceEvent->data.attrSet;
        IOT_LOGI(TAG, "DEVICE event: DEVICE_ATTR_SET, EID=%04x, ELM=%04x, attr=%04x, len=%u",
                 attrSet->eid, attrSet->elm, attrSet->attrId, attrSet->attrDataLen);
        
        printf("[CORE EVT] data setting: ");
        for (size_t i = 0; i < attrSet->attrDataLen; i++)
        {
            printf(" %02x", attrSet->attrData[i]);
        }
        printf("\n");
        
        // app_device_set_state(attrSet->elm, attrSet->attrId, &attrSet->attrData[0], attrSet->attrDataLen);
        // dev_rsp_state_to_cloud(attrSet->elm, attrSet->attrId, &attrSet->attrData[0], attrSet->attrDataLen);

        // TODO: Persist the attribute value in your DAO.
        // REQUIRED: call after DAO update — core sends cloud ack from here.
        IOT_CoreDeviceCmdResponse_t resp = {
            .cmdType  = IOT_DEVICE_CMD_DEVICE_ATTR,
            .cmdData2 = 0,
            .result   = IOT_OK,
            .ctx      = attrSet->ctx,
        };
        IOT_CoreRespondDeviceCmd(&resp);
        break;
    }

    case DEV_EVENT_WIFI_CONNECTED:
    {
        IOT_LOGW(TAG, "DEVICE event: WIFI_CONNECTED, IP: %08lx",
                 (unsigned long)deviceEvent->data.wifiConnected.ip);
        break;
    }
    case DEV_EVENT_WIFI_DISCONNECTED:
    {
        IOT_LOGW(TAG, "DEVICE event: WIFI_DISCONNECTED, reason: %d",
                 deviceEvent->data.wifiDisconnected.reason);
        break;
    }
    case DEV_EVENT_CLOUD_CONNECTED:
    {
        IOT_LOGI(TAG, "DEVICE event: CLOUD_CONNECTED");
        // TODO: Sync device state to cloud if needed
        g_cloud_is_connected = 1;
        // app_cond_resync_all();
        // esp_event_post(EVENT_BASE_COMMON, EVENT_CLOUD_CONNECTED, NULL, 0, pdMS_TO_TICKS(10));
        break;
    }
    case DEV_EVENT_CLOUD_DISCONNECTED:
    {
        g_cloud_is_connected = 0;
        IOT_LOGW(TAG, "DEVICE event: CLOUD_DISCONNECTED");
        break;
    }
    case DEV_EVENT_CLOUD_CONNECTING:
    {
        IOT_LOGW(TAG, "DEVICE event: CLOUD_CONNECTING");
        break;
    }
    default:
        IOT_LOGW(TAG, "Unknown DEVICE event type: %d", deviceEvent->type);
        iot_err = IOT_ERR_INVALID_ARG;
        break;
    }

    return iot_err;
}



// ============================================================================
// OS Event Handler — reboot and firmware update
// ============================================================================

static void app_reboot_timer_cb(void *userData)
{
    (void) userData;
    IOT_LOGI(TAG, "Rebooting device...");
    IOT_ApplicationRestartChip();
}

/**
 * @brief Run a firmware update that iotcore has accepted on our behalf.
 *
 * iotcore parses the cloud's update command, acknowledges it and persists the
 * request — but it does NOT download. Only your SDK knows what heap this
 * application is holding, and that is what decides whether a download can
 * succeed at all.
 *
 * WHY HEAP AND NOT FREE MEMORY: the TLS download needs one CONTIGUOUS block of
 * roughly 16.7 KB. A device reporting 80 KB free but a fragmented heap still
 * fails, with `alloc(16749 bytes) failed` / -0x7F00 right after the download
 * starts. Releasing the BLE stack usually frees tens of KB in one piece and is
 * the cheapest fix; aim for a largest free block of ~25 KB before starting.
 * Do NOT try to fix this by lowering CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN below
 * 16384 — that trades the allocation failure for a mid-download read error once
 * the server sends a record larger than the new cap.
 *
 * Passing IOT_CoreOtaPlatformProgressCb as the progress callback leaves you with
 * no protocol bookkeeping at all: core maps every platform status to the right
 * cloud notification, records the target version, and marks the request applied
 * so that the next boot verifies it.
 */
static iot_err_t app_handle_ota_requested(const IOT_CoreOsEventOtaRequestedData_t *ota)
{
    if (ota == NULL || ota->url == NULL || ota->url[0] == '\0')
    {
        IOT_LOGE(TAG, "OTA request carries no URL");
        return IOT_ERR_INVALID_ARG;
    }

    IOT_LOGI(TAG, "OTA requested - releasing BLE heap before download");

    // Mute the change-WiFi BLE fallback BEFORE tearing the stack down: if WiFi
    // drops mid-download, the fallback would otherwise try to advertise on a
    // stack that no longer exists.
    IOT_CoreChangeWifiSuspendBle();
    IOT_CoreBleDeinit();

    iot_err_t err = IOT_AppManagerStartUpdateProcess(ota->url, ota->cert, ota->certLen,
                                                     ota->authToken, ota->authTokenLen,
                                                     NULL, IOT_CoreOtaPlatformProgressCb);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to start OTA download: %s", iot_err_to_name(err));
        // Nothing was downloaded and no reboot is coming: re-arm the fallback,
        // then drop the request so the cloud is answered rather than left waiting.
        IOT_CoreChangeWifiResumeBle();
        IOT_CoreOtaClearPending();
    }
    return err;
}

/**
 * DEFERRING THE DOWNLOAD TO A LEANER BOOT
 *
 * Releasing BLE is often enough. If your application holds other large,
 * long-lived allocations that it cannot drop while running, download on a boot
 * that never creates them instead. The request is already persisted by core, so
 * it survives the reboot:
 *
 *   // 1. In the handler above, instead of downloading now:
 *   //      app_set_ota_boot_flag(true);   // your own NVS flag
 *   //      IOT_ApplicationRestartChip();
 *   //    Do NOT call IOT_CoreOtaClearPending() here — that DECLINES the update
 *   //    and tells the cloud so. Deferring means leaving it pending.
 *   //
 *   // 2. Early in app_main, before starting your heavy services:
 *   //      if (app_get_ota_boot_flag())
 *   //      {
 *   //          app_set_ota_boot_flag(false);   // clear first: never loop on a
 *   //                                          // download that keeps failing
 *   //          IOT_CoreOtaRequest_t req;
 *   //          if (IOT_CoreOtaGetPending(&req) == IOT_OK)
 *   //          {
 *   //              IOT_AppManagerStartUpdateProcess(req.url, req.cert, req.certLen,
 *   //                                               req.authToken, req.authTokenLen,
 *   //                                               NULL, IOT_CoreOtaPlatformProgressCb);
 *   //              IOT_CoreOtaFreePending(&req);
 *   //          }
 *   //      }
 *
 * Two things to know about this path. The auth token may have expired while the
 * device rebooted; report the failure rather than retrying silently, so the
 * cloud issues a fresh one. And the download needs a network connection, so a
 * "lean" boot must still bring up WiFi — core reports the outcome on the next
 * boot that has one.
 *
 * FIRMWARE ROLLBACK: if you enable CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, a
 * freshly-updated image boots in a pending-verify state and the bootloader
 * reverts to the previous firmware unless the app confirms itself. Call
 * IOT_AppManagerMarkAppValid() once the device has demonstrably settled — after
 * the cloud connection is up, not in app_main. Enabling that option without
 * calling it makes every OTA revert on its second boot.
 */

static iot_err_t IOT_ExCoreEventHandleOs(IOT_CoreOsEvent_t *osEvent)
{
    if (osEvent == NULL)
    {
        IOT_LOGE(TAG, "Invalid osEvent pointer");
        return IOT_ERR_INVALID_ARG;
    }

    switch (osEvent->type)
    {
    case OS_EVENT_REBOOT:
        // Deferred rather than immediate: the reply to the command that asked
        // for this reboot still has to leave the device.
        IOT_LOGW(TAG, "[OS_EVENT_REBOOT] OS event: REBOOT requested, restarting in 500ms");
        if (IOT_TimerInsertCb(app_reboot_timer_cb, NULL, 500) == IOT_TIMER_HANDLE_INVALID)
        {
            IOT_LOGE(TAG, "Failed to schedule reboot timer");
            return IOT_ERR_NO_MEM;
        }
        break;

    case OS_EVENT_OTA_REQUESTED:{
        IOT_LOGW(TAG, "[OS_EVENT_OTA_REQUESTED] ...");
        // esp_event_post(EVENT_BASE_COMMON, EVENT_OTA_REQUEST, NULL, 0, pdMS_TO_TICKS(10));
        // break;
        return app_handle_ota_requested(&osEvent->data.otaRequested);
    }
    default:
        IOT_LOGW(TAG, "Unknown OS event type: %d", osEvent->type);
        break;
    }

    return IOT_OK;
}

// ============================================================================
// Event Dispatcher
// ============================================================================

void eventCallback(void *arg, const IOT_CoreEvent_t *event)
{
    iot_err_t iot_err = IOT_OK;
    switch (event->category)
    {
    case IOT_EVENT_STATE:
    {
        iot_err = IOT_ExCoreEventHandleState((IOT_CoreStateEvent_t *)&event->evt.state);
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to handle STATE event");
        }
        break;
    }
    case IOT_EVENT_DEVICE:
    {
        iot_err = IOT_ExCoreEventHandleDevice((IOT_CoreDeviceEvent_t *)&event->evt.device);
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to handle DEVICE event");
        }
        break;
    }
    case IOT_EVENT_SMART:
    {
        iot_err = IOT_ExCoreEventHandleSmart((IOT_CoreSmartEvent_t *)&event->evt.smart);
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to handle SMART event");
        }
        break;
    }
    case IOT_EVENT_MESH:
        // IOT_LOGI(TAG, "Received MESH event");
        break;
    case IOT_EVENT_SETTING:
        // IOT_LOGI(TAG, "Received SETTING event");
        break;
    case IOT_EVENT_OS:
        iot_err = IOT_ExCoreEventHandleOs((IOT_CoreOsEvent_t *) &event->evt.os);
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "OS event handler failed: %s", iot_err_to_name(iot_err));
        }
        break;
    default:
        IOT_LOGW(TAG, "Unknown event category: %d", event->category);
        break;
    }
}

iot_err_t devRegisterEvent(void)
{
    iot_err_t err = IOT_OK;
    err = IOT_DevCoreRegisterEventCb(IOT_EVENT_STATE, eventCallback, NULL);
    if (err != IOT_OK)
    {
        IOT_LOGE("MAIN", "Failed to register event callback IOT_EVENT_STATE");
    }
    err = IOT_DevCoreRegisterEventCb(IOT_EVENT_SMART, eventCallback, NULL);
    if (err != IOT_OK)
    {
        IOT_LOGE("MAIN", "Failed to register event callback IOT_EVENT_SMART");
    }
    err = IOT_DevCoreRegisterEventCb(IOT_EVENT_DEVICE, eventCallback, NULL);
    if (err != IOT_OK)
    {
        IOT_LOGE("MAIN", "Failed to register event callback IOT_EVENT_DEVICE");
    }
    err = IOT_DevCoreRegisterEventCb(IOT_EVENT_OS, eventCallback, NULL);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to register OS callback");
    }
    return err;
}

void root_device_factory_reset(void)
{
    IOT_LOGW(TAG, "Factory reset triggered. Restarting device...");
    iot_err_t iot_err = IOT_DaoDeviceFactoryReset();
    if (iot_err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to factory reset device DAO");
    }
    // esp_err_t esp_err = RAL_DaoGroupFactoryReset();
    // if (esp_err != ESP_OK)
    // {
    //     IOT_LOGE(TAG, "Failed to factory reset group DAO");
    // }
    iot_err = IOT_CoreFactoryReset();
    if (iot_err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to factory reset");
        iot_err = IOT_ApplicationRestartChip();
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to restart chip");
        }
    }
}


static void app_device_set_state(uint16_t elemId, uint16_t attrId, uint8_t *attrValue, uint8_t lenAttrValue)
{

    switch (attrId)
    {
    case ATTR_ONOFF:
    {
        // printf("case ATTR_ONOFF\n");
        if (lenAttrValue == 2)
        {
            uint16_t onOffValue = (attrValue[0] << 8) | attrValue[1];
            IOT_LOGI(TAG, "Setting ON/OFF state for element 0x%04x to %s", elemId, onOffValue ? "ON" : "OFF");
        }
        else
        {
            IOT_LOGE(TAG, "Invalid attribute value length for ON/OFF attribute: %d", lenAttrValue);
            return;
        }
        break;
    }
    default:
        IOT_LOGW(TAG, "Unknown attribute ID: 0x%04x for element 0x%04x", attrId, elemId);
        break;
    }
}

