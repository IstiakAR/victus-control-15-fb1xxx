#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

#include <gtk/gtk.h>
#include <string>
#include "socket.hpp"

class VictusKeyboardControl
{
public:
  GtkWidget *keyboard_page;

  VictusKeyboardControl(std::shared_ptr<VictusSocketClient> client);

  GtkWidget *get_page();

private:
  std::shared_ptr<VictusSocketClient> socket_client;
};

#endif