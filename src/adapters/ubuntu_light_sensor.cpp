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

#include "ubuntu_light_sensor.h"
#include "event_loop_handler_registration.h"
#include "src/core/log.h"

#include <stdexcept>
#include <thread>
#include <chrono>
#include <random>

namespace
{
    auto const null_handler = [](double) {};
    char const *const log_tag = "UbuntuLightSensor";
}

repowerd::UbuntuLightSensor::UbuntuLightSensor(std::string const& dbus_bus_address,std::shared_ptr<Log> const &log)
    : /* sensor{ua_sensors_light_new()}, */
      dbus_connection{dbus_bus_address},
      event_loop{"Light"},
      handler{null_handler},
      enabled{false},
      log{log}
{
    log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s \e[0m\n", __FILE__, __LINE__, __FUNCTION__);
    /*
    if (!sensor)
        throw std::runtime_error("Failed to allocate light sensor");

    ua_sensors_light_set_reading_cb(sensor, static_sensor_reading_callback, this);
  */
}

repowerd::UbuntuLightSensor::~UbuntuLightSensor()
{
    log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s \e[0m\n", __FILE__, __LINE__, __FUNCTION__);
    if (sensor_event_thread.joinable())
    {
        sensor_event_thread.join();
    }
}

repowerd::HandlerRegistration repowerd::UbuntuLightSensor::register_light_handler(
    LightHandler const &handler)
{
    return EventLoopHandlerRegistration{
        event_loop,
        [this, &handler]
        { this->handler = handler; },
        [this]
        { this->handler = null_handler; }};
}

void repowerd::UbuntuLightSensor::enable_light_events()
{
    log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s \e[0m\n", __FILE__, __LINE__, __FUNCTION__);

    event_loop.enqueue(
                  [this]
                  {
                      if (!enabled)
                      {
                          //ua_sensors_light_enable(sensor);

                          enabled = true;
                          sensor_start();
                          sensor_event_thread = std::thread(std::mem_fn(&repowerd::UbuntuLightSensor::sensor_thread), this);
                      }
                  })
        .get();
}

void repowerd::UbuntuLightSensor::disable_light_events()
{
    event_loop.enqueue(
                  [this]
                  {
                      if (enabled)
                      {
                          //ua_sensors_light_disable(sensor);
                          enabled = false;
                          if (sensor_event_thread.joinable()){
                              sensor_event_thread.join();
                          }
                          sensor_stop();
                      }
                  })
        .get();
}

/* void repowerd::UbuntuLightSensor::static_sensor_reading_callback(
    UASLightEvent* event, void* context)
{
    // auto const uls = static_cast<UbuntuLightSensor*>(context);
    // float light_value{0.0f};
    // uas_light_event_get_light(event, &light_value);
    // uls->event_loop.enqueue([uls, light_value] { uls->handle_light_event(light_value); });
} */

void repowerd::UbuntuLightSensor::handle_light_event(double light)
{
    handler(light);
}



void repowerd::UbuntuLightSensor::sensor_start()
{
    int constexpr timeout = 1000;
    auto constexpr null_cancellable = nullptr;
    GError *error = NULL;

    auto const result = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager",
        "local.SensorManager",
        "loadPlugin",
        g_variant_new("(s)", "alssensor"),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    if (!result){
        log->log(log_tag, "Failed loadPlugin: %s\n", error->message );
    }

   auto const result2 = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager/alssensor",
        "local.ALSSensor",
        "start",
        g_variant_new("(i)",255),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    if (!result2){
        log->log(log_tag, "Failed ALSSensor start: %s\n", error->message );
    }
}

void repowerd::UbuntuLightSensor::sensor_stop()
{
    int constexpr timeout = 1000;
    auto constexpr null_cancellable = nullptr;
    GError *error = NULL;

    auto const result = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager/alssensor",
        "local.ALSSensor",
        "stop",
        g_variant_new("(i)",255),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    if (!result){
        log->log(log_tag, "Failed ALSSensor stop: %s\n", error->message );
    }
}

double repowerd::UbuntuLightSensor::sensor_get_event(void)
{
    guint64 ret_time  = 0;
    guint32 ret_value = 0;

    int constexpr timeout = 1000;
    auto constexpr null_cancellable = nullptr;
    GError *error = NULL;

    auto const result = g_dbus_connection_call_sync(
        dbus_connection,
        "com.nokia.SensorService",
        "/SensorManager/alssensor",
        "local.ALSSensor",
        "lux",
        nullptr, // g_variant_new("(tu)", &ret_time, &ret_value),
        G_VARIANT_TYPE("((tu))"),
        G_DBUS_CALL_FLAGS_NONE,
        timeout,
        null_cancellable,
        &error);

    log->log(log_tag, "%d %d", ret_time, ret_value);

    if (!result){
        log->log(log_tag, "Failed to call Inhibit: %s\n", error->message );
    }

    g_variant_get(result, "((tu))", &ret_time, &ret_value);
    g_variant_unref(result);
    log->log(log_tag, "%d %d", ret_time, ret_value);

    return double(ret_value);
}

void repowerd::UbuntuLightSensor::sensor_thread(void)
{
    log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s  \e[0m\n", __FILE__, __LINE__, __FUNCTION__);
    std::default_random_engine generator(time(NULL));
    std::uniform_int_distribution<int> distribution(0, 1000);

    while (enabled)
    {
        //double value = (double)distribution(generator);
        double value = sensor_get_event();
        log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s:  %f \e[0m\n", __FILE__, __LINE__, __FUNCTION__, value);
        handle_light_event(value);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        //log->log(log_tag,"\e[0;36m[dba_DEBUG] %s:%d %s  \e[0m\n", __FILE__, __LINE__, __FUNCTION__);
    }
}
