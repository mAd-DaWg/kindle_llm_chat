#pragma once

#include <gtk/gtk.h>
#include <string>

/** Create GtkTextTags used by markdown_insert (idempotent). */
void markdown_ensure_tags(GtkTextBuffer *buffer);

/** Insert UTF-8 CommonMark (GitHub-flavored extensions) at iter; advances iter. */
void markdown_insert(GtkTextBuffer *buffer, GtkTextIter *iter, const std::string &markdown);
