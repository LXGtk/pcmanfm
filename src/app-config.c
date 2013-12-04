/*
 *      app-config.c
 *
 *      Copyright 2010 PCMan <pcman.tw@gmail.com>
 *      Copyright 2012-2013 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "app-config.h"

#include <libfm/fm-gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tab-page.h"

#if !FM_CHECK_VERSION(1, 2, 0)
typedef struct
{
    GKeyFile *kf;
    char *group; /* allocated if not in cache */
    char *filepath; /* NULL if in cache */
    gboolean changed;
} FmFolderConfig;

static GKeyFile *fc_cache = NULL;

static gboolean dir_cache_changed = FALSE;

static FmFolderConfig *fm_folder_config_open(FmPath *path)
{
    FmFolderConfig *fc = g_slice_new(FmFolderConfig);
    FmPath *sub_path;

    fc->changed = FALSE;
    /* clear .directory file first */
    sub_path = fm_path_new_child(path, ".directory");
    fc->filepath = fm_path_to_str(sub_path);
    fm_path_unref(sub_path);
    if (g_file_test(fc->filepath, G_FILE_TEST_EXISTS))
    {
        fc->kf = g_key_file_new();
        if (g_key_file_load_from_file(fc->kf, fc->filepath,
                                      G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                      NULL) &&
            g_key_file_has_group(fc->kf, "File Manager"))
        {
            fc->group = "File Manager";
            return fc;
        }
        g_key_file_free(fc->kf);
    }
    g_free(fc->filepath);
    fc->filepath = NULL;
    fc->group = fm_path_to_str(path);
    fc->kf = fc_cache;
    return fc;
}

static gboolean fm_folder_config_close(FmFolderConfig *fc, GError **error)
{
    gboolean ret = TRUE;

    if (fc->filepath)
    {
        if (fc->changed)
        {
            char *out;
            gsize len;

            out = g_key_file_to_data(fc->kf, &len, error);
            if (!out || !g_file_set_contents(fc->filepath, out, len, error))
                ret = FALSE;
            g_free(out);
        }
        g_free(fc->filepath);
        g_key_file_free(fc->kf);
    }
    else
    {
        if (fc->changed)
        {
            /* raise 'changed' flag and schedule config save */
            dir_cache_changed = TRUE;
            pcmanfm_save_config(FALSE);
        }
        g_free(fc->group);
    }

    g_slice_free(FmFolderConfig, fc);
    return ret;
}

static gboolean fm_folder_config_is_empty(FmFolderConfig *fc)
{
    return !g_key_file_has_group(fc->kf, fc->group);
}

static gboolean fm_folder_config_get_integer(FmFolderConfig *fc, const char *key,
                                             gint *val)
{
    return fm_key_file_get_int(fc->kf, fc->group, key, val);
}

static gboolean fm_folder_config_get_boolean(FmFolderConfig *fc, const char *key,
                                             gboolean *val)
{
    return fm_key_file_get_bool(fc->kf, fc->group, key, val);
}

static char *fm_folder_config_get_string(FmFolderConfig *fc, const char *key)
{
    return g_key_file_get_string(fc->kf, fc->group, key, NULL);
}

static char **fm_folder_config_get_string_list(FmFolderConfig *fc,
                                               const char *key, gsize *length)
{
    return g_key_file_get_string_list(fc->kf, fc->group, key, length, NULL);
}

#if !FM_CHECK_VERSION(1, 0, 2)
static void fm_folder_config_set_integer(FmFolderConfig *fc, const char *key,
                                         gint val)
{
    fc->changed = TRUE;
    g_key_file_set_integer(fc->kf, fc->group, key, val);
}
#endif

static void fm_folder_config_set_boolean(FmFolderConfig *fc, const char *key,
                                         gboolean val)
{
    fc->changed = TRUE;
    g_key_file_set_boolean(fc->kf, fc->group, key, val);
}

static void fm_folder_config_set_string(FmFolderConfig *fc, const char *key,
                                        const char *string)
{
    fc->changed = TRUE;
    g_key_file_set_string(fc->kf, fc->group, key, string);
}

static void fm_folder_config_set_string_list(FmFolderConfig *fc, const char *key,
                                             const gchar * const list[],
                                             gsize length)
{
    fc->changed = TRUE;
    g_key_file_set_string_list(fc->kf, fc->group, key, list, length);
}

static void fm_folder_config_remove_key(FmFolderConfig *fc, const char *key)
{
    fc->changed = TRUE;
    g_key_file_remove_key(fc->kf, fc->group, key, NULL);
}

static void fm_folder_config_purge(FmFolderConfig *fc)
{
    fc->changed = TRUE;
    g_key_file_remove_group(fc->kf, fc->group, NULL);
}

static void fm_folder_config_save_cache(const char *dir_path)
{
    char *path, *path2, *path3;
    char *out;
    gsize len;

    /* if per-directory cache was changed since last invocation then save it */
    if (dir_cache_changed)
    {
        out = g_key_file_to_data(fc_cache, &len, NULL);
        if (out)
        {
            /* create temp file with settings */
            path = g_build_filename(dir_path, "dir-settings.conf", NULL);
            path2 = g_build_filename(dir_path, "dir-settings.tmp", NULL);
            path3 = g_build_filename(dir_path, "dir-settings.backup", NULL);
            /* do safe replace now, the file is important enough to be lost */
            if (g_file_set_contents(path2, out, len, NULL))
            {
                /* backup old cache file */
                g_unlink(path3);
                if (!g_file_test(path, G_FILE_TEST_EXISTS) ||
                    g_rename(path, path3) == 0)
                {
                    /* rename temp file */
                    if (g_rename(path2, path) == 0)
                    {
                        /* success! remove the old cache file */
                        g_unlink(path3);
                        /* reset the 'changed' flag */
                        dir_cache_changed = FALSE;
                    }
                    else
                        g_warning("cannot rename %s to %s", path2, path);
                }
                else
                    g_warning("cannot rename %s to %s", path, path3);
            }
            else
                g_warning("cannot save %s", path2);
            g_free(path);
            g_free(path2);
            g_free(path3);
            g_free(out);
        }
    }
}
#endif /* LibFM < 1.2.0 */

#if FM_CHECK_VERSION(1, 0, 2)
static void _parse_sort(GKeyFile *kf, const char *group, FmSortMode *mode,
                        FmFolderModelCol *col)
{
    int tmp_int;
    char **sort;

    /* parse "sort" strings list first */
    sort = g_key_file_get_string_list(kf, group, "sort", NULL, NULL);
    if (sort)
    {
        FmSortMode tmp_mode = 0;
        FmFolderModelCol tmp_col = FM_FOLDER_MODEL_COL_DEFAULT;

        for (tmp_int = 0; sort[tmp_int]; tmp_int++)
        {
            if (tmp_int == 0) /* column should be first! */
                tmp_col = fm_folder_model_get_col_by_name(sort[tmp_int]);
            else if (strcmp(sort[tmp_int], "ascending") == 0)
                tmp_mode = (tmp_mode & ~FM_SORT_ORDER_MASK) | FM_SORT_ASCENDING;
            else if (strcmp(sort[tmp_int], "descending") == 0)
                tmp_mode = (tmp_mode & ~FM_SORT_ORDER_MASK) | FM_SORT_DESCENDING;
            else if (strcmp(sort[tmp_int], "case") == 0)
                tmp_mode |= FM_SORT_CASE_SENSITIVE;
#if FM_CHECK_VERSION(1, 2, 0)
            else if (strcmp(sort[tmp_int], "mingle") == 0)
                tmp_mode |= FM_SORT_NO_FOLDER_FIRST;
#endif
        }
        *mode = tmp_mode;
        if (tmp_col != FM_FOLDER_MODEL_COL_DEFAULT)
            *col = tmp_col;
        g_strfreev(sort);
        return;
    }
    /* parse fallback old style sort config */
    if(fm_key_file_get_int(kf, group, "sort_type", &tmp_int) &&
       tmp_int == GTK_SORT_DESCENDING)
        *mode = FM_SORT_DESCENDING;
    else
        *mode = FM_SORT_ASCENDING;
    if(fm_key_file_get_int(kf, group, "sort_by", &tmp_int) &&
#if FM_CHECK_VERSION(1, 2, 0)
       fm_folder_model_col_is_valid((guint)tmp_int))
#else
       FM_FOLDER_MODEL_COL_IS_VALID((guint)tmp_int))
#endif
        *col = tmp_int;
}
#else /* < 1.0.2 */
static void _parse_sort(GKeyFile *kf, const char *group, GtkSortType *mode,
                        int *col)
{
    int tmp_int;

    if(fm_key_file_get_int(kf, group, "sort_type", &tmp_int) &&
       tmp_int == GTK_SORT_DESCENDING)
        *mode = GTK_SORT_DESCENDING;
    else
        *mode = GTK_SORT_ASCENDING;
    if(fm_key_file_get_int(kf, group, "sort_by", &tmp_int) &&
       FM_FOLDER_MODEL_COL_IS_VALID((guint)tmp_int))
        *col = tmp_int;
}
#endif

#if FM_CHECK_VERSION(1, 0, 2)
static void _save_sort(GString *buf, FmSortMode mode, FmFolderModelCol col)
{
    const char *name = fm_folder_model_col_get_name(col);

    if (name == NULL) /* FM_FOLDER_MODEL_COL_NAME is always valid */
        name = fm_folder_model_col_get_name(FM_FOLDER_MODEL_COL_NAME);
    g_string_append_printf(buf, "sort=%s;%s;", name,
                           FM_SORT_IS_ASCENDING(mode) ? "ascending" : "descending");
    if (mode & FM_SORT_CASE_SENSITIVE)
        g_string_append(buf, "case;");
#if FM_CHECK_VERSION(1, 2, 0)
    if (mode & FM_SORT_NO_FOLDER_FIRST)
        g_string_append(buf, "mingle;");
#endif
    g_string_append_c(buf, '\n');
}
#else
static void _save_sort(GString *buf, GtkSortType type, int col)
{
    g_string_append_printf(buf, "sort_type=%d\n", sort_type);
    g_string_append_printf(buf, "sort_by=%d\n", col);
}
#endif

/* we use capitalized keys here because it is de-facto standard for
   desktop entry files and we use those keys in '.directory' as well */
#if FM_CHECK_VERSION(1, 0, 2)
static gboolean _parse_config_for_path(FmFolderConfig *fc,
                                       FmSortMode *mode, FmFolderModelCol *by,
#else
static gboolean _parse_config_for_path(FmFolderConfig *fc,
                                       GtkSortType *mode, gint *by,
#endif
                                       FmStandardViewMode *view_mode,
                                       gboolean *show_hidden, char ***columns)
{
    char *tmp;
    int tmp_int;
    /* we cannot use _parse_sort() here because we have no access to GKeyFile */
#if FM_CHECK_VERSION(1, 0, 2)
    char **sort;

    /* parse "sort" strings list first */
    sort = fm_folder_config_get_string_list(fc, "Sort", NULL);
    if (sort)
    {
        FmSortMode tmp_mode = 0;
        FmFolderModelCol tmp_col = FM_FOLDER_MODEL_COL_DEFAULT;

        for (tmp_int = 0; sort[tmp_int]; tmp_int++)
        {
            if (tmp_int == 0) /* column should be first! */
                tmp_col = fm_folder_model_get_col_by_name(sort[tmp_int]);
            else if (strcmp(sort[tmp_int], "ascending") == 0)
                tmp_mode = (tmp_mode & ~FM_SORT_ORDER_MASK) | FM_SORT_ASCENDING;
            else if (strcmp(sort[tmp_int], "descending") == 0)
                tmp_mode = (tmp_mode & ~FM_SORT_ORDER_MASK) | FM_SORT_DESCENDING;
            else if (strcmp(sort[tmp_int], "case") == 0)
                tmp_mode |= FM_SORT_CASE_SENSITIVE;
#if FM_CHECK_VERSION(1, 2, 0)
            else if (strcmp(sort[tmp_int], "mingle") == 0)
                tmp_mode |= FM_SORT_NO_FOLDER_FIRST;
#endif
        }
        *mode = tmp_mode;
        if (tmp_col != FM_FOLDER_MODEL_COL_DEFAULT)
            *by = tmp_col;
        g_strfreev(sort);
    }
    else
    {
        /* parse fallback old style sort config */
        if(fm_folder_config_get_integer(fc, "sort_type", &tmp_int) &&
           tmp_int == GTK_SORT_DESCENDING)
            *mode = FM_SORT_DESCENDING;
        else
            *mode = FM_SORT_ASCENDING;
        if(fm_folder_config_get_integer(fc, "sort_by", &tmp_int) &&
#if FM_CHECK_VERSION(1, 2, 0)
           fm_folder_model_col_is_valid((guint)tmp_int))
#else
           FM_FOLDER_MODEL_COL_IS_VALID((guint)tmp_int))
#endif
            *by = tmp_int;
    }
#else /* < 1.0.2 */
    if(fm_folder_config_get_integer(fc, "sort_type", &tmp_int) &&
       tmp_int == GTK_SORT_DESCENDING)
        *mode = GTK_SORT_DESCENDING;
    else
        *mode = GTK_SORT_ASCENDING;
    if(fm_folder_config_get_integer(fc, "sort_by", &tmp_int) &&
       FM_FOLDER_MODEL_COL_IS_VALID((guint)tmp_int))
        *by = tmp_int;
#endif
    if (view_mode && (tmp = fm_folder_config_get_string(fc, "ViewMode")))
    {
        *view_mode = fm_standard_view_mode_from_str(tmp);
        g_free(tmp);
    }
    if (show_hidden)
        fm_folder_config_get_boolean(fc, "ShowHidden", show_hidden);
#if FM_CHECK_VERSION(1, 0, 2)
    if (columns)
        *columns = fm_folder_config_get_string_list(fc, "Columns", NULL);
#endif
    return TRUE;
}


static void fm_app_config_finalize              (GObject *object);

G_DEFINE_TYPE(FmAppConfig, fm_app_config, FM_CONFIG_TYPE);


static void fm_app_config_class_init(FmAppConfigClass *klass)
{
    GObjectClass *g_object_class;
    g_object_class = G_OBJECT_CLASS(klass);
    g_object_class->finalize = fm_app_config_finalize;
}


static void fm_app_config_finalize(GObject *object)
{
    FmAppConfig *cfg;

    g_return_if_fail(object != NULL);
    g_return_if_fail(IS_FM_APP_CONFIG(object));

    cfg = FM_APP_CONFIG(object);
    if (cfg->desktop_section.configured)
    {
      if(cfg->desktop_section.wallpapers_configured > 0)
      {
        int i;

        for(i = 0; i < cfg->desktop_section.wallpapers_configured; i++)
            g_free(cfg->desktop_section.wallpapers[i]);
        g_free(cfg->desktop_section.wallpapers);
      }
      g_free(cfg->desktop_section.wallpaper);
      g_free(cfg->desktop_section.desktop_font);
    }
    /*g_free(cfg->su_cmd);*/

#if !FM_CHECK_VERSION(1, 2, 0)
    g_key_file_free(fc_cache);
    fc_cache = NULL;
#endif

    G_OBJECT_CLASS(fm_app_config_parent_class)->finalize(object);
}


static void fm_app_config_init(FmAppConfig *cfg)
{
    /* load libfm config file */
    fm_config_load_from_file((FmConfig*)cfg, NULL);

    cfg->bm_open_method = FM_OPEN_IN_CURRENT_TAB;

    cfg->mount_on_startup = TRUE;
    cfg->mount_removable = TRUE;
    cfg->autorun = TRUE;

    cfg->desktop_section.desktop_fg.red = cfg->desktop_section.desktop_fg.green = cfg->desktop_section.desktop_fg.blue = 65535;
    cfg->win_width = 640;
    cfg->win_height = 480;
    cfg->splitter_pos = 150;
    cfg->max_tab_chars = 32;

    cfg->side_pane_mode = FM_SP_PLACES;

    cfg->view_mode = FM_FV_ICON_VIEW;
    cfg->show_hidden = FALSE;
#if FM_CHECK_VERSION(1, 0, 2)
    cfg->sort_type = FM_SORT_ASCENDING;
    cfg->sort_by = FM_FOLDER_MODEL_COL_NAME;
    cfg->desktop_section.desktop_sort_type = FM_SORT_ASCENDING;
    cfg->desktop_section.desktop_sort_by = FM_FOLDER_MODEL_COL_MTIME;
#else
    cfg->sort_type = GTK_SORT_ASCENDING;
    cfg->sort_by = COL_FILE_NAME;
    cfg->desktop_section.desktop_sort_type = GTK_SORT_ASCENDING;
    cfg->desktop_section.desktop_sort_by = COL_FILE_MTIME;
#endif
    cfg->desktop_section.wallpaper_common = TRUE;
}


FmConfig *fm_app_config_new(void)
{
    return (FmConfig*)g_object_new(FM_APP_CONFIG_TYPE, NULL);
}

void fm_app_config_load_desktop_config(GKeyFile *kf, const char *group, FmDesktopConfig *cfg)
{
    char* tmp;
    int tmp_int;

    if (!g_key_file_has_group(kf, group))
        return;

    cfg->configured = TRUE;
    if(fm_key_file_get_int(kf, group, "wallpaper_mode", &tmp_int))
        cfg->wallpaper_mode = (FmWallpaperMode)tmp_int;

    if(cfg->wallpapers_configured > 0)
    {
        int i;

        for(i = 0; i < cfg->wallpapers_configured; i++)
            g_free(cfg->wallpapers[i]);
        g_free(cfg->wallpapers);
    }
    g_free(cfg->wallpaper);
    cfg->wallpaper = NULL;
    fm_key_file_get_int(kf, group, "wallpapers_configured", &cfg->wallpapers_configured);
    if(cfg->wallpapers_configured > 0)
    {
        char wpn_buf[32];
        int i;

        cfg->wallpapers = g_malloc(cfg->wallpapers_configured * sizeof(char *));
        for(i = 0; i < cfg->wallpapers_configured; i++)
        {
            snprintf(wpn_buf, sizeof(wpn_buf), "wallpaper%d", i);
            tmp = g_key_file_get_string(kf, group, wpn_buf, NULL);
            cfg->wallpapers[i] = tmp;
        }
    }
    fm_key_file_get_bool(kf, group, "wallpaper_common", &cfg->wallpaper_common);
    if (cfg->wallpaper_common)
    {
        tmp = g_key_file_get_string(kf, group, "wallpaper", NULL);
        g_free(cfg->wallpaper);
        cfg->wallpaper = tmp;
    }

    tmp = g_key_file_get_string(kf, group, "desktop_bg", NULL);
    if(tmp)
    {
        gdk_color_parse(tmp, &cfg->desktop_bg);
        g_free(tmp);
    }
    tmp = g_key_file_get_string(kf, group, "desktop_fg", NULL);
    if(tmp)
    {
        gdk_color_parse(tmp, &cfg->desktop_fg);
        g_free(tmp);
    }
    tmp = g_key_file_get_string(kf, group, "desktop_shadow", NULL);
    if(tmp)
    {
        gdk_color_parse(tmp, &cfg->desktop_shadow);
        g_free(tmp);
    }

    tmp = g_key_file_get_string(kf, group, "desktop_font", NULL);
    g_free(cfg->desktop_font);
    cfg->desktop_font = tmp;

    fm_key_file_get_bool(kf, group, "show_wm_menu", &cfg->show_wm_menu);
    _parse_sort(kf, group, &cfg->desktop_sort_type, &cfg->desktop_sort_by);
}

void fm_app_config_load_from_key_file(FmAppConfig* cfg, GKeyFile* kf)
{
#if FM_CHECK_VERSION(1, 0, 2)
    char *tmp;
    char **tmpv;
#endif
    int tmp_int;

    /* behavior */
    fm_key_file_get_int(kf, "config", "bm_open_method", &cfg->bm_open_method);
    /*tmp = g_key_file_get_string(kf, "config", "su_cmd", NULL);
    g_free(cfg->su_cmd);
    cfg->su_cmd = tmp;*/

    /* volume management */
    fm_key_file_get_bool(kf, "volume", "mount_on_startup", &cfg->mount_on_startup);
    fm_key_file_get_bool(kf, "volume", "mount_removable", &cfg->mount_removable);
    fm_key_file_get_bool(kf, "volume", "autorun", &cfg->autorun);

    /* [desktop] section */
    fm_app_config_load_desktop_config(kf, "desktop", &cfg->desktop_section);

    /* ui */
    fm_key_file_get_int(kf, "ui", "always_show_tabs", &cfg->always_show_tabs);
    fm_key_file_get_int(kf, "ui", "hide_close_btn", &cfg->hide_close_btn);
    fm_key_file_get_int(kf, "ui", "max_tab_chars", &cfg->max_tab_chars);

    fm_key_file_get_int(kf, "ui", "win_width", &cfg->win_width);
    fm_key_file_get_int(kf, "ui", "win_height", &cfg->win_height);

    fm_key_file_get_int(kf, "ui", "splitter_pos", &cfg->splitter_pos);

#if FM_CHECK_VERSION(1, 2, 0)
    tmp_int = FM_SP_NONE;
    tmpv = g_key_file_get_string_list(kf, "ui", "side_pane_mode", NULL, NULL);
    if (tmpv)
    {
        int i;

        for (i = 0; tmpv[i]; i++)
        {
            tmp = tmpv[i];
            if (strcmp(tmp, "hidden") == 0)
                tmp_int |= FM_SP_HIDE;
            else
            {
                tmp_int &= ~FM_SP_MODE_MASK;
                if (tmp[0] >= '0' && tmp[0] <= '9') /* backward compatibility */
                    tmp_int |= atoi(tmp);
                else /* portable way */
                    tmp_int |= fm_side_pane_get_mode_by_name(tmp);
            }
        }
        g_strfreev(tmpv);
    }
    if ((tmp_int & FM_SP_MODE_MASK) != FM_SP_NONE)
#else
    if(fm_key_file_get_int(kf, "ui", "side_pane_mode", &tmp_int))
#endif
        cfg->side_pane_mode = (FmSidePaneMode)tmp_int;

    /* default values for folder views */
#if FM_CHECK_VERSION(1, 0, 2)
    tmp = g_key_file_get_string(kf, "ui", "view_mode", NULL);
    if (tmp)
    {
        if (tmp[0] >= '0' && tmp[0] <= '9') /* backward compatibility */
            tmp_int = atoi(tmp);
        else /* portable way */
            tmp_int = fm_standard_view_mode_from_str(tmp);
        g_free(tmp);
    }
    if (tmp &&
#else
    if(fm_key_file_get_int(kf, "ui", "view_mode", &tmp_int) &&
#endif
       FM_STANDARD_VIEW_MODE_IS_VALID(tmp_int))
        cfg->view_mode = tmp_int;
    fm_key_file_get_bool(kf, "ui", "show_hidden", &cfg->show_hidden);
    _parse_sort(kf, "ui", &cfg->sort_type, &cfg->sort_by);
#if FM_CHECK_VERSION(1, 0, 2)
    tmpv = g_key_file_get_string_list(kf, "ui", "columns", NULL, NULL);
    if (tmpv)
    {
        g_strfreev(cfg->columns);
        cfg->columns = tmpv;
    }
#endif
}

void fm_app_config_load_from_profile(FmAppConfig* cfg, const char* name)
{
    const gchar * const *dirs, * const *dir;
    char *path;
    GKeyFile* kf = g_key_file_new();
    const char* old_name = name;

    if(!name || !*name) /* if profile name is not provided, use 'default' */
    {
        name = "default";
        old_name = "pcmanfm"; /* for compatibility with old versions. */
    }

    /* load system-wide settings */
    dirs = g_get_system_config_dirs();
    for(dir=dirs;*dir;++dir)
    {
        path = g_build_filename(*dir, "pcmanfm", name, "pcmanfm.conf", NULL);
        if(g_key_file_load_from_file(kf, path, 0, NULL))
            fm_app_config_load_from_key_file(cfg, kf);
        g_free(path);
    }

    /* override system-wide settings with user-specific configuration */

    /* For backward compatibility, try to load old config file and
     * then migrate to new location */
    path = g_strconcat(g_get_user_config_dir(), "/pcmanfm/", old_name, ".conf", NULL);
    if(G_UNLIKELY(g_key_file_load_from_file(kf, path, 0, NULL)))
    {
        char* new_dir;
        /* old config file is found, migrate to new profile format */
        fm_app_config_load_from_key_file(cfg, kf);

        /* create the profile dir */
        new_dir = g_build_filename(g_get_user_config_dir(), "pcmanfm", name, NULL);
        if(g_mkdir_with_parents(new_dir, 0700) == 0)
        {
            /* move the old config file to new location */
            char* new_path = g_build_filename(new_dir, "pcmanfm.conf", NULL);
            rename(path, new_path);
            g_free(new_path);
        }
        g_free(new_dir);
    }
    else
    {
        g_free(path);
        path = g_build_filename(g_get_user_config_dir(), "pcmanfm", name, "pcmanfm.conf", NULL);
        if(g_key_file_load_from_file(kf, path, 0, NULL))
            fm_app_config_load_from_key_file(cfg, kf);
    }
    g_free(path);
    g_key_file_free(kf);

#if !FM_CHECK_VERSION(1, 2, 0)
    fc_cache = g_key_file_new();
    path = g_build_filename(g_get_user_config_dir(), "pcmanfm", name,
                            "dir-settings.conf", NULL);
    g_key_file_load_from_file(fc_cache, path, 0, NULL);
    g_free(path);
#endif
}

/**
 * fm_app_config_get_config_for_path
 * @path: path to get config
 * @mode: (allow-none) (out): location to save sort mode
 * @by: (allow-none) (out): location to save sort columns
 * @view_mode: (allow-none) (out): location to save view mode
 * @show_hidden: (allow-none) (out): location to save show hidden flag
 * @columns: (allow-none) (out) (transfer none): location to save columns list
 *
 * Returns: %TRUE if @path has individual configuration.
 */
#if FM_CHECK_VERSION(1, 0, 2)
gboolean fm_app_config_get_config_for_path(FmPath *path, FmSortMode *mode,
                                           FmFolderModelCol *by,
#else
gboolean fm_app_config_get_config_for_path(FmPath *path, GtkSortType *mode,
                                           gint *by,
#endif
                                           FmStandardViewMode *view_mode,
                                           gboolean *show_hidden,
                                           char ***columns)
{
    FmPath *sub_path;
    FmFolderConfig *fc;
    gboolean ret = TRUE;

    /* preload defaults */
    if (mode)
        *mode = app_config->sort_type;
    if (by)
        *by = app_config->sort_by;
    if (view_mode)
        *view_mode = app_config->view_mode;
    if (show_hidden)
        *show_hidden = app_config->show_hidden;
#if FM_CHECK_VERSION(1, 0, 2)
    if (columns)
        *columns = app_config->columns;
#endif
    fc = fm_folder_config_open(path);
    if (!fm_folder_config_is_empty(fc))
        _parse_config_for_path(fc, mode, by, view_mode, show_hidden, columns);
    else if (!fm_path_is_native(path))
    {
        /* if path is non-native then try the scheme */
#if FM_CHECK_VERSION(1, 2, 0)
        sub_path = fm_path_get_scheme_path(path);
#else
        for (sub_path = path; fm_path_get_parent(sub_path) != NULL; )
            sub_path = fm_path_get_parent(sub_path);
#endif
        fm_folder_config_close(fc, NULL);
        fc = fm_folder_config_open(sub_path);
        if (!fm_folder_config_is_empty(fc))
            _parse_config_for_path(fc, mode, by, view_mode, show_hidden, columns);
        /* if path is search://... then use predefined values */
        else if (strncmp(fm_path_get_basename(sub_path), "search:", 7) == 0)
        {
            if (view_mode)
                *view_mode = FM_FV_LIST_VIEW;
            if (show_hidden)
                *show_hidden = TRUE;
#if FM_CHECK_VERSION(1, 0, 2)
            if (columns)
            {
                static char *def[] = {"name", "desc", "dirname", "size", "mtime", NULL};
                *columns = def;
            }
#endif
        }
        else
            ret = FALSE;
    }
    else
        ret = FALSE;
    fm_folder_config_close(fc, NULL);
    return ret;
}

/**
 * @path, @mode, @by, @show_hidden are mandatory
 * @view_mode may be -1 to not change
 * @columns may be %NULL to not change
 */
#if FM_CHECK_VERSION(1, 0, 2)
void fm_app_config_save_config_for_path(FmPath *path, FmSortMode mode,
                                        FmFolderModelCol by,
#else
void fm_app_config_save_config_for_path(FmPath *path, GtkSortType mode, gint by,
#endif
                                        FmStandardViewMode view_mode,
                                        gboolean show_hidden, char **columns)
{
    FmPath *sub_path;
    FmFolderConfig *fc;
#if FM_CHECK_VERSION(1, 0, 2)
    char const *list[5];
    int n = 2;
#endif

    /* if path is search://... then use search: instead */
#if FM_CHECK_VERSION(1, 2, 0)
    sub_path = fm_path_get_scheme_path(path);
#else
    for (sub_path = path; fm_path_get_parent(sub_path) != NULL; )
        sub_path = fm_path_get_parent(sub_path);
#endif
    if (strncmp(fm_path_get_basename(sub_path), "search:", 7) == 0)
    {
        path = fm_path_new_for_uri("search:///");
        fc = fm_folder_config_open(path);
        fm_path_unref(path);
        /* allow to create new entry only if we got valid columns */
        if (fm_folder_config_is_empty(fc) && (columns != NULL && columns[0] != NULL))
        {
            fm_folder_config_close(fc, NULL);
            /* save columns is mandatory for search view */
            return;
        }
        view_mode = FM_FV_LIST_VIEW; /* search view mode should be immutable */
    }
    else
        fc = fm_folder_config_open(path);
#if FM_CHECK_VERSION(1, 0, 2)
    list[0] = fm_folder_model_col_get_name(by);
    if (list[0] == NULL) /* FM_FOLDER_MODEL_COL_NAME is always valid */
        list[0] = fm_folder_model_col_get_name(FM_FOLDER_MODEL_COL_NAME);
    list[1] = FM_SORT_IS_ASCENDING(mode) ? "ascending" : "descending";
    if (mode & FM_SORT_CASE_SENSITIVE)
        list[n++] = "case";
#if FM_CHECK_VERSION(1, 2, 0)
    if (mode & FM_SORT_NO_FOLDER_FIRST)
        list[n++] = "mingle";
#endif
    list[n] = NULL;
    fm_folder_config_set_string_list(fc, "Sort", list, n);
    if (FM_STANDARD_VIEW_MODE_IS_VALID(view_mode))
        fm_folder_config_set_string(fc, "ViewMode",
                                    fm_standard_view_mode_to_str(view_mode));
    if (columns && columns[0])
        fm_folder_config_set_string_list(fc, "Columns",
                                         (const char *const *)columns,
                                         g_strv_length(columns));
    else if (columns) /* empty list means we should reset columns */
        fm_folder_config_remove_key(fc, "Columns");
#else /* pre-1.0.2 */
    if (FM_FOLDER_VIEW_MODE_IS_VALID(view_mode))
        fm_folder_config_set_integer(fc, "ViewMode", view_mode);
    fm_folder_config_set_integer(fc, "sort_type", mode);
    fm_folder_config_set_integer(fc, "sort_by", by);
#endif
    fm_folder_config_set_boolean(fc, "ShowHidden", show_hidden);
    fm_folder_config_close(fc, NULL);
#if FM_CHECK_VERSION(1, 2, 0)
    /* raise 'changed' flag and schedule config save */
    pcmanfm_save_config(FALSE);
#endif
}

void fm_app_config_clear_config_for_path(FmPath *path)
{
    FmFolderConfig *fc = fm_folder_config_open(path);

    fm_folder_config_purge(fc);
    fm_folder_config_close(fc, NULL);
#if FM_CHECK_VERSION(1, 2, 0)
    /* raise 'changed' flag and schedule config save */
    pcmanfm_save_config(FALSE);
#endif
}

void fm_app_config_save_desktop_config(GString *buf, const char *group, FmDesktopConfig *cfg)
{
    g_string_append_printf(buf, "[%s]\n"
                                "wallpaper_mode=%d\n", group, cfg->wallpaper_mode);
    g_string_append_printf(buf, "wallpaper_common=%d\n", cfg->wallpaper_common);
    if (cfg->wallpapers && cfg->wallpapers_configured > 0)
    {
        int i;

        g_string_append_printf(buf, "wallpapers_configured=%d\n", cfg->wallpapers_configured);
        for (i = 0; i < cfg->wallpapers_configured; i++)
            if (cfg->wallpapers[i])
                g_string_append_printf(buf, "wallpaper%d=%s\n", i, cfg->wallpapers[i]);
    }
    if (cfg->wallpaper_common && cfg->wallpaper)
        g_string_append_printf(buf, "wallpaper=%s\n", cfg->wallpaper);
    g_string_append_printf(buf, "desktop_bg=#%02x%02x%02x\n",
                           cfg->desktop_bg.red/257,
                           cfg->desktop_bg.green/257,
                           cfg->desktop_bg.blue/257);
    g_string_append_printf(buf, "desktop_fg=#%02x%02x%02x\n",
                           cfg->desktop_fg.red/257,
                           cfg->desktop_fg.green/257,
                           cfg->desktop_fg.blue/257);
    g_string_append_printf(buf, "desktop_shadow=#%02x%02x%02x\n",
                           cfg->desktop_shadow.red/257,
                           cfg->desktop_shadow.green/257,
                           cfg->desktop_shadow.blue/257);
    if(cfg->desktop_font && *cfg->desktop_font)
        g_string_append_printf(buf, "desktop_font=%s\n", cfg->desktop_font);
    g_string_append_printf(buf, "show_wm_menu=%d\n", cfg->show_wm_menu);
    _save_sort(buf, cfg->desktop_sort_type, cfg->desktop_sort_by);
}

void fm_app_config_save_profile(FmAppConfig* cfg, const char* name)
{
    char* path = NULL;;
    char* dir_path;

    if(!name || !*name)
        name = "default";

    dir_path = g_build_filename(g_get_user_config_dir(), "pcmanfm", name, NULL);
    if(g_mkdir_with_parents(dir_path, 0700) != -1)
    {
        GString* buf = g_string_sized_new(1024);

        g_string_append(buf, "[config]\n");
        g_string_append_printf(buf, "bm_open_method=%d\n", cfg->bm_open_method);
        /*if(cfg->su_cmd && *cfg->su_cmd)
            g_string_append_printf(buf, "su_cmd=%s\n", cfg->su_cmd);*/

        g_string_append(buf, "\n[volume]\n");
        g_string_append_printf(buf, "mount_on_startup=%d\n", cfg->mount_on_startup);
        g_string_append_printf(buf, "mount_removable=%d\n", cfg->mount_removable);
        g_string_append_printf(buf, "autorun=%d\n", cfg->autorun);

        g_string_append(buf, "\n[ui]\n");
        g_string_append_printf(buf, "always_show_tabs=%d\n", cfg->always_show_tabs);
        g_string_append_printf(buf, "max_tab_chars=%d\n", cfg->max_tab_chars);
        /* g_string_append_printf(buf, "hide_close_btn=%d\n", cfg->hide_close_btn); */
        g_string_append_printf(buf, "win_width=%d\n", cfg->win_width);
        g_string_append_printf(buf, "win_height=%d\n", cfg->win_height);
        g_string_append_printf(buf, "splitter_pos=%d\n", cfg->splitter_pos);
#if FM_CHECK_VERSION(1, 2, 0)
        g_string_append(buf, "side_pane_mode=");
        if (cfg->side_pane_mode & FM_SP_HIDE)
            g_string_append(buf, "hidden;");
        g_string_append_printf(buf, "%s\n",
                               fm_side_pane_get_mode_name(cfg->side_pane_mode & FM_SP_MODE_MASK));
#else
        g_string_append_printf(buf, "side_pane_mode=%d\n", cfg->side_pane_mode);
#endif
#if FM_CHECK_VERSION(1, 0, 2)
        g_string_append_printf(buf, "view_mode=%s\n", fm_standard_view_mode_to_str(cfg->view_mode));
#else
        g_string_append_printf(buf, "view_mode=%d\n", cfg->view_mode);
#endif
        g_string_append_printf(buf, "show_hidden=%d\n", cfg->show_hidden);
        _save_sort(buf, cfg->sort_type, cfg->sort_by);
#if FM_CHECK_VERSION(1, 0, 2)
        if (cfg->columns && cfg->columns[0])
        {
            char **colptr;

            g_string_append(buf, "columns=");
            for (colptr = cfg->columns; *colptr; colptr++)
                g_string_append_printf(buf, "%s;", *colptr);
            g_string_append_c(buf, '\n');
        }
#endif

        path = g_build_filename(dir_path, "pcmanfm.conf", NULL);
        g_file_set_contents(path, buf->str, buf->len, NULL);
        g_free(path);
        g_string_free(buf, TRUE);

#if FM_CHECK_VERSION(1, 2, 0)
        /* libfm does not have any profile things */
        fm_folder_config_save_cache();
#else
        fm_folder_config_save_cache(dir_path);
#endif
    }
    g_free(dir_path);
}
