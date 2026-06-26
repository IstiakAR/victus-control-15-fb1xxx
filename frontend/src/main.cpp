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

	bool is_dark_theme = false;
	GtkCssProvider *css_provider = nullptr;

	VictusControl()
	{
		load_theme_preference();
		css_provider = gtk_css_provider_new();
		load_css(is_dark_theme);

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

		// Theme toggle button (left side)
		GtkWidget *theme_btn = gtk_button_new();
		gtk_button_set_has_frame(GTK_BUTTON(theme_btn), FALSE);
		gtk_widget_set_size_request(theme_btn, 40, 40);
		gtk_button_set_label(GTK_BUTTON(theme_btn), "☀");
		gtk_widget_add_css_class(theme_btn, "victus-title-btn");
		g_signal_connect(theme_btn, "realize", G_CALLBACK(+[](GtkWidget *w, gpointer)
		{
			GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(w));
			if (surface) gdk_surface_set_cursor(surface, gdk_cursor_new_from_name("pointer", nullptr));
		}), nullptr);
		g_signal_connect(theme_btn, "clicked", G_CALLBACK(+[](GtkWidget *btn, gpointer data)
		{
			VictusControl *self = static_cast<VictusControl*>(data);
			self->is_dark_theme = !self->is_dark_theme;
			gtk_button_set_label(GTK_BUTTON(btn), self->is_dark_theme ? "☾" : "☀");
			self->load_css(self->is_dark_theme);
			self->save_theme_preference();
		}), this);
		gtk_box_append(GTK_BOX(title_bar), theme_btn);

		// Title label (centered)
		GtkWidget *title_label = gtk_label_new("victus-control");
		gtk_widget_add_css_class(title_label, "title-label");
		gtk_widget_set_hexpand(title_label, TRUE);
		gtk_widget_set_halign(title_label, GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(title_bar), title_label);

		// Close button (right side)
		GtkWidget *close_btn = gtk_button_new();
		gtk_widget_set_size_request(close_btn, 32, 32);
		gtk_button_set_icon_name(GTK_BUTTON(close_btn), "window-close-symbolic");
		gtk_widget_add_css_class(close_btn, "victus-title-btn");
		g_signal_connect(close_btn, "realize", G_CALLBACK(+[](GtkWidget *w, gpointer)
		{
			GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(w));
			if (surface) gdk_surface_set_cursor(surface, gdk_cursor_new_from_name("pointer", nullptr));
		}), nullptr);
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
		if (css_provider) g_object_unref(css_provider);
	}

	void load_css(bool dark)
	{
		if (!css_provider) css_provider = gtk_css_provider_new();

		const char *light_css =
			"* { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif; }\n"
			".app-window { background-color: #ffffff; }\n"
			".main-box { background-color: #ffffff; padding: 24px 32px; }\n"
			".title-bar { background-color: #ffffff; border-bottom: 1px solid #d0d7de; padding: 6px 12px; min-height: 36px; }\n"
			".title-label { color: #1f2328; font-weight: 700; font-size: 14px; padding: 0 8px; }\n"
			"victus-title-btn { cursor: pointer; background-color: #f6f8fa !important; border: none; border-radius: 6px; padding: 6px; min-width: 32px; min-height: 32px; color: #ffffff; font-size: 18px; }\n"
			"victus-title-btn:hover { background-color: #eaeef2 !important; color: #ffffff; }\n"
			"victus-title-btn.close-btn:hover { background-color: #cf222e !important; color: #ffffff; }\n"
			".card { background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 8px; padding: 20px 24px; margin: 8px 0; }\n"
			".card-title { color: #1f2328; font-size: 14px; font-weight: 700; margin-bottom: 16px; letter-spacing: 0.3px; }\n"
			"button { cursor: pointer; min-height: 44px; border-radius: 8px; font-size: 15px; font-weight: 700; padding: 10px 24px; border: 2px solid #d0d7de !important; transition: all 150ms ease; background-color: #ffffff !important; color: #1f2328 !important; }\n"
			"button.active { background-color: #0969da !important; color: #ffffff !important; border-color: #0969da !important; }\n"
			"button.inactive { background-color: #f6f8fa !important; color: #656d76 !important; border-color: #d0d7de !important; }\n"
			"button.inactive:hover { border-color: #0969da !important; color: #1f2328 !important; }\n"
			".status-label { color: #656d76; font-size: 13px; }\n"
			".status-value { color: #1f2328; font-weight: 600; font-size: 14px; }\n"
			".status-value.on { color: #1a7f37; }\n"
			".status-value.off { color: #cf222e; }\n"
			".status-value.max { color: #9a6700; }\n"
			".status-value.auto { color: #0969da; }\n"
			".fan-speed { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px; padding: 12px 16px; margin: 4px 0; }\n"
			".fan-speed-label { color: #656d76; font-size: 12px; font-weight: 500; }\n"
			".fan-speed-value { color: #1f2328; font-size: 18px; font-weight: 700; font-family: monospace; }\n";

		const char *dark_css =
			"* { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif; }\n"
			".app-window { background-color: #0d1117; }\n"
			".main-box { background-color: #0d1117; padding: 24px 32px; }\n"
			".title-bar { background-color: #0d1117; border-bottom: 1px solid #21262d; padding: 6px 12px; min-height: 36px; }\n"
			".title-label { color: #c9d1d9; font-weight: 700; font-size: 14px; padding: 0 8px; }\n"
			"victus-title-btn { cursor: pointer; background-color: #21262d !important; border: none; border-radius: 6px; padding: 6px; min-width: 32px; min-height: 32px; color: #ffffff; font-size: 18px; }\n"
			"victus-title-btn:hover { background-color: #30363d !important; color: #ffffff; }\n"
			"victus-title-btn.close-btn:hover { background-color: #f85149 !important; color: #ffffff; }\n"
			".card { background-color: #161b22; border: 1px solid #21262d; border-radius: 8px; padding: 20px 24px; margin: 8px 0; }\n"
			".card-title { color: #c9d1d9; font-size: 14px; font-weight: 700; margin-bottom: 16px; letter-spacing: 0.3px; }\n"
			"button { cursor: pointer; min-height: 44px; border-radius: 8px; font-size: 15px; font-weight: 700; padding: 10px 24px; border: 2px solid #30363d !important; transition: all 150ms ease; background-color: #21262d !important; color: #ffffff !important; }\n"
			"button.active { background-color: #1f6feb !important; color: #ffffff !important; border-color: #1f6feb !important; }\n"
			"button.inactive { background-color: #21262d !important; color: #8b949e !important; border-color: #30363d !important; }\n"
			"button.inactive:hover { border-color: #58a6ff !important; color: #c9d1d9 !important; }\n"
			".status-label { color: #8b949e; font-size: 13px; }\n"
			".status-value { color: #c9d1d9; font-weight: 600; font-size: 14px; }\n"
			".status-value.on { color: #3fb950; }\n"
			".status-value.off { color: #f85149; }\n"
			".status-value.max { color: #d29922; }\n"
			".status-value.auto { color: #58a6ff; }\n"
			".fan-speed { background-color: #0d1117; border: 1px solid #21262d; border-radius: 6px; padding: 12px 16px; margin: 4px 0; }\n"
			".fan-speed-label { color: #8b949e; font-size: 12px; font-weight: 500; }\n"
			".fan-speed-value { color: #c9d1d9; font-size: 18px; font-weight: 700; font-family: monospace; }\n";

		const char *css = dark ? dark_css : light_css;
		gtk_css_provider_load_from_string(css_provider, css);
		GdkDisplay *display = gdk_display_get_default();
		gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}

	void save_theme_preference()
	{
		const char *home = g_get_home_dir();
		if (!home) return;
		std::string dir = std::string(home) + "/.config/victus-control";
		g_mkdir_with_parents(dir.c_str(), 0700);
		std::ofstream file(dir + "/settings");
		if (file) file << (is_dark_theme ? "dark" : "light") << std::endl;
	}

	void load_theme_preference()
	{
		const char *home = g_get_home_dir();
		if (!home) return;
		std::string path = std::string(home) + "/.config/victus-control/settings";
		std::ifstream file(path);
		std::string mode;
		if (file && std::getline(file, mode)) {
			is_dark_theme = (mode == "dark");
		}
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
		std::cerr << "An error occurred: " << e.what() << std::endl;
		GtkAlertDialog *dialog = gtk_alert_dialog_new("An error occurred: %s", e.what());
		gtk_alert_dialog_set_detail(dialog, "victus-control encountered an unexpected error and must close.");
		gtk_alert_dialog_show(dialog, nullptr);
		return 1;
	}

	return 0;
}