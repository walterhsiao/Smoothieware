/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

/*
 TemperatureFader is an optional module that will automatically modulate a PWM output based on the
 temperature range. It is very similar to TemperatureSwitch, but you specify a transition temperature
 range and an output PWM range.  Temperature values less than the transition range will result in the
 output being turned off, values above the range will result in the output being set to the max value
 in the output PWM range.  It can used to control lights and fans.

Based on TemperatureSwitch from Michael Hackney, mhackney@eclecticangler.com
*/

#ifndef TEMPERATUREFADER_MODULE_H
#define TEMPERATUREFADER_MODULE_H

using namespace std;

#include "libs/Module.h"
#include <string>
#include <vector>

class TemperatureFader : public Module
{
    public:
        TemperatureFader();
        virtual ~TemperatureFader();

        void on_module_loaded();
        void on_second_tick(void *argument);

    private:
        bool load_config(uint16_t modcs);

        // get the highest temperature from the set of configured temperature controllers
        float get_highest_temperature();

        // set the output pwm value
        void set_pwm(float value);
    
    private:
    
        // the set of temperature controllers that match the reuired designator prefix
        vector<uint16_t> temp_controllers;
    
        // temperatureswitch.hotend.min_fade_temp
        float temperaturefader_min_fade_temp;
    
        // temperatureswitch.hotend.max_fade_temp
        float temperaturefader_max_fade_temp;

        // temperatureswitch.hotend.min_fade_pwm
        uint8_t temperaturefader_min_fade_pwm;
    
        // temperatureswitch.hotend.max_fade_pwm
        uint8_t temperaturefader_max_fade_pwm;
    
        // temperatureswitch.hotend.switch
        uint16_t temperaturefader_switch_cs;

        // check temps on heatup every X seconds
        // this can be set in config: temperatureswitch.hotend.heatup_poll
        uint16_t temperaturefader_heatup_poll;

        // check temps on cooldown every X seconds
        // this can be set in config: temperatureswitch.hotend.cooldown_poll
        uint16_t temperaturefader_cooldown_poll;

        // check temps while fading every X seconds
        // this can be set in config: temperatureswitch.hotend.fading_poll
        uint16_t temperaturefader_fading_poll;

        // timer to track the polling interval
        int16_t countdown_timer;
    
        // swtich pwm value
        float temperaturefader_pwm_value;
    
        // switch on/off state
        bool temperaturefader_state;
};

#endif
