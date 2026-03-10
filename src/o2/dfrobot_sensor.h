#pragma once

void dfrobot_sensor_setup();
void dfrobot_sensor_loop();
bool dfrobot_get_last(float &o2_percent);
bool dfrobot_is_frc_done();