/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <config.h>

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

static GDBusProxy *dm_proxy, *seat_proxy;

static gint xephyr_display_number;
static GPid xephyr_pid;

static void
usage ()
{
    g_printerr (/* Text printed out when an unknown command-line argument provided */
                _("Run 'dm-tool --help' to see a full list of available command line options."));
    g_printerr ("\n");
}

static void
xephyr_setup_cb (gpointer user_data)
{
    signal (SIGUSR1, SIG_IGN);
}

static void
xephyr_signal_cb (int signum)
{
    gchar *display_number_string, *path;
    GVariantBuilder *properties;
    GVariant *result;
    GError *error = NULL;

    properties = g_variant_builder_new (G_VARIANT_TYPE ("a(ss)"));
    display_number_string = g_strdup_printf ("%d", xephyr_display_number);
    g_variant_builder_add_value (properties, g_variant_new ("(ss)", "xserver-display-number", display_number_string));
    g_free (display_number_string);

    result = g_dbus_proxy_call_sync (dm_proxy,
                                     "AddSeat",
                                     g_variant_new ("(sa(ss))", "xremote", properties),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    g_variant_builder_unref (properties);
    if (!result)
    {
        g_printerr ("Unable to add seat: %s\n", error->message);
        kill (xephyr_pid, SIGQUIT);
        exit (EXIT_FAILURE);
    }

    if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(o)")))
    {
        g_printerr ("Unexpected response to AddSeat: %s\n", g_variant_get_type_string (result));
        exit (EXIT_FAILURE);
    }

    g_variant_get (result, "(&o)", &path);
    g_print ("%s\n", path);

    exit (EXIT_SUCCESS);
}

int
main (int argc, char **argv)
{
    gchar *command;
    gint n_options;
    gchar **options;
    GError *error = NULL;
    gint arg_index;
    GBusType bus_type = G_BUS_TYPE_SYSTEM;

    g_type_init ();

    for (arg_index = 1; arg_index < argc; arg_index++)
    {
        gchar *arg = argv[arg_index];

        if (!g_str_has_prefix (arg, "-"))
            break;
      
        if (strcmp (arg, "-h") == 0 || strcmp (arg, "--help") == 0)
        {
            g_printerr ("Usage:\n"
                        "  dm-tool [OPTION...] COMMAND [ARGS...] - Display Manager tool\n"
                        "\n"
                        "Options:\n"
                        "  -h, --help        Show help options\n"
                        "  -v, --version     Show release version\n"
                        "  --session-bus     Use session D-Bus\n"
                        "\n"
                        "Commands:\n"
                        "  switch-to-greeter                   Switch to the greeter\n"
                        "  switch-to-user USERNAME [SESSION]   Switch to a user session\n"
                        "  switch-to-guest [SESSION]           Switch to a guest session\n"
                        "  add-nested-seat                     Start a nested display\n"
                        "  add-seat TYPE [NAME=VALUE...]       Add a dynamic seat\n");
            return EXIT_SUCCESS;
        }
        else if (strcmp (arg, "-v") == 0 || strcmp (arg, "--version") == 0)
        {
            /* NOTE: Is not translated so can be easily parsed */
            g_printerr ("lightdm %s\n", VERSION);
            return EXIT_SUCCESS;
        }
        else if (strcmp (arg, "--session-bus") == 0)
            bus_type = G_BUS_TYPE_SESSION;
        else
        {
            g_printerr ("Unknown option %s\n", arg);
            usage ();
            return EXIT_FAILURE;
        }
    }

    if (arg_index >= argc)
    {
        g_printerr ("Missing command\n");
        usage ();
        return EXIT_FAILURE;
    }

    dm_proxy = g_dbus_proxy_new_for_bus_sync (bus_type,
                                              G_DBUS_PROXY_FLAGS_NONE,
                                              NULL,
                                              "org.freedesktop.DisplayManager",
                                              "/org/freedesktop/DisplayManager",
                                              "org.freedesktop.DisplayManager",
                                              NULL,
                                              &error);
    if (!dm_proxy)
    {
        g_printerr ("Unable to contact display manager: %s\n", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);

    seat_proxy = g_dbus_proxy_new_for_bus_sync (bus_type,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                NULL,
                                                "org.freedesktop.DisplayManager",
                                                g_getenv ("XDG_SEAT_PATH"),
                                                "org.freedesktop.DisplayManager.Seat",
                                                NULL,
                                                &error);
    if (!seat_proxy)
    {
        g_printerr ("Unable to contact display manager: %s\n", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);

    command = argv[arg_index];
    arg_index++;
    n_options = argc - arg_index;
    options = argv + arg_index;
    if (strcmp (command, "switch-to-greeter") == 0)
    {
        if (n_options != 0)
        {
            g_printerr ("Usage switch-to-greeter\n");
            usage ();
            return EXIT_FAILURE;
        }

        if (!g_dbus_proxy_call_sync (seat_proxy,
                                     "SwitchToGreeter",
                                     g_variant_new ("()"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error))
        {
            g_printerr ("Unable to switch to greeter: %s\n", error->message);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    else if (strcmp (command, "switch-to-user") == 0)
    {
        gchar *username, *session = "";

        if (n_options > 1)
        {
            g_printerr ("Usage switch-to-user USERNAME [SESSION]\n");
            usage ();
            return EXIT_FAILURE;
        }

        username = options[0];
        if (n_options == 2)
            session = options[1];

        if (!g_dbus_proxy_call_sync (seat_proxy,
                                     "SwitchToUser",
                                     g_variant_new ("(ss)", username, session),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error))
        {
            g_printerr ("Unable to switch to user %s: %s\n", username, error->message);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    else if (strcmp (command, "switch-to-guest") == 0)
    {
        gchar *session = "";

        if (n_options > 1)
        {
            g_printerr ("Usage switch-to-guest [SESSION]\n");
            usage ();
            return EXIT_FAILURE;
        }

        if (n_options == 1)
            session = options[0];

        if (!g_dbus_proxy_call_sync (seat_proxy,
                                     "SwitchToGuest",
                                     g_variant_new ("(s)", session),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error))
        {
            g_printerr ("Unable to switch to guest: %s\n", error->message);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    else if (strcmp (command, "add-nested-seat") == 0)
    {
        gchar *path, *xephyr_command, **xephyr_argv;
        GMainLoop *loop;

        path = g_find_program_in_path ("Xephyr");
        if (!path)
        {
            g_printerr ("Unable to find Xephry, please install it\n");
            return EXIT_FAILURE;
        }

        /* Get a unique display number.  It's racy, but the only reliable method to get one */
        xephyr_display_number = 0;
        while (TRUE)
        {
            gchar *lock_name;
            gboolean has_lock;

            lock_name = g_strdup_printf ("/tmp/.X%d-lock", xephyr_display_number);
            has_lock = g_file_test (lock_name, G_FILE_TEST_EXISTS);
            g_free (lock_name);
          
            if (has_lock)
                xephyr_display_number++;
            else
                break;
        }

        /* Wait for signal from Xephyr is ready */
        signal (SIGUSR1, xephyr_signal_cb);

        xephyr_command = g_strdup_printf ("Xephyr :%d", xephyr_display_number);
        if (!g_shell_parse_argv (xephyr_command, NULL, &xephyr_argv, &error) ||
            !g_spawn_async (NULL, xephyr_argv, NULL,
                            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                            xephyr_setup_cb, NULL,
                            &xephyr_pid, &error))
        {
            g_printerr ("Error running Xephyr: %s\n", error->message);
            exit (EXIT_FAILURE);
        }
        g_clear_error (&error);

        /* Block until ready */
        loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (loop);
    }
    else if (strcmp (command, "add-seat") == 0)
    {
        GVariant *result;
        gchar *type, *path;
        GVariantBuilder *properties;
        gint i;

        if (n_options < 1)
        {
            g_printerr ("Usage add-seat TYPE [NAME=VALUE...]\n");
            usage ();
            return EXIT_FAILURE;
        }

        type = options[0];
        properties = g_variant_builder_new (G_VARIANT_TYPE ("a(ss)"));
      
        for (i = 1; i < n_options; i++)
        {
            gchar *property, *name, *value;

            property = g_strdup (options[i]);
            name = property;
            value = strchr (property, '=');
            if (value)
            {
                *value = '\0';
                value++;
            }
            else
               value = "";

            g_variant_builder_add_value (properties, g_variant_new ("(ss)", name, value));
            g_free (property);
        }

        result = g_dbus_proxy_call_sync (dm_proxy,
                                         "AddSeat",
                                         g_variant_new ("(sa(ss))", type, properties),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);
        g_variant_builder_unref (properties);
        if (!result)
        {
            g_printerr ("Unable to add seat: %s\n", error->message);
            return EXIT_FAILURE;
        }

        if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(o)")))
        {
            g_printerr ("Unexpected response to AddSeat: %s\n", g_variant_get_type_string (result));
            return EXIT_FAILURE;
        }

        g_variant_get (result, "(&o)", &path);
        g_print ("%s\n", path);

        return EXIT_SUCCESS;
    }

    g_printerr ("Unknown command %s\n", command);
    usage ();
    return EXIT_FAILURE;
}
