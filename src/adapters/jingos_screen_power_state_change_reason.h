/*			
* Copyright © 2021-04-10 jingos.			
* 			
* Authored by:dengbaoan <dengbaoan@jingos.com>			
*/

#pragma once

namespace repowerd
{

enum class JingosScreenPowerStateChangeReason
{
    unknown = 0,
    inactivity = 1,
    power_key = 2,
    proximity = 3,
    notification = 4,
    snap_decision = 5,
    call_done = 6
};

}
