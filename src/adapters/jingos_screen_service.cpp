/*
* Copyright © 2021-04-10 jingos.
*
* Authored by:dengbaoan <dengbaoan@jingos.com>
*/

#include "jingos_screen_service.h"
#include "jingos_screen_power_state_change_reason.h"
#include "brightness_notification.h"
#include "event_loop_handler_registration.h"
#include "scoped_g_error.h"
#include "temporary_suspend_inhibition.h"
#include "wakeup_service.h"

#include "src/core/infinite_timeout.h"
#include "src/core/log.h"

#include <cmath>

namespace
{

char const* const log_tag = "JingosScreenService";

auto const null_arg_handler = [](auto){};
auto const null_arg2_handler = [](auto,auto){};

int32_t reason_to_dbus_param(repowerd::DisplayPowerChangeReason reason)
{
    repowerd::JingosScreenPowerStateChangeReason jingos_screen_reason{
        repowerd::JingosScreenPowerStateChangeReason::unknown};

    switch (reason)
    {
    case repowerd::DisplayPowerChangeReason::power_button:
        jingos_screen_reason = repowerd::JingosScreenPowerStateChangeReason::power_key;
        break;

    case repowerd::DisplayPowerChangeReason::activity:
        jingos_screen_reason = repowerd::JingosScreenPowerStateChangeReason::inactivity;
        break;

    case repowerd::DisplayPowerChangeReason::proximity:
        jingos_screen_reason = repowerd::JingosScreenPowerStateChangeReason::proximity;
        break;

    case repowerd::DisplayPowerChangeReason::notification:
        jingos_screen_reason = repowerd::JingosScreenPowerStateChangeReason::notification;
        break;

    case repowerd::DisplayPowerChangeReason::call:
        jingos_screen_reason = repowerd::JingosScreenPowerStateChangeReason::unknown;
        break;

    case repowerd::DisplayPowerChangeReason::call_done:
        jingos_screen_reason = repowerd::JingosScreenPowerStateChangeReason::call_done;
        break;

    default:
        break;
    };

    return static_cast<int32_t>(jingos_screen_reason);
}

char const* const dbus_screen_interface = "com.jingos.repowerd.Screen";
char const* const dbus_screen_path = "/com/jingos/repowerd/Screen";
char const* const dbus_screen_service_name = "com.jingos.repowerd.Screen";

char const* const jingos_screen_service_introspection = R"(<!DOCTYPE node PUBLIC '-//freedesktop//DTD D-BUS Object Introspection 1.0//EN' 'http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd'>
<node>
  <interface name='com.jingos.repowerd.Screen'>
    <method name='setScreenPowerMode'>
      <arg type='b' direction='out'/>
      <arg name='mode' type='s' direction='in'/>
      <arg name='reason' type='i' direction='in'/>
    </method>
    <method name='keepDisplayOn'>
      <arg name='sender' type='s' direction='in'/>
      <arg name='pid' type='u' direction='in'/>
      <arg type='i' direction='out'/>
    </method>
    <method name='keepDisplayOff'>
      <arg name='sender' type='s' direction='in'/>
      <arg name='pid' type='u' direction='in'/>
      <arg name='id' type='i' direction='in'/>
    </method>
    <method name='keepDisplayNameOwnerChanged'>
      <arg name='name' type='s' direction='in'/>
      <arg name='old_owner' type='s' direction='in'/>
      <arg name='new_owner' type='s' direction='in'/>
    </method>
    <method name='displayOnRequest'>
      <arg type='i' direction='out'/>
    </method>
    <method name='removeDisplayOnRequest'>
      <arg name='id' type='i' direction='in'/>
    </method>
    <method name='setUserBrightness'>
      <arg name='brightness' type='i' direction='in'/>
    </method>
    <method name='userAutobrightnessEnable'>
      <arg name='enable' type='b' direction='in'/>
    </method>
    <method name='setInactivityTimeouts'>
      <arg name='poweroff_timeout' type='i' direction='in'/>
      <arg name='dimmer_timeout' type='i' direction='in'/>
    </method>
    <method name='setTouchVisualizationEnabled'>
      <arg name='enabled' type='b' direction='in'/>
    </method>
    <signal name='DisplayPowerStateChange'>
      <arg name='state' type='i'/>
      <arg name='reason' type='i'/>
    </signal>
  </interface>
</node>)";

char const* const dbus_powerd_interface = "com.jingos.powerd";
char const* const dbus_powerd_path = "/com/jingos/powerd";
char const* const dbus_powerd_service_name = "com.jingos.powerd";

char const* const jingos_powerd_service_introspection = R"(
<node>
  <interface name='com.jingos.powerd'>
    <method name='requestSysState'>
      <arg type='s' name='name' direction='in' />
      <arg type='i' name='state' direction='in' />
      <arg type='s' name='cookie' direction='out' />
    </method>
    <method name='clearSysState'>
      <arg type='s' name='cookie' direction='in' />
    </method>
    <method name='requestWakeup'>
      <arg type='s' name='name' direction='in' />
      <arg type='t' name='time' direction='in' />
      <arg type='s' name='cookie' direction='out' />
    </method>
    <method name='clearWakeup'>
      <arg type='s' name='cookie' direction='in' />
    </method>
    <method name='getBrightnessParams'>
      <!-- Returns dim, min, max, and default brighness and whether or not
           autobrightness is supported, in that order -->
      <arg type='(iiiib)' name='params' direction="out" />
    </method>
    <signal name='Wakeup'>
        <arg name='cookie' type='s'/>
    </signal>
  </interface>
</node>)";

std::string notification_id(std::string const& sender, size_t index)
{
    return sender + "-" + std::to_string(index);
}

}

repowerd::JingosScreenService::JingosScreenService(
    std::shared_ptr<WakeupService> const& wakeup_service,
    std::shared_ptr<BrightnessNotification> const& brightness_notification,
    std::shared_ptr<Log> const& log,
    std::shared_ptr<TemporarySuspendInhibition> const& temporary_suspend_inhibition,
    DeviceConfig const& device_config,
    std::string const& dbus_bus_address)
    : wakeup_service{wakeup_service},
      brightness_notification{brightness_notification},
      temporary_suspend_inhibition{temporary_suspend_inhibition},
      log{log},
      dbus_connection{dbus_bus_address},
      dbus_event_loop{"DBusService"},
      disable_inactivity_timeout_handler{null_arg2_handler},
      enable_inactivity_timeout_handler{null_arg2_handler},
      set_inactivity_timeout_handler{null_arg2_handler},
      disable_autobrightness_handler{null_arg_handler},
      enable_autobrightness_handler{null_arg_handler},
      set_normal_brightness_value_handler{null_arg2_handler},
      notification_handler{null_arg2_handler},
      notification_done_handler{null_arg2_handler},
      allow_suspend_handler{null_arg2_handler},
      disallow_suspend_handler{null_arg2_handler},
      started{false},
      next_keep_display_on_id{1},
      next_request_sys_state_id{1},
      brightness_params(BrightnessParams::from_device_config(device_config))
{
}

void repowerd::JingosScreenService::start_processing()
{
    if (started) return;

    jingos_screen_handler_registration = dbus_event_loop.register_object_handler(
        dbus_connection,
        dbus_screen_path,
        jingos_screen_service_introspection,
        [this] (
            GDBusConnection* connection,
            gchar const* sender,
            gchar const* object_path,
            gchar const* interface_name,
            gchar const* method_name,
            GVariant* parameters,
            GDBusMethodInvocation* invocation)
        {
            dbus_method_call(
                connection, sender, object_path, interface_name,
                method_name, parameters, invocation);
        });

    name_owner_changed_handler_registration = dbus_event_loop.register_signal_handler(
        dbus_connection,
        "org.freedesktop.DBus",
        "org.freedesktop.DBus",
        "NameOwnerChanged",
        "/org/freedesktop/DBus",
        [this] (
            GDBusConnection* connection,
            gchar const* sender,
            gchar const* object_path,
            gchar const* interface_name,
            gchar const* signal_name,
            GVariant* parameters)
        {
            dbus_signal(
                connection, sender, object_path, interface_name,
                signal_name, parameters);
        });

    powerd_handler_registration = dbus_event_loop.register_object_handler(
        dbus_connection,
        dbus_powerd_path,
        jingos_powerd_service_introspection,
        [this] (
            GDBusConnection* connection,
            gchar const* sender,
            gchar const* object_path,
            gchar const* interface_name,
            gchar const* method_name,
            GVariant* parameters,
            GDBusMethodInvocation* invocation)
        {
            dbus_method_call(
                connection, sender, object_path, interface_name,
                method_name, parameters, invocation);
        });

    wakeup_handler_registration = wakeup_service->register_wakeup_handler(
        [this] (std::string const& cookie)
        {
            temporary_suspend_inhibition->inhibit_suspend_for(
                std::chrono::seconds{3}, "Wakeup_" + cookie);

            dbus_event_loop.enqueue([this, cookie] { dbus_emit_Wakeup(cookie); });
        });

    brightness_handler_registration = brightness_notification->register_brightness_handler(
        [this] (double brightness)
        {
            dbus_event_loop.enqueue([this,brightness] { dbus_emit_brightness(brightness); });
        });

    dbus_connection.request_name(dbus_screen_service_name);
    dbus_connection.request_name(dbus_powerd_service_name);

    started = true;
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_enable_inactivity_timeout_handler(
    EnableInactivityTimeoutHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { enable_inactivity_timeout_handler = handler; },
        [this] { enable_inactivity_timeout_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_disable_inactivity_timeout_handler(
    DisableInactivityTimeoutHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { disable_inactivity_timeout_handler = handler; },
        [this] { disable_inactivity_timeout_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_set_inactivity_timeout_handler(
    SetInactivityTimeoutHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { set_inactivity_timeout_handler = handler; },
        [this] { set_inactivity_timeout_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_disable_autobrightness_handler(
    DisableAutobrightnessHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { disable_autobrightness_handler = handler; },
        [this] { disable_autobrightness_handler = null_arg_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_enable_autobrightness_handler(
    EnableAutobrightnessHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { enable_autobrightness_handler = handler; },
        [this] { enable_autobrightness_handler = null_arg_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_set_normal_brightness_value_handler(
    SetNormalBrightnessValueHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { set_normal_brightness_value_handler = handler; },
        [this] { set_normal_brightness_value_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_notification_handler(
    NotificationHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { notification_handler = handler; },
        [this] { notification_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_notification_done_handler(
    NotificationDoneHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { notification_done_handler = handler; },
        [this] { notification_done_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_allow_suspend_handler(
    AllowSuspendHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { allow_suspend_handler = handler; },
        [this] { allow_suspend_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration
repowerd::JingosScreenService::register_disallow_suspend_handler(
    DisallowSuspendHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { disallow_suspend_handler = handler; },
        [this] { disallow_suspend_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration repowerd::JingosScreenService::register_turn_on_display_handler(
    TunOnDisplayHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { tun_on_display_handler = handler; },
        [this] { tun_on_display_handler = null_arg2_handler; }};
}

repowerd::HandlerRegistration repowerd::JingosScreenService::register_turn_off_display_handler(
    TunOffDisplayHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
        [this, &handler] { tun_off_display_handler = handler; },
        [this] { tun_off_display_handler = null_arg2_handler; }};
}

void repowerd::JingosScreenService::notify_display_power_on(
    DisplayPowerChangeReason reason)
{
    int32_t const power_state_on = 1;
    int32_t const reason_param = reason_to_dbus_param(reason);

    dbus_emit_DisplayPowerStateChange(power_state_on, reason_param);
}

void repowerd::JingosScreenService::notify_display_power_off(
    DisplayPowerChangeReason reason)
{
    int32_t const power_state_off = 0;
    int32_t const reason_param = reason_to_dbus_param(reason);

    dbus_emit_DisplayPowerStateChange(power_state_off, reason_param);
}

void repowerd::JingosScreenService::dbus_method_call(
    GDBusConnection* /*connection*/,
    gchar const* sender_cstr,
    gchar const* /*object_path_cstr*/,
    gchar const* /*interface_name_cstr*/,
    gchar const* method_name_cstr,
    GVariant* parameters,
    GDBusMethodInvocation* invocation)
{
    std::string const sender{sender_cstr ? sender_cstr : ""};
    std::string const method_name{method_name_cstr ? method_name_cstr : ""};
    auto const pid = dbus_get_invocation_sender_pid(invocation);

    log->log(log_tag, "\e[0;36m[dba_DEBUG] %s:%d %s:  sender_cstr:%s  sender:%s method_name_cstr:%s method_name:%s \e[0m\n",__FILE__,__LINE__,__FUNCTION__ , sender_cstr,sender.c_str(), method_name_cstr,method_name.c_str());

    if (method_name == "keepDisplayOn")
    {
        uint32_t pid{0};
        char const* sender{""};
        g_variant_get(parameters, "(su)", &sender, &pid);

        auto const id = dbus_keepDisplayOn(sender, pid);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(i)", id));
        log->log(log_tag, "\e[0;36m[dba_DEBUG] %s:%d %s:sender:%s pid: %d \e[0m\n",__FILE__,__LINE__,__FUNCTION__ , sender, pid);
    }
    else if (method_name == "keepDisplayOff")
    {
        uint32_t pid{0};
        char const* sender{""};
        int32_t id{-1};
        g_variant_get(parameters, "(sui)", &sender, &pid, &id);

        dbus_removeDisplayOnRequest(sender, id, pid);
        g_dbus_method_invocation_return_value(invocation, NULL);
        log->log(log_tag, "\e[0;36m[dba_DEBUG] %s:%d %s:sender:%s pid: %d \e[0m\n",__FILE__,__LINE__,__FUNCTION__ , sender, pid);
    }
    else if (method_name == "displayOnRequest")
    {
        auto const id = dbus_keepDisplayOn(sender, pid);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(i)", id));
        log->log(log_tag, "\e[0;36m[dba_DEBUG] %s:%d %s:sender:%s pid: %d \e[0m\n",__FILE__,__LINE__,__FUNCTION__ , sender.c_str(), pid);
    }
    else if (method_name == "keepDisplayNameOwnerChanged")
    {
        char const* name = "";
        char const* old_owner = "";
        char const* new_owner = "";
        g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

        dbus_NameOwnerChanged(name, old_owner, new_owner);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (method_name == "removeDisplayOnRequest")
    {
        int32_t id{-1};
        g_variant_get(parameters, "(i)", &id);

        dbus_removeDisplayOnRequest(sender, id, pid);

        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (method_name == "setUserBrightness")
    {
        int32_t brightness{0};
        g_variant_get(parameters, "(i)", &brightness);

        dbus_setUserBrightness(brightness, pid);

        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (method_name == "setInactivityTimeouts")
    {
        int32_t poweroff_timeout{-1};
        int32_t dimmer_timeout{-1};
        g_variant_get(parameters, "(ii)", &poweroff_timeout, &dimmer_timeout);

        dbus_setInactivityTimeouts(poweroff_timeout, dimmer_timeout, pid);

        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (method_name == "userAutobrightnessEnable")
    {
        gboolean enable{FALSE};
        g_variant_get(parameters, "(b)", &enable);

        dbus_userAutobrightnessEnable(enable == TRUE, pid);

        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (method_name == "setScreenPowerMode")
    {
        char const* mode{""};
        int32_t reason{-1};
        g_variant_get(parameters, "(&si)", &mode, &reason);

        auto const result = dbus_setScreenPowerMode(sender, mode, reason, pid);

        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(b)", result ? TRUE : FALSE));
    }
    else if (method_name == "requestSysState")
    {
        char const* name{""};
        int32_t state{-1};
        g_variant_get(parameters, "(&si)", &name, &state);

        try
        {
            auto const cookie = dbus_requestSysState(sender, name, state, pid);
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(s)", cookie.c_str()));
        }
        catch (std::exception const& e)
        {
            g_dbus_method_invocation_return_error_literal(
                invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, e.what());
        }
    }
    else if (method_name == "clearSysState")
    {
        char const* cookie{""};
        g_variant_get(parameters, "(&s)", &cookie);

        dbus_clearSysState(sender, cookie, pid);

        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (method_name == "requestWakeup")
    {
        char const* name{""};
        uint64_t time{0};
        g_variant_get(parameters, "(&st)", &name, &time);

        auto const cookie = dbus_requestWakeup(sender, name, time);

        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(s)", cookie.c_str()));
    }
    else if (method_name == "clearWakeup")
    {
        char const* cookie{""};
        g_variant_get(parameters, "(&s)", &cookie);

        dbus_clearWakeup(sender, cookie);

        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    else if (method_name == "getBrightnessParams")
    {
        auto params = dbus_getBrightnessParams();

        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("((iiiib))",
                params.dim_value,
                params.min_value,
                params.max_value,
                params.default_value,
                params.autobrightness_supported));
    }
    else
    {
        dbus_unknown_method(sender, method_name);

        g_dbus_method_invocation_return_error_literal(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "");
    }
}

void repowerd::JingosScreenService::dbus_signal(
    GDBusConnection* /*connection*/,
    gchar const* sender_cstr,
    gchar const* object_path_cstr,
    gchar const* interface_name_cstr,
    gchar const* signal_name_cstr,
    GVariant* parameters)
{
    std::string const sender{sender_cstr ? sender_cstr : ""};
    std::string const object_path{object_path_cstr ? object_path_cstr : ""};
    std::string const interface_name{interface_name_cstr ? interface_name_cstr : ""};
    std::string const signal_name{signal_name_cstr ? signal_name_cstr : ""};

    if (sender == "org.freedesktop.DBus" &&
        object_path == "/org/freedesktop/DBus" &&
        interface_name == "org.freedesktop.DBus" &&
        signal_name == "NameOwnerChanged")
    {
        char const* name = "";
        char const* old_owner = "";
        char const* new_owner = "";
        g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

        dbus_NameOwnerChanged(name, old_owner, new_owner);
    }
}

int32_t repowerd::JingosScreenService::dbus_keepDisplayOn(
    std::string const& sender, pid_t pid)
{
    log->log(log_tag, "dbus_keepDisplayOn(%s)", sender.c_str());

    auto const id = next_keep_display_on_id++;
    keep_display_on_ids.emplace(sender, id);

    for (auto iter = keep_display_on_ids.begin(); iter != keep_display_on_ids.end(); ++iter){
        log->log(log_tag, "\e[0;36m[dba_DEBUG] %s:%d %s:  ==>  %s \e[0m\n",__FILE__,__LINE__,__FUNCTION__ , iter->first.c_str());
    }
    disable_inactivity_timeout_handler(std::to_string(id), pid);

    log->log(log_tag, "dbus_keepDisplayOn(%s) => %d", sender.c_str(), id);

    return id;
}

void repowerd::JingosScreenService::dbus_removeDisplayOnRequest(
    std::string const& sender, int32_t id, pid_t pid)
{
    log->log(log_tag, "dbus_removeDisplayOnRequest(%s,%d)", sender.c_str(), id);

    bool id_removed{false};

    auto range = keep_display_on_ids.equal_range(sender);
    for (auto iter = range.first;
         iter != range.second;
         ++iter)
    {
        if (iter->second == id)
        {
            keep_display_on_ids.erase(iter);
            id_removed = true;
            break;
        }
    }

    if (id_removed)
        enable_inactivity_timeout_handler(std::to_string(id), pid);
}

void repowerd::JingosScreenService::dbus_NameOwnerChanged(
    std::string const& name,
    std::string const& old_owner,
    std::string const& new_owner)
{
    log->log(log_tag, "dbus_NameOwnerChanged 0  ===>(%s,%s,%s)", name.c_str(), old_owner.c_str(), new_owner.c_str());

    if (keep_display_on_ids.find(name) != keep_display_on_ids.end() ||
        request_sys_state_ids.find(name) != request_sys_state_ids.end() ||
        active_notifications.find(name) != active_notifications.end())
    {
        log->log(log_tag, "dbus_NameOwnerChanged 1 ===> (%s,%s,%s)", name.c_str(), old_owner.c_str(), new_owner.c_str());
    }

    if (new_owner.empty() && old_owner == name)
    {
         for (auto iter = keep_display_on_ids.begin(); iter != keep_display_on_ids.end(); ++iter){
             log->log(log_tag, "\e[0;36m[dba_DEBUG] %s:%d %s:  %s: \e[0m\n",__FILE__,__LINE__,__FUNCTION__ , iter->first.c_str());
         }

        auto const kdo_range = keep_display_on_ids.equal_range(name);
        for (auto iter = kdo_range.first; iter != kdo_range.second; ++iter){
            log->log(log_tag, "kdo_range item %s", std::to_string(iter->second).c_str());
            enable_inactivity_timeout_handler(std::to_string(iter->second), 0);
        }
        keep_display_on_ids.erase(name);

        auto rss_range = request_sys_state_ids.equal_range(name);
        for (auto iter = rss_range.first; iter != rss_range.second; ++iter)
            allow_suspend_handler(std::to_string(iter->second), 0);
        request_sys_state_ids.erase(name);

        auto const num_notifications_removed = active_notifications.erase(name);
        for (auto i = 0u; i < num_notifications_removed; ++i)
            notification_done_handler(notification_id(name, i), 0);
    }
}

void repowerd::JingosScreenService::dbus_userAutobrightnessEnable(
    bool enable, pid_t pid)
{
    log->log(log_tag, "dbus_userAutobrightnessEnable(%s)",
             enable ? "enable" : "disable");

    if (enable)
        enable_autobrightness_handler(pid);
    else
        disable_autobrightness_handler(pid);

    brightness_params.autobrightness_supported = enable;
    g_dbus_connection_emit_signal( dbus_connection,
                            nullptr,
                            dbus_screen_path,
                            dbus_screen_interface,
                            "AutobrightnessChange",
                            g_variant_new("(b)", enable),
                            nullptr);
}

void repowerd::JingosScreenService::dbus_setUserBrightness(
    int32_t brightness, pid_t pid)
{
    log->log(log_tag, "dbus_setUserBrightness(%d)", brightness);

    set_normal_brightness_value_handler(
        brightness/static_cast<double>(brightness_params.max_value),
        pid);

    brightness_params.default_value = brightness;
    g_dbus_connection_emit_signal( dbus_connection,
                                nullptr,
                                dbus_screen_path,
                                dbus_screen_interface,
                                "UserBrightnessChange",
                                g_variant_new("(i)", brightness),
                                nullptr);
}

void repowerd::JingosScreenService::dbus_setInactivityTimeouts(
    int32_t poweroff_timeout, int32_t dimmer_timeout, pid_t pid)
{
    log->log(log_tag, "dbus_setInactivityTimeouts(%d,%d)",
             poweroff_timeout, dimmer_timeout);

    if (poweroff_timeout < 0){
        poweroff_timeout = 0;
    }

    auto const timeout = poweroff_timeout == 0 ? repowerd::infinite_timeout :
                                                 std::chrono::seconds{poweroff_timeout};
    set_inactivity_timeout_handler(timeout, pid);

    g_dbus_connection_emit_signal( dbus_connection,
                                    nullptr,
                                    dbus_screen_path,
                                    dbus_screen_interface,
                                    "InactivityTimeoutsChange",
                                    g_variant_new("(ii)", poweroff_timeout, dimmer_timeout),
                                    nullptr);
}

bool repowerd::JingosScreenService::dbus_setScreenPowerMode(
    std::string const& sender, std::string const& mode, int32_t reason, pid_t pid)
{
    log->log(log_tag, "dbus_setScreenPowerMode(%s,%s,%d)", sender.c_str(), mode.c_str(), reason);

    if (reason == static_cast<int32_t>(JingosScreenPowerStateChangeReason::notification) ||
        reason == static_cast<int32_t>(JingosScreenPowerStateChangeReason::snap_decision))
    {
        if (mode == "on")
        {
            auto const id = notification_id(sender, active_notifications.count(sender));
            active_notifications.emplace(sender);
            notification_handler(id, pid);
        }
        else if (mode == "off")
        {
            auto const iter = active_notifications.find(sender);
            if (iter != active_notifications.end())
            {
                active_notifications.erase(iter);
                auto const id = notification_id(sender, active_notifications.count(sender));
                notification_done_handler(id, pid);
            }
        }
        return true;
    }else if(reason == static_cast<int32_t>(JingosScreenPowerStateChangeReason::inactivity) ||
             reason == static_cast<int32_t>(JingosScreenPowerStateChangeReason::power_key)){

        auto const id = notification_id(sender, active_notifications.count(sender));
        if (mode == "on"){
            tun_on_display_handler(id, pid);
        }else if (mode == "off"){
            tun_off_display_handler(id, pid);
        }
        return true;
    }
    return false;
}

void repowerd::JingosScreenService::dbus_emit_DisplayPowerStateChange(
    int32_t power_state, int32_t reason)
{
    log->log(log_tag, "dbus_emit_DisplayPowerStateChange(%d,%d)",
             power_state, reason);

    g_dbus_connection_emit_signal(
        dbus_connection,
        nullptr,
        dbus_screen_path,
        dbus_screen_interface,
        "DisplayPowerStateChange",
        g_variant_new("(ii)", power_state, reason),
        nullptr);
}

std::string repowerd::JingosScreenService::dbus_requestSysState(
    std::string const& sender,
    std::string const& name,
    int32_t state,
    pid_t pid)
{
    log->log(log_tag, "dbus_requestSysState(%s,%s,%d)",
             sender.c_str(), name.c_str(), state);

    int32_t const active_state{1};

    if (state != active_state)
        throw std::runtime_error{"Invalid state"};

    auto const id = next_request_sys_state_id++;
    request_sys_state_ids.emplace(sender, id);

    disallow_suspend_handler(std::to_string(id), pid);

    log->log(log_tag, "dbus_requestSysState(%s,%s,%d) => %d",
             sender.c_str(), name.c_str(), state, id);

    return std::to_string(id);
}

void repowerd::JingosScreenService::dbus_clearSysState(
    std::string const& sender,
    std::string const& cookie,
    pid_t pid)
{
    log->log(log_tag, "dbus_clearSysState(%s,%s)",
             sender.c_str(), cookie.c_str());

    bool id_removed{false};

    int32_t id = 0;
    try { id = std::stoi(cookie); } catch(...) {}

    auto range = request_sys_state_ids.equal_range(sender);
    for (auto iter = range.first;
         iter != range.second;
         ++iter)
    {
        if (iter->second == id)
        {
            request_sys_state_ids.erase(iter);
            id_removed = true;
            break;
        }
    }

    if (id_removed)
        allow_suspend_handler(std::to_string(id), pid);
}

std::string repowerd::JingosScreenService::dbus_requestWakeup(
    std::string const& sender,
    std::string const& name,
    uint64_t time)
{
    log->log(log_tag, "dbus_requestWakeup(%s,%s,%ju)",
             sender.c_str(), name.c_str(), static_cast<uintmax_t>(time));

    auto const cookie =
        wakeup_service->schedule_wakeup_at(std::chrono::system_clock::from_time_t(time));

    log->log(log_tag, "dbus_requestWakeup(%s,%s,%ju) => %s",
             sender.c_str(), name.c_str(), static_cast<uintmax_t>(time), cookie.c_str());

    return cookie;
}

void repowerd::JingosScreenService::dbus_clearWakeup(
    std::string const& sender, std::string const& cookie)
{
    log->log(log_tag, "dbus_clearWakeup(%s,%s)", sender.c_str(), cookie.c_str());

    wakeup_service->cancel_wakeup(cookie);
}

repowerd::BrightnessParams repowerd::JingosScreenService::dbus_getBrightnessParams()
{
    log->log(log_tag, "dbus_getBrightnessParams() => (%d,%d,%d,%d,%s)",
             brightness_params.dim_value,
             brightness_params.min_value,
             brightness_params.max_value,
             brightness_params.default_value,
             brightness_params.autobrightness_supported ? "true" : "false");

    return brightness_params;
}

void repowerd::JingosScreenService::dbus_emit_Wakeup(std::string const& cookie)
{
    log->log(log_tag, "dbus_emit_Wakeup(%s)", cookie.c_str());
    GVariant* params = g_variant_new_parsed("(@s %s,)", cookie.c_str());

    g_dbus_connection_emit_signal(
        dbus_connection,
        nullptr,
        dbus_powerd_path,
        dbus_powerd_interface,
        "Wakeup",
        params,
        nullptr);
}

void repowerd::JingosScreenService::dbus_emit_brightness(double brightness)
{
    int32_t const brightness_abs = round(brightness * brightness_params.max_value);

    log->log(log_tag, "dbus_emit_brightness(%f), brightness_value=%d",
             brightness, brightness_abs);

    g_dbus_connection_emit_signal(
        dbus_connection,
        nullptr,
        dbus_powerd_path,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new_parsed(
            "(@s %s, @a{sv} {'brightness': <%i>}, @as [])",
            dbus_powerd_interface, brightness_abs),
        nullptr);
}

void repowerd::JingosScreenService::dbus_unknown_method(
    std::string const& sender, std::string const& name)
{
    log->log(log_tag, "dbus_unknown_method(%s,%s)", sender.c_str(), name.c_str());
}

pid_t repowerd::JingosScreenService::dbus_get_invocation_sender_pid(
    GDBusMethodInvocation* invocation)
{
    int constexpr timeout = 1000;
    auto constexpr null_cancellable = nullptr;
    ScopedGError error;
    auto const sender = g_dbus_method_invocation_get_sender(invocation);

    auto const result = g_dbus_connection_call_sync(
        dbus_connection,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "GetConnectionUnixProcessID",
        g_variant_new("(s)", sender),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        error);

    if (!result)
    {
        log->log(log_tag, "failed to get pid of '%s': %s",
                 sender, error.message_str().c_str());
        return -1;
    }

    guint pid;
    g_variant_get(result, "(u)", &pid);
    g_variant_unref(result);

    return pid;
}
