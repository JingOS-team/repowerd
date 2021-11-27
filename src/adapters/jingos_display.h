/*			
* Copyright © 2021-04-10 jingos.			
* 			
* Authored by:dengbaoan <dengbaoan@jingos.com>			
*/

#pragma once

#include "src/core/display_power_control.h"
#include "src/core/display_information.h"
#include "src/core/handler_registration.h"
#include "src/core/log.h"

#include "dbus_connection_handle.h"
#include "dbus_event_loop.h"

#include <memory>
#include <atomic>

namespace repowerd
{
class Log;

class JingosDisplay : public DisplayPowerControl, public DisplayInformation
{
public:
    JingosDisplay(
        std::shared_ptr<Log> const& log,
        std::string const& dbus_bus_address);

    // From DisplayPowerControl
    void turn_on(DisplayPowerControlFilter filter) override;
    void turn_off(DisplayPowerControlFilter filter) override;

    // From DisplayInformation
    bool has_active_external_displays() override;

private:
    void handle_dbus_signal(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* signal_name,
        GVariant* parameters);
    void dbus_query_active_outputs();
    void dbus_PropertiesChanged(GVariantIter* properties_iter);
    void dbus_ActiveOutputs(int32_t internal, int32_t external);


    std::shared_ptr<Log> const log;
    DBusConnectionHandle dbus_connection;
    DBusEventLoop dbus_event_loop;
    HandlerRegistration dbus_signal_handler_registration;
    std::atomic<bool> has_active_external_displays_;
};

}
