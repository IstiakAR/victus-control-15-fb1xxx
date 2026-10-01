#ifndef SENSORS_HPP
#define SENSORS_HPP

#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <unordered_map>

#include "socket.hpp"

class VictusSensorControl
{
public:
	GtkWidget *sensors_page;

	VictusSensorControl(std::shared_ptr<VictusSocketClient> client);

	GtkWidget *get_page();

private:
	GtkWidget *sensors_grid;
	std::unordered_map<std::string, GtkWidget *> sensor_value_labels;

	std::shared_ptr<VictusSocketClient> socket_client;

	void update_temperatures();
	GtkWidget *create_sensor_row(const std::string &name);
	void set_sensor_value(GtkWidget *label, const std::string &text);
};

#endif // SENSORS_HPP