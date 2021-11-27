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

#include "ubuntu_proximity_sensor.h"
#include "device_quirks.h"
#include "event_loop_handler_registration.h"

#include "src/core/log.h"

#include <stdexcept>
#include <algorithm>
#include <thread>
#include <chrono>
#include <random>
namespace
{

char const* const log_tag = "UbuntuProximitySensor";
auto const null_handler = [](repowerd::ProximityState){};
}

repowerd::UbuntuProximitySensor::UbuntuProximitySensor(
    std::string const& dbus_bus_address,
    std::shared_ptr<Log> const& log,
    DeviceQuirks const& device_quirks)
    :dbus_connection{dbus_bus_address},
      event_loop{"Light"},
      handler{null_handler},
      enabled{false},
      m_sessionId{0},
      m_state{ProximityState::far},
      log{log}
{
    // log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s \e[0m\n", __FILE__, __LINE__, __FUNCTION__);
}
repowerd::UbuntuProximitySensor::~UbuntuProximitySensor(){
    // log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s \e[0m\n", __FILE__, __LINE__, __FUNCTION__);
}
repowerd::HandlerRegistration repowerd::UbuntuProximitySensor::register_proximity_handler(
    ProximityHandler const& handler)
{
    return EventLoopHandlerRegistration{
        event_loop,
        [this, &handler]{ this->handler = handler; },
        [this]{ this->handler = null_handler; }};
}

repowerd::ProximityState repowerd::UbuntuProximitySensor::proximity_state()
{
    return  ProximityState::far;

    int count = 3; 
    int value = -1;
    do {
        value = sensor_get_event();
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }while( (count--) && (value < 0) );

    log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s:  %d \e[0m\n", __FILE__, __LINE__, __FUNCTION__, value);
    return ( value != 0 ? ProximityState::far : ProximityState::near );
}

void repowerd::UbuntuProximitySensor::enable_proximity_events()
{
    log->log(log_tag, "enable_proximity_events()");

    event_loop.enqueue(
        [this]
        {
            log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s\e[0m\n", __FILE__, __LINE__, __FUNCTION__);
            if (!enabled)
            {
                log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s\e[0m\n", __FILE__, __LINE__, __FUNCTION__);
                enabled = true;
                //sensor_start();
                //sensor_event_thread = std::thread(std::mem_fn(&repowerd::UbuntuProximitySensor::sensor_thread), this);
            }
        }).get();
}

void repowerd::UbuntuProximitySensor::disable_proximity_events()
{
    log->log(log_tag, "disable_proximity_events()");

    event_loop.enqueue(
        [this]
        {
            if (enabled)
            {
                enabled = false;
                if (sensor_event_thread.joinable()){
                    sensor_event_thread.join();
                }
                //sensor_stop();
            }
        }).get();
}
void repowerd::UbuntuProximitySensor::emit_proximity_event(
    repowerd::ProximityState state)
{
    event_loop.enqueue([this, state] { handle_proximity_event(state); }).get();
}
void repowerd::UbuntuProximitySensor::handle_proximity_event(repowerd::ProximityState state)
{
    log->log(log_tag, "enable_proximity_events()");
    m_state = state;
    handler(m_state);
}

void repowerd::UbuntuProximitySensor::sensor_start()
{
    int constexpr timeout = 3000;
    auto constexpr null_cancellable = nullptr;
    GError *error = NULL;

    auto const result = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager",
        "local.SensorManager",
        "loadPlugin",
        g_variant_new("(s)", "proximitysensor"),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    if (!result){
        log->log(log_tag, "Failed loadPlugin: %s %d\n", error->message );
    }        

    auto const result2 = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager",
        "local.SensorManager",
        "requestSensor",
        g_variant_new("(sx)", "proximitysensor", getpid()),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    if (!result2){
        log->log(log_tag, "Failed requestSensor: %s\n", error->message );
    }

    g_variant_get(result2, "(i)", &m_sessionId);
    log->log(log_tag, "m_sessionId= : %d\n", m_sessionId);

   auto const result3 = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager/proximitysensor",
        "local.ProximitySensor",
        "start",
        g_variant_new("(i)", m_sessionId),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    if (!result3){
        log->log(log_tag, "Failed ProximitySensor start: %s\n", error->message );
    }        
}

void repowerd::UbuntuProximitySensor::sensor_stop()
{
    int constexpr timeout = 1000;
    auto constexpr null_cancellable = nullptr;
    GError *error = NULL;

    auto const result = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager/proximitysensor",
        "local.ProximitySensor",
        "stop",
        g_variant_new("(i)",m_sessionId),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    if (!result){
        log->log(log_tag, "Failed ProximitySensor stop: %s\n", error->message );
    }          
}

int repowerd::UbuntuProximitySensor::sensor_get_event()
{
    guint64 ret_time  = 0;
    guint32 ret_value = 0;

    int constexpr timeout = 1000;
    auto constexpr null_cancellable = nullptr;
    GError *error = NULL;

    auto const result = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager/proximitysensor",
        "local.ProximitySensor",
        "proximity",
        nullptr, // g_variant_new("(tu)", &ret_time, &ret_value),
        G_VARIANT_TYPE("((tu))"),
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    if (!result){
        log->log(log_tag, "Failed to call Inhibit: %s\n", error->message );
    }

    g_variant_get(result, "((tu))", &ret_time, &ret_value);
    g_variant_unref(result);
    log->log(log_tag, "ret_time: %d ret_value:%d", ret_time, ret_value);
    
    return (ret_time != 0)? uint32_t(ret_value) : -1;
}

void repowerd::UbuntuProximitySensor::sensor_thread(void)
{
    log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s  \e[0m\n", __FILE__, __LINE__, __FUNCTION__);
    std::default_random_engine generator(time(NULL));
    std::uniform_int_distribution<int> distribution(0, 1000);

    while (enabled)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(250));
        log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s  \e[0m\n", __FILE__, __LINE__, __FUNCTION__);
        int value = sensor_get_event();
        log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s:  %f \e[0m\n", __FILE__, __LINE__, __FUNCTION__, value);
        handle_proximity_event(value > 0 ? ProximityState::far : ProximityState::near );
    }
}