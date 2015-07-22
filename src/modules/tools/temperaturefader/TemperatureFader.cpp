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

#include "TemperatureFader.h"
#include "libs/Module.h"
#include "libs/Kernel.h"
#include "modules/tools/temperaturecontrol/TemperatureControlPublicAccess.h"
#include "SwitchPublicAccess.h"

#include "utils.h"
#include "Gcode.h"
#include "Config.h"
#include "ConfigValue.h"
#include "checksumm.h"
#include "PublicData.h"
#include "StreamOutputPool.h"
#include "TemperatureControlPool.h"

#define temperaturefader_checksum                   CHECKSUM("temperaturefader")
#define enable_checksum                             CHECKSUM("enable")
#define temperaturefader_hotend_checksum            CHECKSUM("hotend")
#define temperaturefader_min_fade_temp_checksum     CHECKSUM("min_fade_temp")
#define temperaturefader_max_fade_temp_checksum     CHECKSUM("max_fade_temp")
#define temperaturefader_min_fade_pwm_checksum      CHECKSUM("min_fade_pwm")
#define temperaturefader_max_fade_pwm_checksum      CHECKSUM("max_fade_pwm")
#define temperaturefader_type_checksum              CHECKSUM("type")
#define temperaturefader_switch_checksum            CHECKSUM("switch")
#define temperaturefader_heatup_poll_checksum       CHECKSUM("heatup_poll")
#define temperaturefader_cooldown_poll_checksum     CHECKSUM("cooldown_poll")
#define temperaturefader_fading_poll_checksum       CHECKSUM("fading_poll")
#define designator_checksum                         CHECKSUM("designator")

#define default_heatup_poll     15
#define default_cooldown_poll   60
#define default_fading_poll     1
#define default_min_fade_temp   50.0f
#define default_max_fade_temp   150.0f
#define default_min_fade_pwm    0
#define default_max_fade_pwm    255


TemperatureFader::TemperatureFader()
: temp_controllers()
, temperaturefader_min_fade_temp(default_min_fade_temp)
, temperaturefader_max_fade_temp(default_max_fade_temp)
, temperaturefader_min_fade_pwm(default_min_fade_pwm)
, temperaturefader_max_fade_pwm(default_max_fade_pwm)
, temperaturefader_switch_cs(0)
, temperaturefader_heatup_poll(default_heatup_poll)
, temperaturefader_cooldown_poll(default_cooldown_poll)
, temperaturefader_fading_poll(default_fading_poll)
, countdown_timer(0)
, temperaturefader_pwm_value(0)
, temperaturefader_state(false)
{
}

TemperatureFader::~TemperatureFader() {
    
}

// Load module
void TemperatureFader::on_module_loaded()
{
    vector<uint16_t> modulist;
    // allow for multiple temperature switches
    THEKERNEL->config->get_module_list(&modulist, temperaturefader_checksum);
    for (auto m : modulist) {
        load_config(m);
    }

    // no longer need this instance as it is just used to load the other instances
    delete this;
}


bool TemperatureFader::load_config(uint16_t modcs)
{
    // see if enabled
    if (!THEKERNEL->config->value(temperaturefader_checksum, modcs, enable_checksum)->by_default(false)->as_bool()) {
        return false;
    }

    // create a temperature control and load settings

    char designator= 0;
    string s= THEKERNEL->config->value(temperaturefader_checksum, modcs, designator_checksum)->by_default("")->as_string();
    if(s.empty()){
        // for backward compatibility temperatureswitch.hotend will need designator 'T' by default @DEPRECATED
        if(modcs == temperaturefader_hotend_checksum) designator= 'T';

    }else{
        designator= s[0];
    }

    if(designator == 0) return false; // no designator then not valid

    // create a new temperature switch module
    TemperatureFader *ts= new TemperatureFader();

    // get the list of temperature controllers and remove any that don't have designator == specified designator
    auto& tempcontrollers= THEKERNEL->temperature_control_pool->get_controllers();

    // see what its designator is and add to list of it the one we specified
    void *returned_temp;
    for (auto controller : tempcontrollers) {
        bool temp_ok = PublicData::get_value(temperature_control_checksum, controller, current_temperature_checksum, &returned_temp);
        if (temp_ok) {
            struct pad_temperature temp =  *static_cast<struct pad_temperature *>(returned_temp);
            if (temp.designator[0] == designator) {
                ts->temp_controllers.push_back(controller);
            }
        }
    }

    // if we don't have any matching controllers, then not valid
    if (ts->temp_controllers.empty()) {
        delete ts;
        return false;
    }

    // load settings from config file
    s = THEKERNEL->config->value(temperaturefader_checksum, modcs, temperaturefader_switch_checksum)->by_default("")->as_string();
    if(s.empty()) {
        // handle old configs where this was called type @DEPRECATED
        s = THEKERNEL->config->value(temperaturefader_checksum, modcs, temperaturefader_type_checksum)->by_default("")->as_string();
        if(s.empty()) {
            // no switch specified so invalid entry
            delete this;
            return false;
        }
    }
    ts->temperaturefader_switch_cs= get_checksum(s); // checksum of the switch to use

    ts->temperaturefader_min_fade_temp = THEKERNEL->config->value(temperaturefader_checksum, modcs, temperaturefader_min_fade_temp_checksum)->by_default(default_min_fade_temp)->as_number();
    ts->temperaturefader_max_fade_temp = THEKERNEL->config->value(temperaturefader_checksum, modcs, temperaturefader_max_fade_temp_checksum)->by_default(default_max_fade_temp)->as_number();
    ts->temperaturefader_min_fade_pwm = THEKERNEL->config->value(temperaturefader_checksum, modcs, temperaturefader_min_fade_pwm_checksum)->by_default(default_min_fade_pwm)->as_number();
    ts->temperaturefader_max_fade_pwm = THEKERNEL->config->value(temperaturefader_checksum, modcs, temperaturefader_max_fade_pwm_checksum)->by_default(default_max_fade_pwm)->as_number();
    
    if (temperaturefader_max_fade_temp < temperaturefader_min_fade_temp) {
        temperaturefader_max_fade_temp = temperaturefader_min_fade_temp;
    }
    
    if (temperaturefader_max_fade_pwm < temperaturefader_min_fade_pwm) {
        temperaturefader_max_fade_pwm = temperaturefader_min_fade_pwm;
    }
    
    // these are to tune the heatup and cooldown polling frequencies
    ts->temperaturefader_heatup_poll = THEKERNEL->config->value(temperaturefader_checksum, modcs, temperaturefader_heatup_poll_checksum)->by_default(default_heatup_poll)->as_number();
    ts->temperaturefader_cooldown_poll = THEKERNEL->config->value(temperaturefader_checksum, modcs, temperaturefader_cooldown_poll_checksum)->by_default(default_cooldown_poll)->as_number();
    ts->countdown_timer = ts->temperaturefader_heatup_poll;

    // Register for events
    ts->register_for_event(ON_SECOND_TICK);

    return true;
}

// Called once a second but we only need to service on the cooldown and heatup poll intervals
void TemperatureFader::on_second_tick(void *argument)
{
    if (countdown_timer <= 1) {
        float current_temp = get_highest_temperature();
        
        if (current_temp <= temperaturefader_min_fade_temp) { // turn off
            set_pwm(temperaturefader_min_fade_pwm);
            countdown_timer = temperaturefader_heatup_poll;
        } else if (current_temp >= temperaturefader_max_fade_temp) { // on max
            set_pwm(temperaturefader_max_fade_pwm);
            countdown_timer = temperaturefader_cooldown_poll;
        } else { // fading
            float frac = (current_temp - temperaturefader_min_fade_temp) / (temperaturefader_max_fade_temp - temperaturefader_min_fade_temp);
            float value = this->temperaturefader_min_fade_pwm + int(frac * (temperaturefader_max_fade_pwm - temperaturefader_min_fade_pwm));
            set_pwm(value);
            countdown_timer = this->temperaturefader_fading_poll;
        }
    } else {
        countdown_timer--;
    }
}

// Get the highest temperature from the set of temperature controllers
float TemperatureFader::get_highest_temperature()
{
    void *returned_temp;
    float high_temp = 0.0;

    for (auto controller : temp_controllers) {
        bool temp_ok = PublicData::get_value(temperature_control_checksum, controller, current_temperature_checksum, &returned_temp);
        if (temp_ok) {
            struct pad_temperature temp =  *static_cast<struct pad_temperature *>(returned_temp);
            // check if this controller's temp is the highest and save it if so
            if (temp.current_temperature > high_temp) {
                high_temp = temp.current_temperature;
            }
        }
    }
    return high_temp;
}

// Sets the output pwm value
void TemperatureFader::set_pwm(float value)
{
    if (temperaturefader_pwm_value != value) {
        
        // toggle the switch
        bool on = value > temperaturefader_min_fade_pwm;
        if (on != temperaturefader_state) {
            temperaturefader_state = on;
            bool ok = PublicData::set_value(switch_checksum, this->temperaturefader_switch_cs, state_checksum, &this->temperaturefader_state);
            if (!ok) {
                THEKERNEL->streams->printf("Failed updating TemperatureFader state.\r\n");
            }
        }
        
        // update pwm value
        this->temperaturefader_pwm_value = value;
        bool ok = PublicData::set_value(switch_checksum, this->temperaturefader_switch_cs, value_checksum, &this->temperaturefader_pwm_value);
        if (!ok) {
            THEKERNEL->streams->printf("Failed updating TemperatureFader pwm value.\r\n");
        }
    }
}
