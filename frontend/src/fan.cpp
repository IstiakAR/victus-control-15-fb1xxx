#include "fan.hpp"
#include "socket.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

VictusFanControl::VictusFanControl(std::shared_ptr<VictusSocketClient> client) : socket_client(client)
{
    fan_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(fan_page, "page-container");

    // --- Card 1: Mode Selector ---
    GtkWidget *mode_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(mode_card, "card");

    GtkWidget *mode_title = gtk_label_new("Fan Mode");
    gtk_widget_add_css_class(mode_title, "card-title");
    gtk_widget_set_halign(mode_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(mode_card), mode_title);

    // Button row: side by side, equal width
    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_set_homogeneous(GTK_BOX(btn_row), TRUE);
    gtk_widget_set_hexpand(btn_row, TRUE);

    // Auto button
    auto_btn = gtk_button_new_with_label("AUTO");
    gtk_widget_add_css_class(auto_btn, "active");
    g_signal_connect(auto_btn, "clicked", G_CALLBACK(on_auto_clicked), this);
    gtk_box_append(GTK_BOX(btn_row), auto_btn);

    // Max button
    max_btn = gtk_button_new_with_label("MAX");
    gtk_widget_add_css_class(max_btn, "inactive");
    g_signal_connect(max_btn, "clicked", G_CALLBACK(on_max_clicked), this);
    gtk_box_append(GTK_BOX(btn_row), max_btn);
    gtk_box_append(GTK_BOX(mode_card), btn_row);

    // --- Status row ---
    GtkWidget *mode_status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(mode_status_row, GTK_ALIGN_START);

    GtkWidget *mode_status_label = gtk_label_new("Current Mode:");
    gtk_widget_add_css_class(mode_status_label, "status-label");

    state_label = gtk_label_new("---");
    gtk_widget_add_css_class(state_label, "status-value");
    gtk_widget_add_css_class(state_label, "auto");

    gtk_box_append(GTK_BOX(mode_status_row), mode_status_label);
    gtk_box_append(GTK_BOX(mode_status_row), state_label);
    gtk_box_append(GTK_BOX(mode_card), mode_status_row);

    gtk_box_append(GTK_BOX(fan_page), mode_card);

    // --- Card 2: Fan Speeds ---
    GtkWidget *speed_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(speed_card, "card");

    GtkWidget *speed_title = gtk_label_new("Fan Speeds");
    gtk_widget_add_css_class(speed_title, "card-title");
    gtk_widget_set_halign(speed_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(speed_card), speed_title);

    // Fan 1 speed
    GtkWidget *fan1_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(fan1_box, "fan-speed");

    GtkWidget *fan1_label = gtk_label_new("Fan 1");
    gtk_widget_add_css_class(fan1_label, "fan-speed-label");
    gtk_widget_set_halign(fan1_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(fan1_box), fan1_label);

    fan1_speed_label = gtk_label_new("N/A RPM");
    gtk_widget_add_css_class(fan1_speed_label, "fan-speed-value");
    gtk_widget_set_halign(fan1_speed_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(fan1_box), fan1_speed_label);

    gtk_box_append(GTK_BOX(speed_card), fan1_box);

    // Fan 2 speed
    GtkWidget *fan2_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(fan2_box, "fan-speed");

    GtkWidget *fan2_label = gtk_label_new("Fan 2");
    gtk_widget_add_css_class(fan2_label, "fan-speed-label");
    gtk_widget_set_halign(fan2_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(fan2_box), fan2_label);

    fan2_speed_label = gtk_label_new("N/A RPM");
    gtk_widget_add_css_class(fan2_speed_label, "fan-speed-value");
    gtk_widget_set_halign(fan2_speed_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(fan2_box), fan2_speed_label);

    gtk_box_append(GTK_BOX(speed_card), fan2_box);

    gtk_box_append(GTK_BOX(fan_page), speed_card);

    update_fan_speeds();

    // Set up a timer to periodically update fan speeds
    g_timeout_add_seconds(2, [](gpointer data) -> gboolean {
        static_cast<VictusFanControl*>(data)->update_fan_speeds();
        return G_SOURCE_CONTINUE;
    }, this);
}

GtkWidget* VictusFanControl::get_page()
{
    return fan_page;
}

void VictusFanControl::set_mode(const std::string &mode)
{
    auto result = socket_client->send_command_async(SET_FAN_MODE, mode).get();
    if (result != "OK") {
        std::cerr << "Failed to set fan mode: " << result << std::endl;
    }
    update_ui_from_system_state();
}

void VictusFanControl::update_ui_from_system_state()
{
    auto response = socket_client->send_command_async(GET_FAN_MODE);
    std::string fan_mode = response.get();

    if (fan_mode.find("ERROR") != std::string::npos) {
        fan_mode = "AUTO";
        std::cerr << "Failed to get fan mode, defaulting to AUTO." << std::endl;
    }


    // Update state label
    gtk_label_set_text(GTK_LABEL(state_label), fan_mode.c_str());

    // Remove all mode classes
    gtk_widget_remove_css_class(state_label, "auto");
    gtk_widget_remove_css_class(state_label, "max");
    gtk_widget_remove_css_class(state_label, "on");
    gtk_widget_remove_css_class(state_label, "off");

    // Update button states
    if (fan_mode == "MAX") {
        gtk_widget_add_css_class(auto_btn, "inactive");
        gtk_widget_remove_css_class(auto_btn, "active");
        gtk_widget_add_css_class(max_btn, "active");
        gtk_widget_remove_css_class(max_btn, "inactive");
        gtk_widget_add_css_class(state_label, "max");
    } else { // AUTO
        gtk_widget_add_css_class(auto_btn, "active");
        gtk_widget_remove_css_class(auto_btn, "inactive");
        gtk_widget_add_css_class(max_btn, "inactive");
        gtk_widget_remove_css_class(max_btn, "active");
        gtk_widget_add_css_class(state_label, "auto");
    }
}

void VictusFanControl::update_fan_speeds()
{
    auto response1 = socket_client->send_command_async(GET_FAN_SPEED, "1");
    std::string fan1_speed = response1.get();
    if (fan1_speed.find("ERROR") != std::string::npos) fan1_speed = "N/A";

    auto response2 = socket_client->send_command_async(GET_FAN_SPEED, "2");
    std::string fan2_speed = response2.get();
    if (fan2_speed.find("ERROR") != std::string::npos) fan2_speed = "N/A";

    gtk_label_set_text(GTK_LABEL(fan1_speed_label), (fan1_speed + " RPM").c_str());
    gtk_label_set_text(GTK_LABEL(fan2_speed_label), (fan2_speed + " RPM").c_str());
}

void VictusFanControl::on_auto_clicked(GtkButton *button, gpointer data)
{
    static_cast<VictusFanControl*>(data)->set_mode("AUTO");
}

void VictusFanControl::on_max_clicked(GtkButton *button, gpointer data)
{
    static_cast<VictusFanControl*>(data)->set_mode("MAX");
}

