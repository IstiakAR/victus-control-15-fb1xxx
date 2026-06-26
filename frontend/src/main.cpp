#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <gtk/gtk.h>
#include "keyboard.hpp"
#include "fan.hpp"
#include "socket.hpp"

class VictusControl
{
public:
	GtkWidget *window;
	GtkWidget *main_box;
	GtkWidget *content_area;

	std::shared_ptr<VictusSocketClient> socket_client;
	std::unique_ptr<VictusFanControl> fan_control;
	std::unique_ptr<VictusKeyboardControl> keyboard_control;

	VictusControl()
	{
		load_css();

		socket_client = std::make_shared<VictusSocketClient>("/run/victus-control/victus_backend.sock");
		fan_control = std::make_unique<VictusFanControl>(socket_client);
		keyboard_control = std::make_unique<VictusKeyboardControl>(socket_client);

		window = gtk_window_new();
		gtk_window_set_title(GTK_WINDOW(window), "victus-control");
		gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);
		gtk_widget_add_css_class(window, "app-window");

		// Main vertical layout
		main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_window_set_child(GTK_WINDOW(window), main_box);

		// Custom title bar
		GtkWidget *title_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_add_css_class(title_bar, "title-bar");

		// Title label
		GtkWidget *title_label = gtk_label_new("victus-control");
		gtk_widget_add_css_class(title_label, "title-label");
		gtk_widget_set_hexpand(title_label, TRUE);
		gtk_widget_set_halign(title_label, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(title_bar), title_label);

		// Close button
		GtkWidget *close_btn = gtk_button_new();
		gtk_button_set_icon_name(GTK_BUTTON(close_btn), "window-close-symbolic");
		gtk_widget_add_css_class(close_btn, "victus-title-btn");
		g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_window_destroy), window);
		gtk_box_append(GTK_BOX(title_bar), close_btn);

		gtk_window_set_titlebar(GTK_WINDOW(window), title_bar);

		// Content area (scrollable)
		GtkWidget *scrolled = gtk_scrolled_window_new();
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
		                                GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_widget_set_vexpand(scrolled, TRUE);

		content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_add_css_class(content_area, "main-box");
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), content_area);

		gtk_box_append(GTK_BOX(main_box), scrolled);

		// Build the UI content
		build_ui();
	}

	~VictusControl()
	{
	}

	void load_css()
	{
		GtkCssProvider *provider = gtk_css_provider_new();
		const char *css =
			"/* Dark theme */\n"
			"\n"
			"* {\n"
			"  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif;\n"
			"}\n"
			"\n"
			".app-window {\n"
			"  background-color: #0d1117;\n"
			"}\n"
			"\n"
			".main-box {\n"
			"  background-color: #0d1117;\n"
			"  padding: 24px 32px;\n"
			"}\n"
			"\n"
			"/* Title bar */\n"
			".title-bar {\n"
			"  background-color: #0d1117;\n"
			"  border-bottom: 1px solid #21262d;\n"
			"  padding: 6px 12px;\n"
			"  min-height: 36px;\n"
			"}\n"
			"\n"
			".title-label {\n"
			"  color: #c9d1d9;\n"
			"  font-weight: 700;\n"
			"  font-size: 14px;\n"
			"  padding: 0 8px;\n"
			"}\n"
			"\n"
			"victus-title-btn {\n"
			"  background-color: #21262d !important;\n"
			"  border: none;\n"
			"  border-radius: 6px;\n"
			"  padding: 6px;\n"
			"  min-width: 32px;\n"
			"  min-height: 32px;\n"
			"  color: #8b949e;\n"
			"}\n"
			"\n"
			"victus-title-btn:hover {\n"
			"  background-color: #30363d !important;\n"
			"  color: #c9d1d9;\n"
			"}\n"
			"\n"
			"victus-title-btn.close-btn:hover {\n"
			"  background-color: #f85149 !important;\n"
			"  color: #ffffff;\n"
			"}\n"
			"\n"
			"/* Card container */\n"
			".card {\n"
			"  background-color: #161b22;\n"
			"  border: 1px solid #21262d;\n"
			"  border-radius: 8px;\n"
			"  padding: 20px 24px;\n"
			"  margin: 8px 0;\n"
			"}\n"
			"\n"
			"/* Card title */\n"
			".card-title {\n"
			"  color: #c9d1d9;\n"
			"  font-size: 14px;\n"
			"  font-weight: 700;\n"
			"  margin-bottom: 16px;\n"
			"  letter-spacing: 0.3px;\n"
			"}\n"
			"\n"
			"/* Fan mode buttons */\n"
			"button {\n"
			"  min-height: 44px;\n"
			"  border-radius: 8px;\n"
			"  font-size: 15px;\n"
			"  font-weight: 700;\n"
			"  padding: 10px 24px;\n"
			"  border: 2px solid #30363d !important;\n"
			"  transition: all 150ms ease;\n"
			"  background-color: #21262d !important;\n"
			"  color: #ffffff !important;\n"
			"}\n"
			"\n"
			"button.active {\n"
			"  background-color: #1f6feb !important;\n"
			"  color: #ffffff !important;\n"
			"  border-color: #1f6feb !important;\n"
			"}\n"
			"\n"
			"button.inactive {\n"
			"  background-color: #21262d !important;\n"
			"  color: #8b949e !important;\n"
			"  border-color: #30363d !important;\n"
			"}\n"
			"\n"
			"button.inactive:hover {\n"
			"  border-color: #58a6ff !important;\n"
			"  color: #c9d1d9 !important;\n"
			"}\n"
			"\n"
			"/* Status labels */\n"
			".status-label {\n"
			"  color: #8b949e;\n"
			"  font-size: 13px;\n"
			"}\n"
			"\n"
			".status-value {\n"
			"  color: #c9d1d9;\n"
			"  font-weight: 600;\n"
			"  font-size: 14px;\n"
			"}\n"
			"\n"
			".status-value.on {\n"
			"  color: #3fb950;\n"
			"}\n"
			"\n"
			".status-value.off {\n"
			"  color: #f85149;\n"
			"}\n"
			"\n"
			".status-value.max {\n"
			"  color: #d29922;\n"
			"}\n"
			"\n"
			".status-value.auto {\n"
			"  color: #58a6ff;\n"
			"}\n"
			"\n"
			"/* Fan speed display */\n"
			".fan-speed {\n"
			"  background-color: #0d1117;\n"
			"  border: 1px solid #21262d;\n"
			"  border-radius: 6px;\n"
			"  padding: 12px 16px;\n"
			"  margin: 4px 0;\n"
			"}\n"
			"\n"
			".fan-speed-label {\n"
			"  color: #8b949e;\n"
			"  font-size: 12px;\n"
			"  font-weight: 500;\n"
			"}\n"
			"\n"
			".fan-speed-value {\n"
			"  color: #c9d1d9;\n"
			"  font-size: 18px;\n"
			"  font-weight: 700;\n"
			"  font-family: monospace;\n"
			"}\n"
			"";

		gtk_css_provider_load_from_string(provider, css);
		gtk_style_context_add_provider_for_display(
			gdk_display_get_default(),
			GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(provider);
	}

	void build_ui()
	{
		// Keyboard section
		GtkWidget *keyboard_page = keyboard_control->get_page();
		gtk_box_append(GTK_BOX(content_area), keyboard_page);

		// Fan section
		GtkWidget *fan_page = fan_control->get_page();
		gtk_box_append(GTK_BOX(content_area), fan_page);
	}

	void run()
	{
		GMainLoop *loop = g_main_loop_new(nullptr, FALSE);

		g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer loop)
		{
			g_main_loop_quit(static_cast<GMainLoop *>(loop));
		}), loop);

		gtk_widget_set_visible(window, true);

		g_main_loop_run(loop);

		g_main_loop_unref(loop);
	}
};

int main(int argc, char *argv[])
{
	gtk_init();

	try {
		VictusControl app;
		app.run();
	} catch (const std::exception &e) {
		std::cerr << "An unhandled exception occurred: " << e.what() << std::endl;

		GtkAlertDialog *dialog = gtk_alert_dialog_new(
			"An error occurred: %s",
			e.what()
		);
		gtk_alert_dialog_set_detail(dialog, "victus-control encountered an unexpected error and must close.");
		gtk_alert_dialog_show(dialog, nullptr);
		return 1;
	}


	return 0;
}