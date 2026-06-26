#ifndef FAN_HPP
#define FAN_HPP

#include <gtk/gtk.h>
#include <string>
#include "socket.hpp"

class VictusFanControl
{
public:
	GtkWidget *fan_page;

	VictusFanControl(std::shared_ptr<VictusSocketClient> client);

	GtkWidget *get_page();

private:
    // Buttons
	GtkWidget *auto_btn;
	GtkWidget *max_btn;

    // Labels for displaying current state
	GtkWidget *state_label;
	GtkWidget *fan1_speed_label;
	GtkWidget *fan2_speed_label;

	void set_mode(const std::string &mode);
	void update_fan_speeds();
	void update_ui_from_system_state();

    // Signal handlers
	static void on_auto_clicked(GtkButton *button, gpointer data);
	static void on_max_clicked(GtkButton *button, gpointer data);

	std::shared_ptr<VictusSocketClient> socket_client;
};

#endif // FAN_HPP