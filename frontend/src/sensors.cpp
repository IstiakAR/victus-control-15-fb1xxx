#include "sensors.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {
constexpr int kColumns = 2;
constexpr double kWarmCelsius = 65.0;
constexpr double kHotCelsius = 80.0;

std::string strip_unit(const std::string &value) {
	std::string text = value;
	size_t pos = text.find(" C");
	if (pos != std::string::npos)
		text = text.substr(0, pos);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
		text.pop_back();
	return text;
}
} // namespace

VictusSensorControl::VictusSensorControl(std::shared_ptr<VictusSocketClient> client)
    : socket_client(client)
{
	sensors_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(sensors_page, "page-container");

	// --- Card: Temperatures ---
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_widget_add_css_class(card, "card");

	GtkWidget *title = gtk_label_new("Temperatures");
	gtk_widget_add_css_class(title, "card-title");
	gtk_widget_set_halign(title, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(card), title);

	sensors_grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(sensors_grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(sensors_grid), 12);
	gtk_grid_set_column_homogeneous(GTK_GRID(sensors_grid), TRUE);
	gtk_widget_set_hexpand(sensors_grid, TRUE);
	gtk_box_append(GTK_BOX(card), sensors_grid);

	gtk_box_append(GTK_BOX(sensors_page), card);

	update_temperatures();

	g_timeout_add_seconds(2, [](gpointer data) -> gboolean {
		static_cast<VictusSensorControl *>(data)->update_temperatures();
		return G_SOURCE_CONTINUE;
	}, this);
}

GtkWidget *VictusSensorControl::get_page()
{
	return sensors_page;
}

GtkWidget *VictusSensorControl::create_sensor_row(const std::string &name)
{
	size_t index = sensor_value_labels.size();
	int row = static_cast<int>(index / kColumns);
	int col = static_cast<int>(index % kColumns);

	GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_add_css_class(tile, "sensor-tile");

	GtkWidget *dot = gtk_label_new("\u25cf");
	gtk_widget_add_css_class(dot, "sensor-dot");
	gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(tile), dot);

	GtkWidget *label = gtk_label_new(name.c_str());
	gtk_widget_add_css_class(label, "sensor-name");
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(tile), label);

	GtkWidget *value_label = gtk_label_new("N/A");
	gtk_widget_add_css_class(value_label, "sensor-value");
	gtk_widget_set_halign(value_label, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(tile), value_label);

	gtk_grid_attach(GTK_GRID(sensors_grid), tile, col, row, 1, 1);

	sensor_value_labels[name] = value_label;

	return value_label;
}

void VictusSensorControl::set_sensor_value(GtkWidget *label, const std::string &text)
{
	gtk_label_set_text(GTK_LABEL(label), text.c_str());

	gtk_widget_remove_css_class(label, "cool");
	gtk_widget_remove_css_class(label, "warm");
	gtk_widget_remove_css_class(label, "hot");

	char *end = nullptr;
	double temperature = std::strtod(text.c_str(), &end);
	if (end == text.c_str())
		return;

	if (temperature >= kHotCelsius)
		gtk_widget_add_css_class(label, "hot");
	else if (temperature >= kWarmCelsius)
		gtk_widget_add_css_class(label, "warm");
	else
		gtk_widget_add_css_class(label, "cool");
}

void VictusSensorControl::update_temperatures()
{
	auto response = socket_client->send_command_async(GET_TEMPERATURES);
	std::string result = response.get();

	if (result.find("ERROR") != std::string::npos) {
		if (sensor_value_labels.empty()) {
			create_sensor_row("Temperatures");
		}
		for (auto &entry : sensor_value_labels) {
			set_sensor_value(entry.second, "N/A");
		}
		return;
	}

	std::istringstream stream(result);
	std::string line;
	while (std::getline(stream, line)) {
		if (line.empty())
			continue;

		size_t separator = line.find('=');
		if (separator == std::string::npos)
			continue;

		std::string name = line.substr(0, separator);
		std::string value = line.substr(separator + 1);

		auto it = sensor_value_labels.find(name);
		if (it == sensor_value_labels.end()) {
			it = sensor_value_labels
			         .emplace(name, create_sensor_row(name))
			         .first;
		}

		set_sensor_value(it->second, strip_unit(value));
	}
}