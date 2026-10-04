// SPDX-License-Identifier: GPL-2.0-or-later
// A small GTK 3 program (unmodified toolkit, Wayland backend): a label and a button; Enter/Space on the button prints.
#include <gtk/gtk.h>
#include <stdio.h>

static void clicked(GtkWidget *w, gpointer d) { (void)w; (void)d; printf("gtk: clicked\n"); fflush(stdout); }
static void menu_hit(GtkWidget *w, gpointer d) { (void)w; (void)d; printf("gtk: menu activated\n"); fflush(stdout); }
static gboolean quit_later(gpointer d) { (void)d; printf("gtk: bye\n"); fflush(stdout); gtk_main_quit(); return FALSE; }

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "gtkhello");
    gtk_window_set_default_size(GTK_WINDOW(win), 360, 200);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *label = gtk_label_new("Hallo von GTK auf THOS");
    GtkWidget *btn = gtk_button_new_with_label("Klick mich");
    g_signal_connect(btn, "clicked", G_CALLBACK(clicked), NULL);
    GtkWidget *bar = gtk_menu_bar_new(), *file = gtk_menu_item_new_with_label("Datei"), *menu = gtk_menu_new();
    GtkWidget *item = gtk_menu_item_new_with_label("Eintrag A"), *item2 = gtk_menu_item_new_with_label("Eintrag B");
    g_signal_connect(item, "activate", G_CALLBACK(menu_hit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item2);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), file);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 8);
    gtk_box_pack_start(GTK_BOX(box), btn, TRUE, TRUE, 8);
    gtk_container_add(GTK_CONTAINER(win), box);
    gtk_widget_show_all(win);
    gtk_widget_grab_focus(btn);
    printf("gtk: shown\n"); fflush(stdout);
    g_timeout_add_seconds(argc > 1 ? atoi(argv[1]) : 60, quit_later, NULL);
    gtk_main();
    return 0;
}
