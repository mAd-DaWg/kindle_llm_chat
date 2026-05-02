#include "markdown_view.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "md4c.h"
}

namespace {

struct MdGtk {
  GtkTextBuffer *buf = nullptr;
  GtkTextIter *it = nullptr;
  /** Block/structural tags (quote, list, headings, code fences, …). */
  std::vector<const char *> block_tags;
  /** Inline span tags (emphasis, code, links, …) — never mixed with block pops. */
  std::vector<const char *> span_tags;
  bool in_ordered_list = false;
  unsigned ol_next = 1;
  /** Nested `>` blockquote depth (for CommonMark-style cumulative indent). */
  unsigned quote_depth = 0;
};

bool block_stack_has_list(const MdGtk *ctx) {
  for (const char *t : ctx->block_tags) {
    if (t && std::strcmp(t, "md_list") == 0) {
      return true;
    }
  }
  return false;
}

const char *quote_tag_for_depth(unsigned depth) {
  const unsigned c = std::min(depth, 4u);
  switch (c) {
  case 1:
    return "md_quote1";
  case 2:
    return "md_quote2";
  case 3:
    return "md_quote3";
  default:
    return "md_quote4";
  }
}

void insert_raw(MdGtk *ctx, const char *text, MD_SIZE size) {
  if (size == 0) {
    return;
  }
  /* gtk_text_buffer_insert invalidates every GtkTextIter except the one passed in
   * (which it advances past the new text). A copied "start" before insert is unsafe. */
  const gint start_off = gtk_text_iter_get_offset(ctx->it);
  gtk_text_buffer_insert(ctx->buf, ctx->it, text, static_cast<gint>(size));
  GtkTextIter start;
  gtk_text_buffer_get_iter_at_offset(ctx->buf, &start, start_off);
  for (const char *tag : ctx->block_tags) {
    if (tag) {
      gtk_text_buffer_apply_tag_by_name(ctx->buf, tag, &start, ctx->it);
    }
  }
  for (const char *tag : ctx->span_tags) {
    if (tag) {
      gtk_text_buffer_apply_tag_by_name(ctx->buf, tag, &start, ctx->it);
    }
  }
}

void insert_nl(MdGtk *ctx) { insert_raw(ctx, "\n", 1); }

/** ATX `#` / `##` / … / `######` and Setext headings (per CommonMark / Markdown Guide). */
const char *heading_tag(unsigned level) {
  switch (level) {
  case 1:
    return "md_h1";
  case 2:
    return "md_h2";
  case 3:
    return "md_h3";
  case 4:
    return "md_h4";
  case 5:
    return "md_h5";
  case 6:
    return "md_h6";
  default:
    return "md_h4";
  }
}

int on_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  auto *ctx = static_cast<MdGtk *>(userdata);
  switch (type) {
  case MD_BLOCK_DOC:
    break;
  case MD_BLOCK_QUOTE:
    ctx->quote_depth++;
    ctx->block_tags.push_back(quote_tag_for_depth(ctx->quote_depth));
    break;
  case MD_BLOCK_UL:
    ctx->in_ordered_list = false;
    break;
  case MD_BLOCK_OL: {
    const auto *d = static_cast<const MD_BLOCK_OL_DETAIL *>(detail);
    ctx->in_ordered_list = true;
    ctx->ol_next = (d && d->start > 0) ? d->start : 1;
    break;
  }
  case MD_BLOCK_LI: {
    const auto *d = static_cast<const MD_BLOCK_LI_DETAIL *>(detail);
    if (d && d->is_task) {
      const char *mark = (d->task_mark == 'x' || d->task_mark == 'X') ? "[x] " : "[ ] ";
      insert_raw(ctx, mark, static_cast<MD_SIZE>(strlen(mark)));
    } else if (ctx->in_ordered_list) {
      char line[32];
      const int n = std::snprintf(line, sizeof(line), "%u. ", ctx->ol_next++);
      if (n > 0) {
        insert_raw(ctx, line, static_cast<MD_SIZE>(n));
      }
    } else {
      static const char bullet[] = "\xe2\x80\xa2 ";
      insert_raw(ctx, bullet, static_cast<MD_SIZE>(sizeof(bullet) - 1));
    }
    ctx->block_tags.push_back("md_list");
    break;
  }
  case MD_BLOCK_HR:
    /* Thematic break (---, ***, ___): blank lines around it match Markdown Guide best practices. */
    insert_raw(ctx, "\n---\n\n", 6);
    break;
  case MD_BLOCK_H: {
    const auto *d = static_cast<const MD_BLOCK_H_DETAIL *>(detail);
    const unsigned lvl = d && d->level >= 1 && d->level <= 6 ? d->level : 1;
    ctx->block_tags.push_back(heading_tag(lvl));
    break;
  }
  case MD_BLOCK_CODE:
    ctx->block_tags.push_back("md_pre");
    break;
  case MD_BLOCK_HTML:
    ctx->block_tags.push_back("md_html");
    break;
  case MD_BLOCK_P:
    break;
  case MD_BLOCK_TABLE:
    insert_nl(ctx);
    break;
  case MD_BLOCK_THEAD:
  case MD_BLOCK_TBODY:
    break;
  case MD_BLOCK_TR: {
    static const char sep[] = "| ";
    insert_raw(ctx, sep, static_cast<MD_SIZE>(sizeof(sep) - 1));
    break;
  }
  case MD_BLOCK_TH:
    ctx->block_tags.push_back("md_th");
    break;
  case MD_BLOCK_TD:
    ctx->block_tags.push_back(nullptr);
    break;
  default:
    break;
  }
  return 0;
}

int on_leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  auto *ctx = static_cast<MdGtk *>(userdata);
  switch (type) {
  case MD_BLOCK_DOC:
    break;
  case MD_BLOCK_QUOTE:
    if (!ctx->block_tags.empty()) {
      const char *b = ctx->block_tags.back();
      if (b && std::strncmp(b, "md_quote", 8) == 0) {
        ctx->block_tags.pop_back();
      }
    }
    if (ctx->quote_depth > 0) {
      ctx->quote_depth--;
    }
    insert_nl(ctx);
    break;
  case MD_BLOCK_UL:
    insert_nl(ctx);
    break;
  case MD_BLOCK_OL:
    ctx->in_ordered_list = false;
    insert_nl(ctx);
    break;
  case MD_BLOCK_LI:
    if (!ctx->block_tags.empty()) {
      ctx->block_tags.pop_back();
    }
    insert_nl(ctx);
    break;
  case MD_BLOCK_HR:
    break;
  case MD_BLOCK_H:
    if (!ctx->block_tags.empty()) {
      ctx->block_tags.pop_back();
    }
    insert_nl(ctx);
    insert_nl(ctx);
    break;
  case MD_BLOCK_CODE:
    if (!ctx->block_tags.empty()) {
      ctx->block_tags.pop_back();
    }
    insert_nl(ctx);
    insert_nl(ctx);
    break;
  case MD_BLOCK_HTML:
    if (!ctx->block_tags.empty()) {
      ctx->block_tags.pop_back();
    }
    insert_nl(ctx);
    break;
  case MD_BLOCK_P:
    /* CommonMark: soft line ends in a paragraph are spaces; blank line *between* paragraphs is two newlines. */
    if (block_stack_has_list(ctx)) {
      insert_nl(ctx);
    } else {
      insert_raw(ctx, "\n\n", 2);
    }
    break;
  case MD_BLOCK_TR:
    insert_nl(ctx);
    break;
  case MD_BLOCK_TH:
    if (!ctx->block_tags.empty()) {
      ctx->block_tags.pop_back();
    }
    insert_raw(ctx, " | ", 3);
    break;
  case MD_BLOCK_TD:
    if (!ctx->block_tags.empty()) {
      ctx->block_tags.pop_back();
    }
    insert_raw(ctx, " | ", 3);
    break;
  case MD_BLOCK_TABLE:
    insert_nl(ctx);
    break;
  default:
    break;
  }
  (void)detail;
  return 0;
}

int on_enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
  auto *ctx = static_cast<MdGtk *>(userdata);
  switch (type) {
  case MD_SPAN_EM:
    ctx->span_tags.push_back("md_em");
    break;
  case MD_SPAN_STRONG:
    ctx->span_tags.push_back("md_strong");
    break;
  case MD_SPAN_A:
    ctx->span_tags.push_back("md_link");
    break;
  case MD_SPAN_IMG:
    ctx->span_tags.push_back("md_em");
    break;
  case MD_SPAN_CODE:
    ctx->span_tags.push_back("md_code");
    break;
  case MD_SPAN_DEL:
    ctx->span_tags.push_back("md_del");
    break;
  default:
    ctx->span_tags.push_back(nullptr);
    break;
  }
  (void)detail;
  return 0;
}

int on_leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
  auto *ctx = static_cast<MdGtk *>(userdata);
  if (!ctx->span_tags.empty()) {
    ctx->span_tags.pop_back();
  }
  (void)type;
  (void)detail;
  return 0;
}

int on_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata) {
  auto *ctx = static_cast<MdGtk *>(userdata);
  if (type == MD_TEXT_NULLCHAR) {
    insert_raw(ctx, "\xEF\xBF\xBD", 3);
    return 0;
  }
  /* md4c: SOFTBR = line break inside a paragraph without two trailing spaces (HTML space). BR = hard break. */
  if (type == MD_TEXT_SOFTBR) {
    insert_raw(ctx, " ", 1);
    return 0;
  }
  if (type == MD_TEXT_BR) {
    insert_nl(ctx);
    return 0;
  }
  if (type == MD_TEXT_CODE || type == MD_TEXT_NORMAL || type == MD_TEXT_ENTITY || type == MD_TEXT_HTML ||
      type == MD_TEXT_LATEXMATH) {
    insert_raw(ctx, text, size);
    return 0;
  }
  return 0;
}

static void alloc_color(GdkColor *c, const char *spec) {
  gdk_color_parse(spec, c);
  GdkColormap *cm = gdk_colormap_get_system();
  gdk_colormap_alloc_color(cm, c, FALSE, TRUE);
}

} // namespace

void markdown_ensure_tags(GtkTextBuffer *buffer) {
  GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
  if (gtk_text_tag_table_lookup(table, "md_strong")) {
    return;
  }

  GdkColor role_fg;
  GdkColor code_bg;
  GdkColor pre_bg;
  GdkColor link_fg;
  GdkColor del_fg;
  GdkColor quote_fg;
  GdkColor html_fg;
  GdkColor heading_fg;
  alloc_color(&role_fg, "#555555");
  /* GitHub / ChatGPT-style inline code chip */
  alloc_color(&code_bg, "#f6f8fa");
  alloc_color(&pre_bg, "#f0f3f7");
  alloc_color(&link_fg, "#0969da");
  alloc_color(&del_fg, "#656d76");
  alloc_color(&quote_fg, "#57606a");
  alloc_color(&html_fg, "#656d76");
  alloc_color(&heading_fg, "#1f2328");

  gtk_text_buffer_create_tag(buffer, "md_role", "foreground-gdk", &role_fg, "foreground-set", TRUE, "weight",
                             PANGO_WEIGHT_BOLD, "weight-set", TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_strong", "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_em", "style", PANGO_STYLE_ITALIC, "style-set", TRUE, nullptr);
  /* Inline `code`: monospace + tinted background (needs background-set + alloc'd GdkColor on GTK+ 2). */
  gtk_text_buffer_create_tag(buffer, "md_code", "font", "Monospace 9", "foreground-gdk", &heading_fg, "foreground-set",
                             TRUE, "background-gdk", &code_bg, "background-set", TRUE, "pixels-above-lines", 1,
                             "pixels-below-lines", 1, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_pre", "font", "Monospace 9", "foreground-gdk", &heading_fg, "foreground-set",
                             TRUE, "background-gdk", &pre_bg, "background-set", TRUE, "pixels-above-lines", 4,
                             "pixels-below-lines", 4, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_html", "font", "Monospace 9", "foreground-gdk", &html_fg, "foreground-set",
                             TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_link", "foreground-gdk", &link_fg, "foreground-set", TRUE, "underline",
                             PANGO_UNDERLINE_SINGLE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_del", "strikethrough", TRUE, "foreground-gdk", &del_fg, "foreground-set", TRUE,
                             nullptr);
  gtk_text_buffer_create_tag(buffer, "md_quote1", "left-margin", 8, "foreground-gdk", &quote_fg, "foreground-set",
                             TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_quote2", "left-margin", 20, "foreground-gdk", &quote_fg, "foreground-set",
                             TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_quote3", "left-margin", 32, "foreground-gdk", &quote_fg, "foreground-set",
                             TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_quote4", "left-margin", 44, "foreground-gdk", &quote_fg, "foreground-set",
                             TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_list", "left-margin", 6, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_th", "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, "foreground-gdk",
                             &heading_fg, "foreground-set", TRUE, nullptr);
  /* Headings: scale only applies when scale-set is TRUE (GTK+ 2). */
  gtk_text_buffer_create_tag(buffer, "md_h1", "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, "foreground-gdk",
                             &heading_fg, "foreground-set", TRUE, "scale", 1.75, "scale-set", TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h2", "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, "foreground-gdk",
                             &heading_fg, "foreground-set", TRUE, "scale", 1.48, "scale-set", TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h3", "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, "foreground-gdk",
                             &heading_fg, "foreground-set", TRUE, "scale", 1.28, "scale-set", TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h4", "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, "foreground-gdk",
                             &heading_fg, "foreground-set", TRUE, "scale", 1.12, "scale-set", TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h5", "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, "foreground-gdk",
                             &heading_fg, "foreground-set", TRUE, "scale", 1.06, "scale-set", TRUE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h6", "weight", PANGO_WEIGHT_BOLD, "weight-set", TRUE, "foreground-gdk",
                             &heading_fg, "foreground-set", TRUE, "scale", 1.02, "scale-set", TRUE, nullptr);
}

bool markdown_insert(GtkTextBuffer *buffer, GtkTextIter *iter, const std::string &markdown) {
  markdown_ensure_tags(buffer);
  GtkTextIter start_snap = *iter;
  MdGtk ctx;
  ctx.buf = buffer;
  ctx.it = iter;

  MD_PARSER parser{};
  parser.abi_version = 0;
  /*
   * CommonMark + GitHub tables / strike / tasks / autolinks (typical LLM output).
   * Line breaks and paragraph spacing follow https://www.markdownguide.org/basic-syntax/ (soft break vs hard break,
   * blank lines between paragraphs). PERMISSIVEATXHEADERS allows ###Title without a space (many apps accept it).
   */
  parser.flags = MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS;
  parser.enter_block = on_enter_block;
  parser.leave_block = on_leave_block;
  parser.enter_span = on_enter_span;
  parser.leave_span = on_leave_span;
  parser.text = on_text;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  const int rc = md_parse(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()), &parser, &ctx);
  if (rc != 0) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    if (gtk_text_iter_compare(&start_snap, &end) < 0) {
      gtk_text_buffer_delete(buffer, &start_snap, &end);
    }
    const gint off = gtk_text_iter_get_offset(&start_snap);
    gtk_text_buffer_get_iter_at_offset(buffer, iter, off);
    gtk_text_buffer_insert(buffer, iter, markdown.c_str(), static_cast<gint>(markdown.size()));
    return false;
  }
  return true;
}
