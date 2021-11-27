/*			
* Copyright © 2021-04-10 jingos.			
* 			
* Authored by:dengbaoan <dengbaoan@jingos.com>			
*/

#pragma once

#include "src/core/user_activity.h"

#include "dbus_connection_handle.h"
#include "dbus_event_loop.h"

namespace repowerd
{

class JingosUserActivity : public UserActivity
{
public:
    JingosUserActivity(std::string const& dbus_bus_address);

    void start_processing() override;
    HandlerRegistration register_user_activity_handler(
        UserActivityHandler const& handler) override;

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

    UserActivityHandler user_activity_handler;
};

}
