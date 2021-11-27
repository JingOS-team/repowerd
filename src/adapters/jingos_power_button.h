/*			
* Copyright © 2021-04-10 jingos.			
* 			
* Authored by:dengbaoan <dengbaoan@jingos.com>			
*/

#pragma once

#include "src/core/power_button.h"
#include "src/core/power_button_event_sink.h"

#include "dbus_connection_handle.h"
#include "dbus_event_loop.h"

namespace repowerd
{
class Log;
class JingosPowerButton : public PowerButton, public PowerButtonEventSink
{
public:
    JingosPowerButton(std::string const& dbus_bus_address, std::shared_ptr<Log> const& log);
    void start_processing() override;

    HandlerRegistration register_power_button_handler(
        PowerButtonHandler const& handler) override;

    void notify_long_press() override;

private:
    void handle_dbus_signal(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* signal_name,
        GVariant* parameters);

    DBusConnectionHandle dbus_connection;
    DBusEventLoop dbus_event_loop;
    HandlerRegistration dbus_signal_handler_registration;

    PowerButtonHandler power_button_handler;

    //! [dba debug: 2021-04-08] 
    std::shared_ptr<Log> const log;
};

}
