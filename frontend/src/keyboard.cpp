#include "keyboard.hpp"
#include <gtk/gtk.h>
#include <iostream>
#include <sstream>

VictusKeyboardControl::VictusKeyboardControl(
    std::shared_ptr<VictusSocketClient> client)
    : socket_client(client) {
  keyboard_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(keyboard_page, "page-container");

  // --- Card ---
  GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_add_css_class(card, "card");

  // Card title
  GtkWidget *title = gtk_label_new("Keyboard Backlight");
  gtk_widget_add_css_class(title, "card-title");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(card), title);

  // Info note
  GtkWidget *note_label = gtk_label_new(nullptr);
  gtk_label_set_markup(GTK_LABEL(note_label),
    "<span size='12000'>The keyboard backlight cannot be controlled through software on this system.</span>");
  gtk_widget_add_css_class(note_label, "status-label");
  gtk_widget_set_halign(note_label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(note_label), TRUE);
  gtk_box_append(GTK_BOX(card), note_label);

  // Fn+F4 hint
  GtkWidget *hint_label = gtk_label_new(nullptr);
  gtk_label_set_markup(GTK_LABEL(hint_label),
    "<span size='16000' weight='bold'>Press Fn + F4</span>\n"
    "<span size='11000'>to toggle the keyboard backlight</span>");
  gtk_widget_add_css_class(hint_label, "status-value");
  gtk_widget_set_halign(hint_label, GTK_ALIGN_CENTER);
  gtk_label_set_justify(GTK_LABEL(hint_label), GTK_JUSTIFY_CENTER);
  gtk_box_append(GTK_BOX(card), hint_label);

  gtk_box_append(GTK_BOX(keyboard_page), card);
}

GtkWidget *VictusKeyboardControl::get_page() { return keyboard_page; }