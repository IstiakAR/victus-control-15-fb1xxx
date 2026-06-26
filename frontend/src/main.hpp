#include <gtk/gtk.h>

#include <memory>

#include "fan.hpp"
#include "keyboard.hpp"
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

	VictusControl();
	~VictusControl();

	void build_ui();
	void run();
};
