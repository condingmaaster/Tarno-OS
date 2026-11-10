// SPDX-License-Identifier: GPL-2.0-or-later
// Real GTK3 event routing, including its default client-side decoration.
#include <gtk/gtk.h>
#include <stdio.h>
static gboolean timeout(gpointer unused){(void)unused;puts("gtk-input-timeout");fflush(stdout);gtk_main_quit();return G_SOURCE_REMOVE;}
static void closed(GtkWidget*w,gpointer unused){(void)w;(void)unused;if(gtk_main_level())gtk_main_quit();}
static gboolean stop(gpointer unused){(void)unused;gtk_main_quit();return G_SOURCE_REMOVE;}
static gboolean pressed(GtkWidget*w,GdkEventButton*event,gpointer unused){(void)w;(void)unused;printf("gtk-input-press button=%u x=%.1f y=%.1f\n",event->button,event->x,event->y);fflush(stdout);return FALSE;}
static void clicked(GtkButton*b,gpointer unused){(void)unused;gtk_button_set_label(b,"GTK click received");puts("gtk-input-clicked");fflush(stdout);g_timeout_add(600,stop,NULL);}
int main(int argc,char**argv){
 gtk_init(&argc,&argv);
 GtkWidget*w=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(w),"THOS GTK input check");gtk_window_set_default_size(GTK_WINDOW(w),420,240);
 GtkWidget*b=gtk_button_new_with_label("Click to verify real GTK input");gtk_container_add(GTK_CONTAINER(w),b);
 GtkCssProvider*css=gtk_css_provider_new();GError*error=NULL;
 gtk_css_provider_load_from_data(css,"button,button:hover,button:active{background-image:none;background-color:rgb(19,177,221);color:black;border:0;box-shadow:none;}",-1,&error);
 if(error){fprintf(stderr,"gtk-input-css: %s\n",error->message);g_error_free(error);return 2;}
 gtk_style_context_add_provider(gtk_widget_get_style_context(b),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);g_object_unref(css);
 g_signal_connect(b,"button-press-event",G_CALLBACK(pressed),NULL);g_signal_connect(b,"clicked",G_CALLBACK(clicked),NULL);g_signal_connect(w,"destroy",G_CALLBACK(closed),NULL);
 gtk_widget_show_all(w);puts("gtk-input-ready");fflush(stdout);g_timeout_add_seconds(45,timeout,NULL);gtk_main();gtk_widget_destroy(w);puts("gtk-input-exit");return 0;
}
