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

#include "src/core/proximity_sensor.h"
#include "event_loop.h"
#include "dbus_connection_handle.h"
#include "dbus_event_loop.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <vector>


// #include <ubuntu/application/sensors/proximity.h>

namespace repowerd
{

class DeviceQuirks;
class Log;

class UbuntuProximitySensor : public ProximitySensor
{
public:
    UbuntuProximitySensor(
        std::string const& dbus_bus_address,
        std::shared_ptr<Log> const& log,
        DeviceQuirks const& device_quirks);
    ~UbuntuProximitySensor();

    HandlerRegistration register_proximity_handler(
        ProximityHandler const& handler) override;
    ProximityState proximity_state() override;

    void enable_proximity_events() override;
    void disable_proximity_events() override;

    void emit_proximity_event(ProximityState state);

private:
    void handle_proximity_event(ProximityState state);
    ProximityState m_state;
    EventLoop event_loop;
    ProximityHandler handler;
    bool enabled;

    //! [dba debug: 2021-05-26] sensor
    DBusConnectionHandle dbus_connection;
    void sensor_start();
    void sensor_stop();
    void sensor_thread();
    int sensor_get_event(void);
    int32_t  m_sessionId;
    std::thread sensor_event_thread;
    std::shared_ptr<Log> const log;
};

}
