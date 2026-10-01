#include "sensors.hpp"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SensorEntry {
  std::string name;
  double celsius;
};

std::string trim(const std::string &input) {
  size_t start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";

  size_t end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
}

bool read_first_line(const std::string &path, std::string *value) {
  std::ifstream file(path);
  if (!file)
    return false;

  std::string line;
  if (!std::getline(file, line))
    return false;

  *value = trim(line);
  return true;
}

bool read_millidegrees(const std::string &path, double *celsius) {
  std::ifstream file(path);
  if (!file)
    return false;

  long value = 0;
  file >> value;
  if (file.fail())
    return false;

  *celsius = static_cast<double>(value) / 1000.0;
  return true;
}

std::vector<SensorEntry> scan_hwmon() {
  std::vector<SensorEntry> sensors;
  std::vector<std::string> seen_names;

  DIR *dir = opendir("/sys/class/hwmon");
  if (!dir)
    return sensors;

  std::vector<std::string> hwmons;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (std::strncmp(entry->d_name, "hwmon", 5) == 0) {
      hwmons.push_back(std::string("/sys/class/hwmon/") + entry->d_name);
    }
  }
  closedir(dir);

  std::sort(hwmons.begin(), hwmons.end());

  for (const auto &base : hwmons) {
    std::string driver;
    read_first_line(base + "/name", &driver);

    std::vector<std::string> inputs;
    DIR *inner = opendir(base.c_str());
    if (!inner)
      continue;

    struct dirent *inner_entry;
    while ((inner_entry = readdir(inner)) != nullptr) {
      std::string file_name = inner_entry->d_name;
      if (file_name.rfind("temp", 0) != 0)
        continue;
      if (file_name.size() < 11)
        continue;
      if (file_name.compare(file_name.size() - 6, 6, "_input") != 0)
        continue;
      inputs.push_back(file_name);
    }
    closedir(inner);

    std::sort(inputs.begin(), inputs.end());

    for (const auto &file_name : inputs) {
      std::string prefix = file_name.substr(0, file_name.size() - 6);

      std::string label;
      std::string name;
      if (read_first_line(base + "/" + prefix + "_label", &label) &&
          !label.empty()) {
        name = label;
      } else {
        std::string index = prefix.substr(4);
        name = driver.empty() ? ("temp" + index) : (driver + " temp" + index);
      }

      if (std::find(seen_names.begin(), seen_names.end(), name) !=
          seen_names.end()) {
        continue;
      }

      double celsius = 0.0;
      if (!read_millidegrees(base + "/" + file_name, &celsius))
        continue;

      seen_names.push_back(name);
      sensors.push_back(SensorEntry{name, celsius});
    }
  }

  return sensors;
}

std::vector<SensorEntry> scan_thermal_zones() {
  std::vector<SensorEntry> sensors;
  std::vector<std::string> seen_names;

  DIR *dir = opendir("/sys/class/thermal");
  if (!dir)
    return sensors;

  std::vector<std::string> zones;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (std::strncmp(entry->d_name, "thermal_zone", 12) == 0) {
      zones.push_back(std::string("/sys/class/thermal/") + entry->d_name);
    }
  }
  closedir(dir);

  std::sort(zones.begin(), zones.end());

  for (const auto &base : zones) {
    std::string type;
    if (!read_first_line(base + "/type", &type) || type.empty())
      continue;

    if (std::find(seen_names.begin(), seen_names.end(), type) !=
        seen_names.end()) {
      continue;
    }

    double celsius = 0.0;
    if (!read_millidegrees(base + "/temp", &celsius))
      continue;

    seen_names.push_back(type);
    sensors.push_back(SensorEntry{type, celsius});
  }

  return sensors;
}

} // namespace

std::string get_temperatures() {
  std::vector<SensorEntry> sensors = scan_hwmon();
  if (sensors.empty()) {
    sensors = scan_thermal_zones();
  }

  if (sensors.empty()) {
    return "ERROR: No temperature sensors found";
  }

  std::ostringstream oss;
  for (size_t i = 0; i < sensors.size(); ++i) {
    if (i > 0)
      oss << "\n";
    oss << sensors[i].name << "=" << std::fixed << std::setprecision(1)
        << sensors[i].celsius << " C";
  }
  return oss.str();
}