#include <string>

std::string set_fan_mode(const std::string &value);
std::string get_fan_mode();

std::string get_fan_speed(const std::string &fan_num);
std::string get_fan_max_speed(const std::string &fan_num);
std::string set_fan_speed(const std::string &fan_num, const std::string &speed, bool trigger_mode = true, bool update_cache = true);
std::string get_cpu_temperature();
void shutdown_fan_controller();
