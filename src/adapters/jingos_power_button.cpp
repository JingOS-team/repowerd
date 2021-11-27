/*			
* Copyright © 2021-04-10 jingos.			
* 			
* Authored by:dengbaoan <dengbaoan@jingos.com>			
*/

#include "jingos_power_button.h"
#include "event_loop_handler_registration.h"
#include "src/core/log.h"
namespace
{
auto const null_handler = [](repowerd::PowerButtonState){};
char const* const dbus_power_button_name = "com.jingos.repowerd.PowerButton";
char const* const dbus_power_button_path = "/com/jingos/repowerd/PowerButton";
char const* const dbus_power_button_interface = "com.jingos.repowerd.PowerButton";
char const* const log_tag = "JingosPowerButton";
}

repowerd::JingosPowerButton::JingosPowerButton(
    std::string const& dbus_bus_addres,
    std::shared_ptr<Log> const& log)
    : dbus_connection{dbus_bus_addres},
      dbus_event_loop{"PowerButton"},
      power_button_handler{null_handler},
      log{log}
{
}

void repowerd::JingosPowerButton::start_processing()
{
    log->log(log_tag, "start_processing");

    dbus_signal_handler_registration = dbus_event_loop.register_signal_handler(
        dbus_connection,
        dbus_power_button_name,
        dbus_power_button_interface,
        nullptr,
        dbus_power_button_path,
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

repowerd::HandlerRegistration repowerd::JingosPowerButton::register_power_button_handler(
    PowerButtonHandler const& handler)
{
    log->log(log_tag, "register_power_button_handler");

    return EventLoopHandlerRegistration{
        dbus_event_loop,
            [this, &handler] { this->power_button_handler = handler; },
            [this] { this->power_button_handler = null_handler; }};
}

void repowerd::JingosPowerButton::notify_long_press()
{
    log->log(log_tag, "notify_long_press");

    g_dbus_connection_emit_signal(
        dbus_connection,
        nullptr,
        dbus_power_button_path,
        dbus_power_button_interface,
        "LongPress",
        nullptr,
        nullptr);
}

void repowerd::JingosPowerButton::handle_dbus_signal(
    GDBusConnection* /*connection*/,
    gchar const* sender,
    gchar const* object_path,
    gchar const* interface_name,
    gchar const* signal_name_cstr,
    GVariant* /*parameters*/)
{
    std::string const signal_name{signal_name_cstr ? signal_name_cstr : ""};
    log->log(log_tag,
             "signal_name=%s\n sender= %s\n object_path= %s\n interface_name= "
             "%s\n  signal_name_cstr= %s\n ",
             signal_name.c_str(), sender, object_path, interface_name,
             signal_name_cstr);

    if (signal_name == "Press"){
        log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s: \e[0m\n",__FILE__,__LINE__,__FUNCTION__);
        power_button_handler(repowerd::PowerButtonState::pressed);
    }  
    else if (signal_name == "Release"){
        log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s: \e[0m\n",__FILE__,__LINE__,__FUNCTION__);
        power_button_handler(repowerd::PowerButtonState::released);
    }else{
        log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s: \e[0m\n",__FILE__,__LINE__,__FUNCTION__);
    }
}
