/*
* Copyright © 2021-04-10 jingos.
*
* Authored by:dengbaoan <dengbaoan@jingos.com>
*/

#pragma once

#include "src/core/client_requests.h"
#include "src/core/display_power_event_sink.h"
#include "src/core/notification_service.h"

#include "brightness_params.h"
#include "dbus_connection_handle.h"
#include "dbus_event_loop.h"

#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <gio/gio.h>
#include <sys/types.h>

namespace repowerd
{
class BrightnessNotification;
class DeviceConfig;
class Log;
class TemporarySuspendInhibition;
class WakeupService;

class JingosScreenService : public ClientRequests,
                           public DisplayPowerEventSink,
                           public NotificationService
{
public:
    JingosScreenService(
        std::shared_ptr<WakeupService> const& wakeup_service,
        std::shared_ptr<BrightnessNotification> const& brightness_notification,
        std::shared_ptr<Log> const& log,
        std::shared_ptr<TemporarySuspendInhibition> const& temporary_suspend_inhibition,
        DeviceConfig const& device_config,
        std::string const& dbus_bus_address);

    void start_processing() override;

    HandlerRegistration register_disable_inactivity_timeout_handler(
        DisableInactivityTimeoutHandler const& handler) override;
    HandlerRegistration register_enable_inactivity_timeout_handler(
        EnableInactivityTimeoutHandler const& handler) override;
    HandlerRegistration register_set_inactivity_timeout_handler(
        SetInactivityTimeoutHandler const& handler) override;

    HandlerRegistration register_disable_autobrightness_handler(
        DisableAutobrightnessHandler const& handler) override;
    HandlerRegistration register_enable_autobrightness_handler(
        EnableAutobrightnessHandler const& handler) override;
    HandlerRegistration register_set_normal_brightness_value_handler(
        SetNormalBrightnessValueHandler const& handler) override;

    HandlerRegistration register_notification_handler(
        NotificationHandler const& handler) override;
    HandlerRegistration register_notification_done_handler(
        NotificationDoneHandler const& handler) override;

    HandlerRegistration register_allow_suspend_handler(
        DisallowSuspendHandler const& handler) override;
    HandlerRegistration register_disallow_suspend_handler(
        AllowSuspendHandler const& handler) override;

    HandlerRegistration register_turn_on_display_handler(
        TunOnDisplayHandler const& handler) override;
    HandlerRegistration register_turn_off_display_handler(
        TunOffDisplayHandler const& handler) override;

    void notify_display_power_on(DisplayPowerChangeReason reason) override;
    void notify_display_power_off(DisplayPowerChangeReason reason) override;

private:
    void dbus_method_call(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* method_name,
        GVariant* parameters,
        GDBusMethodInvocation* invocation);
    void dbus_signal(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* signal_name,
        GVariant* parameters);

    int32_t dbus_keepDisplayOn(std::string const& sender, pid_t pid);
    void dbus_removeDisplayOnRequest(std::string const& sender, int32_t id, pid_t pid);
    void dbus_setUserBrightness(int32_t brightness, pid_t pid);
    void dbus_setInactivityTimeouts(int32_t poweroff_timeout, int32_t dimmer_timeout, pid_t pid);
    void dbus_userAutobrightnessEnable(bool enable, pid_t pid);
    void dbus_NameOwnerChanged(
        std::string const& name,
        std::string const& old_owner,
        std::string const& new_owner);
    bool dbus_setScreenPowerMode(
        std::string const& sender,
        std::string const& mode,
        int32_t reason,
        pid_t pid);
    void dbus_emit_DisplayPowerStateChange(int32_t power_state, int32_t reason);

    std::string dbus_requestSysState(
        std::string const& sender,
        std::string const& name,
        int32_t state,
        pid_t pid);
    void dbus_clearSysState(
        std::string const& sender,
        std::string const& cookie,
        pid_t pid);
    std::string dbus_requestWakeup(
        std::string const& sender,
        std::string const& name,
        uint64_t time);
    void dbus_clearWakeup(std::string const& sender, std::string const& cookie);
    BrightnessParams dbus_getBrightnessParams();
    void dbus_emit_Wakeup(std::string const& cookie);
    void dbus_emit_brightness(double brightness);

    void dbus_unknown_method(std::string const& sender, std::string const& name);
    pid_t dbus_get_invocation_sender_pid(GDBusMethodInvocation* invocation);

    std::shared_ptr<WakeupService> const wakeup_service;
    std::shared_ptr<BrightnessNotification> const brightness_notification;
    std::shared_ptr<TemporarySuspendInhibition> const temporary_suspend_inhibition;
    std::shared_ptr<Log> const log;
    DBusConnectionHandle dbus_connection;
    DBusEventLoop dbus_event_loop;

    DisableInactivityTimeoutHandler disable_inactivity_timeout_handler;
    EnableInactivityTimeoutHandler enable_inactivity_timeout_handler;
    SetInactivityTimeoutHandler set_inactivity_timeout_handler;
    DisableAutobrightnessHandler disable_autobrightness_handler;
    EnableAutobrightnessHandler enable_autobrightness_handler;
    SetNormalBrightnessValueHandler set_normal_brightness_value_handler;
    NotificationHandler notification_handler;
    NotificationDoneHandler notification_done_handler;
    AllowSuspendHandler allow_suspend_handler;
    DisallowSuspendHandler disallow_suspend_handler;
    TunOnDisplayHandler tun_on_display_handler;
    TunOffDisplayHandler tun_off_display_handler;
    bool started;

    std::unordered_multimap<std::string,int32_t> keep_display_on_ids;
    int32_t next_keep_display_on_id;
    std::unordered_multiset<std::string> active_notifications;

    std::unordered_multimap<std::string,int32_t> request_sys_state_ids;
    int32_t next_request_sys_state_id;
    BrightnessParams brightness_params;

    // These need to be at the end, so that handlers are unregistered first on
    // destruction, to avoid accessing other members if an event arrives
    // on destruction.
    HandlerRegistration jingos_screen_handler_registration;
    HandlerRegistration name_owner_changed_handler_registration;
    HandlerRegistration powerd_handler_registration;
    HandlerRegistration wakeup_handler_registration;
    HandlerRegistration brightness_handler_registration;
};

}
