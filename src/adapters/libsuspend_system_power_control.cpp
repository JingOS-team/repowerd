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

#include "libsuspend_system_power_control.h"
//#include "libsuspend/libsuspend.h"
#include "libsuspend/include/suspend/autosuspend.h"
#include "src/core/log.h"
#include <iostream>
#include <stdexcept>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
namespace
{
char const* const log_tag = "LibsuspendSystemPowerControl";
}

static int sysfs_write(const char *path, const void *buf, int len)
{
    int fd;
    ssize_t ret;

    fd = open(path, O_WRONLY);
    if (fd == -1)
        return -errno;

    ret = write(fd, buf, len);
    if (ret == -1)
        ret = -errno;

    close(fd);
    return ret;
}
static int sysfs_read(const char *path, void *buf, int len)
{
    int fd;
    ssize_t ret;

    fd = open(path, O_RDONLY);
    if (fd == -1)
        return -errno;

    ret = read(fd, buf, len);
    if (ret == -1)
        ret = -errno;

    close(fd);
    return ret;
}
static int acquire_wake_lock(const char *name)
{
    int ret = sysfs_write("/sys/power/wake_lock", name, strlen(name));
    return ret < 0 ? ret : 0;
}

static int release_wake_lock(const char *name)
{
    int ret = sysfs_write("/sys/power/wake_unlock", name, strlen(name));
    return ret < 0 ? ret : 0;
}
static void wakeup_callback(bool success){
    std::cout <<__FILE__<< __FUNCTION__ << "wakeup_callback";
    if(success){
        release_wake_lock("repowerd_wakeup_callback");
        acquire_wake_lock("repowerd_wakeup_callback 5000000000");
    }
}

repowerd::LibsuspendSystemPowerControl::LibsuspendSystemPowerControl(
    std::shared_ptr<Log> const& log)
    : log{log}
{
    autosuspend_set_wakeup_callback(&wakeup_callback);
    autosuspend_enable();
    log->log(log_tag, "\e[0;36m[dba_DEBUG] %s:%d %s: %s \e[0m\n",__FILE__,__LINE__,__FUNCTION__ ,"autosuspend_enable");
}

void repowerd::LibsuspendSystemPowerControl::start_processing()
{
}

repowerd::HandlerRegistration
repowerd::LibsuspendSystemPowerControl::register_system_resume_handler(
    SystemResumeHandler const&)
{
    return HandlerRegistration{};
}

repowerd::HandlerRegistration
repowerd::LibsuspendSystemPowerControl::register_system_allow_suspend_handler(
    SystemAllowSuspendHandler const&)
{
    return HandlerRegistration{};
}

repowerd::HandlerRegistration
repowerd::LibsuspendSystemPowerControl::register_system_disallow_suspend_handler(
    SystemDisallowSuspendHandler const&)
{
    return HandlerRegistration{};
}

void repowerd::LibsuspendSystemPowerControl::allow_automatic_suspend(
    std::string const& id)
{
    fprintf(stderr, "\e[0;36m  %s:%d %s id: %s: \e[0m\n",__FILE__,__LINE__,__FUNCTION__, id.c_str());

    std::lock_guard<std::mutex> lock{suspend_mutex};

    log->log(log_tag, "allow_suspend(%s)", id.c_str());

    if (suspend_disallowances.erase(id) > 0 &&
        suspend_disallowances.empty()){
        log->log(log_tag, "Preparing for suspend");
        release_wake_lock("repowerd");
    }
}

void repowerd::LibsuspendSystemPowerControl::disallow_automatic_suspend(
    std::string const& id)
{
    fprintf(stderr, "\e[0;36m  %s:%d %s id: %s: \e[0m\n",__FILE__,__LINE__,__FUNCTION__, id.c_str());

    std::lock_guard<std::mutex> lock{suspend_mutex};
    auto const could_be_suspended = suspend_disallowances.empty();

    suspend_disallowances.insert(id);

    if (could_be_suspended){
        log->log(log_tag, "exiting suspend");
        acquire_wake_lock("repowerd");
    }
}

void repowerd::LibsuspendSystemPowerControl::power_off()
{
    fprintf(stderr, "\e[0;36m  %s:%d %s %s: \e[0m\n",__FILE__,__LINE__,__FUNCTION__, "shutdown -P now");

    if (system("shutdown -P now")) {}
}

void repowerd::LibsuspendSystemPowerControl::suspend()
{
}

void repowerd::LibsuspendSystemPowerControl::allow_default_system_handlers()
{
}

void repowerd::LibsuspendSystemPowerControl::disallow_default_system_handlers()
{
}
