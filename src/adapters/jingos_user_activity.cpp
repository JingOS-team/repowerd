/*			
* Copyright © 2021-04-10 jingos.			
* 			
* Authored by:dengbaoan <dengbaoan@jingos.com>			
*/

#include "jingos_user_activity.h"
#include "event_loop_handler_registration.h"

namespace
{
auto const null_handler = [](repowerd::UserActivityType){};
char const* const dbus_user_activity_name = "com.jingos.repowerd.UserActivity";
char const* const dbus_user_activity_path = "/com/jingos/repowerd/UserActivity";
char const* const dbus_user_activity_interface = "com.jingos.repowerd.UserActivity";

repowerd::UserActivityType user_activity_type_from_dbus_value(int32_t value)
{
    if (value == 0)
        return repowerd::UserActivityType::change_power_state;
    else
        return repowerd::UserActivityType::extend_power_state;
}

}

repowerd::JingosUserActivity::JingosUserActivity(
    std::string const& dbus_bus_address)
    : dbus_connection{dbus_bus_address},
      dbus_event_loop{"UserActivity"},
      user_activity_handler{null_handler}
{
}

void repowerd::JingosUserActivity::start_processing()
{
    dbus_signal_handler_registration = dbus_event_loop.register_signal_handler(
        dbus_connection,
        dbus_user_activity_name,
        dbus_user_activity_interface,
        "Activity",
        dbus_user_activity_path,
        [this] (
            GDBusConnection* connection,
            gchar const* sender,
            gchar const* object_path,
            gchar const* interface_name,
            gchar const* signal_name,
            GVariant* parameters)
        {
            handle_dbus_signal(
                connection, sender, object_path, interface_name,
                signal_name, parameters);
        });
}

repowerd::HandlerRegistration repowerd::JingosUserActivity::register_user_activity_handler(
    UserActivityHandler const& handler)
{
    return EventLoopHandlerRegistration{
        dbus_event_loop,
            [this, &handler] { this->user_activity_handler = handler; },
            [this] { this->user_activity_handler = null_handler; }};
}

void repowerd::JingosUserActivity::handle_dbus_signal(
    GDBusConnection* /*connection*/,
    gchar const* /*sender*/,
    gchar const* /*object_path*/,
    gchar const* /*interface_name*/,
    gchar const* signal_name_cstr,
    GVariant* parameters)
{
    std::string const signal_name{signal_name_cstr ? signal_name_cstr : ""};

    if (signal_name == "Activity")
    {
        int32_t activity_type;
        g_variant_get(parameters, "(i)", &activity_type);
        user_activity_handler(user_activity_type_from_dbus_value(activity_type));
    }
}
