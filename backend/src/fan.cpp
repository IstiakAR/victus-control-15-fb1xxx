#include <cstdlib>
#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <sys/wait.h>
#include <vector>

#include "fan.hpp"
#include "util.hpp"
#include "validation.hpp"

static std::mutex mode_mutex;
static std::string requested_mode = "AUTO";
static std::atomic<bool> fan_mode_requires_root(false);

static std::once_flag cpu_sensor_once;
static std::once_flag gpu_sensor_once;
static std::once_flag gpu_usage_once;
static std::optional<std::string> cpu_temp_path;
static std::optional<std::string> gpu_temp_path;
static std::optional<std::string> gpu_busy_path;
static std::atomic<bool> cpu_sensor_warned(false);
static std::atomic<bool> gpu_sensor_warned(false);
static std::atomic<bool> gpu_usage_warned(false);

static std::array<int, 2> fan_max_cache = {0, 0};
static std::once_flag fan_max_once[2];

static constexpr const char *kSudoPath = "/usr/bin/sudo";
static constexpr const char *kFanModeHelperPath = "/usr/bin/set-fan-mode.sh";
static constexpr const char *kFanSpeedHelperPath = "/usr/bin/set-fan-speed.sh";

struct CpuSampleTimes {
    unsigned long long idle;
    unsigned long long total;
};

static std::string to_lower_copy(const std::string &input)
{
    std::string lowered = input;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}


static std::optional<std::string> find_thermal_zone_by_type(const std::vector<std::string> &hints)
{
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) {
        return std::nullopt;
    }

    std::optional<std::string> fallback;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (std::strncmp(entry->d_name, "thermal_zone", 12) != 0) {
            continue;
        }

        std::string base_path = std::string("/sys/class/thermal/") + entry->d_name;
        std::ifstream type_file(base_path + "/type");
        if (!type_file) {
            continue;
        }

        std::string sensor_type;
        std::getline(type_file, sensor_type);
        std::string lowered = to_lower_copy(sensor_type);

        if (!fallback) {
            fallback = base_path + "/temp";
        }

        for (const auto &hint : hints) {
            if (lowered.find(hint) != std::string::npos) {
                closedir(dir);
                return base_path + "/temp";
            }
        }
    }

    closedir(dir);
    return fallback;
}

static std::optional<std::string> find_hwmon_temp_sensor(const std::vector<std::string> &name_hints,
                                                         const std::vector<std::string> &label_hints)
{
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) {
        return std::nullopt;
    }

    std::optional<std::string> fallback;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (std::strncmp(entry->d_name, "hwmon", 5) != 0) {
            continue;
        }

        std::string base_path = std::string("/sys/class/hwmon/") + entry->d_name;
        std::string name_path = base_path + "/name";
        std::ifstream name_file(name_path);
        std::string name_value;
        if (name_file) {
            std::getline(name_file, name_value);
        }
        std::string lowered_name = to_lower_copy(name_value);

        bool name_matches = false;
        for (const auto &hint : name_hints) {
            if (!hint.empty() && lowered_name.find(hint) != std::string::npos) {
                name_matches = true;
                break;
            }
        }

        DIR *inner = opendir(base_path.c_str());
        if (!inner) {
            continue;
        }

        std::vector<std::string> input_candidates;
        struct dirent *inner_entry;
        while ((inner_entry = readdir(inner)) != nullptr)
        {
            std::string file_name = inner_entry->d_name;
            if (file_name.rfind("temp", 0) != 0) {
                continue;
            }
            if (file_name.find("_input") == std::string::npos) {
                continue;
            }

            std::string input_path = base_path + "/" + file_name;
            input_candidates.push_back(input_path);

            std::string prefix = file_name.substr(0, file_name.find("_input"));
            std::string label_path = base_path + "/" + prefix + "_label";

            std::ifstream label_file(label_path);
            if (label_file) {
                std::string label_value;
                std::getline(label_file, label_value);
                std::string lowered_label = to_lower_copy(label_value);

                for (const auto &hint : label_hints) {
                    if (!hint.empty() && lowered_label.find(hint) != std::string::npos) {
                        closedir(inner);
                        closedir(dir);
                        return input_path;
                    }
                }
            }
        }
        closedir(inner);

        if (name_matches && !input_candidates.empty()) {
            closedir(dir);
            return input_candidates.front();
        }

        if (!fallback && !input_candidates.empty()) {
            fallback = input_candidates.front();
        }
    }

    closedir(dir);
    return fallback;
}

static std::optional<std::string> locate_cpu_temp_sensor()
{
    std::call_once(cpu_sensor_once, []() {
        const std::vector<std::string> hwmon_name_hints = {"k10temp", "coretemp", "zenpower", "cpu", "package", "soc"};
        const std::vector<std::string> hwmon_label_hints = {"cpu", "package", "soc"};
        cpu_temp_path = find_hwmon_temp_sensor(hwmon_name_hints, hwmon_label_hints);

        if (!cpu_temp_path) {
            const std::vector<std::string> zone_hints = {"x86_pkg", "tctl", "cpu", "soc"};
            cpu_temp_path = find_thermal_zone_by_type(zone_hints);
        }

        if (!cpu_temp_path && !cpu_sensor_warned.exchange(true)) {
            std::cerr << "AUTO mode: CPU thermal sensor not found" << std::endl;
        }
    });
    return cpu_temp_path;
}

static std::optional<std::string> locate_gpu_temp_sensor()
{
    std::call_once(gpu_sensor_once, []() {
        const std::vector<std::string> hwmon_name_hints = {"amdgpu", "radeon", "nvidia", "gpu"};
        const std::vector<std::string> hwmon_label_hints = {"edge", "gpu", "junction", "hotspot"};
        gpu_temp_path = find_hwmon_temp_sensor(hwmon_name_hints, hwmon_label_hints);

        if (!gpu_temp_path) {
            const std::vector<std::string> zone_hints = {"gpu", "amdgpu", "nvidia"};
            gpu_temp_path = find_thermal_zone_by_type(zone_hints);
        }

        if (!gpu_temp_path && !gpu_sensor_warned.exchange(true)) {
            std::cerr << "AUTO mode: GPU thermal sensor not found" << std::endl;
        }
    });
    return gpu_temp_path;
}

static std::optional<std::string> locate_gpu_busy_file()
{
    std::call_once(gpu_usage_once, []() {
        DIR *dir = opendir("/sys/class/drm");
        if (!dir) {
            if (!gpu_usage_warned.exchange(true)) {
                std::cerr << "AUTO mode: /sys/class/drm unavailable" << std::endl;
            }
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (std::strncmp(entry->d_name, "card", 4) != 0) {
                continue;
            }

            std::string candidate = std::string("/sys/class/drm/") + entry->d_name + "/device/gpu_busy_percent";
            std::ifstream test(candidate);
            if (test)
            {
                gpu_busy_path = candidate;
                break;
            }
        }

        closedir(dir);
        if (!gpu_busy_path && !gpu_usage_warned.exchange(true)) {
            std::cerr << "AUTO mode: GPU usage source not found" << std::endl;
        }
    });
    return gpu_busy_path;
}

static std::optional<double> read_temperature_celsius(const std::string &path)
{
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    long value = 0;
    file >> value;
    if (file.fail()) {
        return std::nullopt;
    }

    return static_cast<double>(value) / 1000.0;
}

static std::optional<double> read_cpu_usage_pct()
{
    std::ifstream stat_file("/proc/stat");
    if (!stat_file) {
        return std::nullopt;
    }

    std::string line;
    std::getline(stat_file, line);
    std::istringstream iss(line);

    std::string label;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    iss >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    if (label != "cpu") {
        return std::nullopt;
    }

    unsigned long long idle_all = idle + iowait;
    unsigned long long non_idle = user + nice + system + irq + softirq + steal;
    unsigned long long total = idle_all + non_idle;

    static std::mutex cpu_usage_mutex;
    static std::optional<CpuSampleTimes> previous_cpu_times;

    std::lock_guard<std::mutex> lock(cpu_usage_mutex);
    if (!previous_cpu_times) {
        previous_cpu_times = CpuSampleTimes{idle_all, total};
        return std::nullopt;
    }

    unsigned long long total_diff = total - previous_cpu_times->total;
    unsigned long long idle_diff = idle_all - previous_cpu_times->idle;
    previous_cpu_times = CpuSampleTimes{idle_all, total};

    if (total_diff == 0) {
        return std::nullopt;
    }

    double usage = static_cast<double>(total_diff - idle_diff) / static_cast<double>(total_diff);
    return usage * 100.0;
}

static std::optional<double> read_gpu_usage_pct()
{
    static std::optional<std::string> gpu_busy_path = locate_gpu_busy_file();
    if (!gpu_busy_path) {
        return std::nullopt;
    }

    std::ifstream file(*gpu_busy_path);
    if (!file) {
        return std::nullopt;
    }

    double value = 0.0;
    file >> value;
    if (file.fail()) {
        return std::nullopt;
    }

    return value;
}

static int fan_max_for_index(size_t index)
{
    std::call_once(fan_max_once[index], [index]() {
        std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");
        if (!hwmon_path.empty()) {
            std::string path = hwmon_path + "/fan" + std::to_string(index + 1) + "_max";
            std::ifstream file(path);
            if (file) {
                int value = 0;
                file >> value;
                if (!file.fail() && value > 0) {
                    fan_max_cache[index] = value;
                    return;
                }
            }
        }
        fan_max_cache[index] = (index == 0) ? 5800 : 6100;
    });
    return fan_max_cache[index];
}

static int encode_pwm_mode(const std::string &mode)
{
    if (mode == "AUTO") return 2;
    if (mode == "MAX") return 0;
    return -1;
}

static int run_helper_command(const std::vector<std::string> &args)
{
    if (args.empty()) {
        errno = EINVAL;
        return -1;
    }

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const auto &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        execv(args.front().c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }

    return status;
}

static std::string apply_fan_mode_with_sudo(const std::string &mode)
{
    int result = run_helper_command({kSudoPath, kFanModeHelperPath, mode});

    if (result == 0) {
        return "OK";
    }

    if (result == -1) {
        std::cerr << "set-fan-mode.sh invocation failed: " << strerror(errno) << std::endl;
        return "ERROR: Unable to set fan mode";
    }

    if (WIFEXITED(result)) {
        std::cerr << "set-fan-mode.sh failed with exit code: " << WEXITSTATUS(result) << std::endl;
    } else {
        std::cerr << "set-fan-mode.sh terminated abnormally when setting mode " << mode << std::endl;
    }

    return "ERROR: Unable to set fan mode";
}

static std::string write_hw_fan_mode(const std::string &mode)
{
    std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

    if (!hwmon_path.empty())
    {
        bool use_sudo = fan_mode_requires_root.load(std::memory_order_acquire);
        int encoded_mode = encode_pwm_mode(mode);
        if (encoded_mode < 0) {
            return "ERROR: Invalid fan mode: " + mode;
        }

        if (!use_sudo) {
            std::string control_path = hwmon_path + "/pwm1_enable";
            errno = 0;
            std::ofstream fan_ctrl(control_path);

            if (fan_ctrl) {
                fan_ctrl << encoded_mode;
                fan_ctrl.flush();
                if (!fan_ctrl.fail()) {
                    return "OK";
                }

                int write_errno = errno;
                std::cerr << "Failed to write fan mode via sysfs: " << strerror(write_errno) << std::endl;
                if (write_errno != EACCES && write_errno != EPERM) {
                    return "ERROR: Failed to write fan mode";
                }
                fan_mode_requires_root.store(true, std::memory_order_release);
                use_sudo = true;
            } else {
                int open_errno = errno;
                std::cerr << "Failed to open fan mode control (" << control_path << "): " << strerror(open_errno) << std::endl;
                if (open_errno != EACCES && open_errno != EPERM) {
                    return "ERROR: Unable to set fan mode";
                }
                fan_mode_requires_root.store(true, std::memory_order_release);
                use_sudo = true;
            }
        }

        if (use_sudo) {
            return apply_fan_mode_with_sudo(mode);
        }

        return "ERROR: Unable to set fan mode";
    }

    return "ERROR: Hwmon directory not found";
}

std::string get_fan_mode()
{
    std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

    if (!hwmon_path.empty())
    {
        std::string pwm_path = hwmon_path + "/pwm1_enable";
        std::ifstream fan_ctrl(pwm_path);

        if (fan_ctrl)
        {
            std::stringstream buffer;
            buffer << fan_ctrl.rdbuf();
            std::string fan_mode = buffer.str();

            fan_mode.erase(fan_mode.find_last_not_of(" \n\r\t") + 1);

            if (fan_mode == "2")
                return "AUTO";
            else if (fan_mode == "0")
                return "MAX";
            else
                return "AUTO";
        }
        else
        {
            std::cerr << "Failed to open fan control file. Error: " << strerror(errno) << std::endl;
            return "ERROR: Unable to read fan mode";
        }
    }
    else
    {
        std::cerr << "Hwmon directory not found" << std::endl;
        return "ERROR: Hwmon directory not found";
    }
}

std::string set_fan_mode(const std::string &mode)
{
    if (mode != "AUTO" && mode != "MAX") {
        return "ERROR: Invalid fan mode. Use AUTO or MAX.";
    }

    auto result = write_hw_fan_mode(mode);
    if (result == "OK") {
        std::lock_guard<std::mutex> lock(mode_mutex);
        requested_mode = mode;
    }
    return result;
}

std::string set_fan_speed(const std::string &fan_num, const std::string &speed, bool, bool)
{
    auto fan_index = fan_index_from_string(fan_num);
    if (!fan_index) {
        return "ERROR: Invalid fan number";
    }

    std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

    if (!hwmon_path.empty())
    {
        std::string pwm_path = hwmon_path + "/pwm" + std::to_string(*fan_index + 1);
        int target_speed = 0;
        if (!parse_strict_int(speed, &target_speed)) {
            return "ERROR: Invalid speed value";
        }

        errno = 0;
        std::ofstream pwm_file(pwm_path);

        if (pwm_file) {
            pwm_file << target_speed;
            pwm_file.flush();
            if (!pwm_file.fail()) {
                return "OK";
            }
            return "ERROR: Failed to write fan speed";
        } else {
            std::cerr << "Failed to open fan speed control: " << strerror(errno) << std::endl;
            return "ERROR: Unable to set fan speed";
        }
    }

    return "ERROR: Hwmon directory not found";
}

std::string get_fan_speed(const std::string &fan_num)
{
    auto fan_index = fan_index_from_string(fan_num);
    if (!fan_index) {
        return "ERROR: Invalid fan number";
    }

    std::string hwmon_path = find_hwmon_directory("/sys/devices/platform/hp-wmi/hwmon");

    if (!hwmon_path.empty())
    {
        std::string fan_path =
            hwmon_path + "/fan" + std::to_string(*fan_index + 1) + "_input";
        std::ifstream fan_file(fan_path);

        if (fan_file)
        {
            std::stringstream buffer;
            buffer << fan_file.rdbuf();

            std::string fan_speed = buffer.str();
            fan_speed.erase(fan_speed.find_last_not_of(" \n\r\t") + 1);

            return fan_speed;
        }
        else
        {
            std::cerr << "Failed to open fan speed file. Error: " << strerror(errno) << std::endl;
            return "ERROR: Unable to read fan speed";
        }
    }
    else
    {
        std::cerr << "Hwmon directory not found" << std::endl;
        return "ERROR: Hwmon directory not found";
    }
}

std::string get_fan_max_speed(const std::string &fan_num)
{
    auto fan_index = fan_index_from_string(fan_num);
    if (!fan_index) {
        return "ERROR: Invalid fan number";
    }

    return std::to_string(fan_max_for_index(*fan_index));
}

std::string get_cpu_temperature()
{
    auto cpu_temp = read_temperature_celsius(*locate_cpu_temp_sensor());
    if (!cpu_temp) {
        return "ERROR: CPU temperature unavailable";
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << *cpu_temp;
    return oss.str();
}

std::string get_gpu_temperature()
{
    auto gpu_temp = read_temperature_celsius(*locate_gpu_temp_sensor());
    if (!gpu_temp) {
        return "ERROR: GPU temperature unavailable";
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << *gpu_temp;
    return oss.str();
}

std::string get_cpu_usage()
{
    auto usage = read_cpu_usage_pct();
    if (!usage) {
        return "ERROR: CPU usage unavailable";
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << *usage;
    return oss.str();
}

std::string get_gpu_usage()
{
    auto usage = read_gpu_usage_pct();
    if (!usage) {
        return "ERROR: GPU usage unavailable";
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << *usage;
    return oss.str();
}

void shutdown_fan_controller()
{
    // No background threads to stop anymore
}