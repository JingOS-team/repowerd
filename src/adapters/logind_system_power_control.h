/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#pragma once

#include "src/core/system_power_control.h"

#include "dbus_connection_handle.h"
#include "dbus_event_loop.h"
#include "fd.h"

#include <memory>
#include <mutex>
#include <unordered_set>

namespace repowerd
{
class Log;

class LogindSystemPowerControl : public SystemPowerControl
{
public:
    LogindSystemPowerControl(
        std::shared_ptr<Log> const& log,
        std::string const& dbus_bus_address);

    void start_processing() override;
    HandlerRegistration register_system_resume_handler(
        SystemResumeHandler const& system_resume_handler) override;
    HandlerRegistration register_system_allow_suspend_handler(
        SystemAllowSuspendHandler const& system_allow_suspend_handler) override;
    HandlerRegistration register_system_disallow_suspend_handler(
        SystemDisallowSuspendHandler const& system_disallow_suspend_handler) override;

    void allow_automatic_suspend(std::string const& id) override;
    void disallow_automatic_suspend(std::string const& id) override;

    void power_off() override;
    void suspend() override;

    void allow_default_system_handlers() override;
    void disallow_default_system_handlers() override;

private:
    void handle_dbus_signal(
        GDBusConnection* connection,
        gchar const* sender,
        gchar const* object_path,
        gchar const* interface_name,
        gchar const* signal_name,
        GVariant* parameters);
    void handle_dbus_change_manager_properties(GVariantIter* properties_iter);
    Fd dbus_inhibit(char const* what, char const* who);
    void dbus_power_off();
    void dbus_suspend();
    void initialize_is_suspend_blocked();
    std::string dbus_get_block_inhibited();
    void update_suspend_block(std::string const& blocks);
    void notify_suspend_block_state();

    std::shared_ptr<Log> const log;
    DBusConnectionHandle dbus_connection;
    DBusEventLoop dbus_event_loop;

    HandlerRegistration dbus_manager_signal_handler_registration;
    HandlerRegistration dbus_manager_properties_handler_registration;
    SystemResumeHandler system_resume_handler;
    SystemAllowSuspendHandler system_allow_suspend_handler;
    SystemDisallowSuspendHandler system_disallow_suspend_handler;

    bool is_suspend_blocked;
    std::mutex inhibitions_mutex;
    std::mutex suspend_mutex;
    Fd idle_and_lid_inhibition_fd;
};

}
