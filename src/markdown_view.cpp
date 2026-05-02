#include "markdown_view.h"

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
  std::vector<const char *> tag_stack;
  bool in_ordered_list = false;
  unsigned ol_next = 1;
};

void insert_raw(MdGtk *ctx, const char *text, MD_SIZE size) {
  if (size == 0) {
    return;
  }
  GtkTextIter start = *ctx->it;
  gtk_text_buffer_insert(ctx->buf, ctx->it, text, static_cast<gint>(size));
  for (const char *tag : ctx->tag_stack) {
    if (tag) {
      gtk_text_buffer_apply_tag_by_name(ctx->buf, tag, &start, ctx->it);
    }
  }
}

void insert_nl(MdGtk *ctx) { insert_raw(ctx, "\n", 1); }

const char *heading_tag(unsigned level) {
  switch (level) {
  case 1:
    return "md_h1";
  case 2:
    return "md_h2";
  case 3:
    return "md_h3";
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
    ctx->tag_stack.push_back("md_quote");
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
    ctx->tag_stack.push_back("md_list");
    break;
  }
  case MD_BLOCK_HR:
    insert_raw(ctx, "----\n", 5);
    break;
  case MD_BLOCK_H: {
    const auto *d = static_cast<const MD_BLOCK_H_DETAIL *>(detail);
    const unsigned lvl = d && d->level >= 1 && d->level <= 6 ? d->level : 1;
    ctx->tag_stack.push_back(heading_tag(lvl));
    break;
  }
  case MD_BLOCK_CODE:
    ctx->tag_stack.push_back("md_pre");
    break;
  case MD_BLOCK_HTML:
    ctx->tag_stack.push_back("md_html");
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
    ctx->tag_stack.push_back("md_th");
    break;
  case MD_BLOCK_TD:
    ctx->tag_stack.push_back(nullptr);
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
    if (!ctx->tag_stack.empty() && std::strcmp(ctx->tag_stack.back(), "md_quote") == 0) {
      ctx->tag_stack.pop_back();
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
    if (!ctx->tag_stack.empty()) {
      ctx->tag_stack.pop_back();
    }
    insert_nl(ctx);
    break;
  case MD_BLOCK_HR:
    break;
  case MD_BLOCK_H:
    if (!ctx->tag_stack.empty()) {
      ctx->tag_stack.pop_back();
    }
    insert_nl(ctx);
    insert_nl(ctx);
    break;
  case MD_BLOCK_CODE:
    if (!ctx->tag_stack.empty()) {
      ctx->tag_stack.pop_back();
    }
    insert_nl(ctx);
    insert_nl(ctx);
    break;
  case MD_BLOCK_HTML:
    if (!ctx->tag_stack.empty()) {
      ctx->tag_stack.pop_back();
    }
    insert_nl(ctx);
    break;
  case MD_BLOCK_P:
    insert_nl(ctx);
    break;
  case MD_BLOCK_TR:
    insert_nl(ctx);
    break;
  case MD_BLOCK_TH:
    if (!ctx->tag_stack.empty()) {
      ctx->tag_stack.pop_back();
    }
    insert_raw(ctx, " | ", 3);
    break;
  case MD_BLOCK_TD:
    if (!ctx->tag_stack.empty()) {
      ctx->tag_stack.pop_back();
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
    ctx->tag_stack.push_back("md_em");
    break;
  case MD_SPAN_STRONG:
    ctx->tag_stack.push_back("md_strong");
    break;
  case MD_SPAN_A:
    ctx->tag_stack.push_back("md_link");
    break;
  case MD_SPAN_IMG:
    ctx->tag_stack.push_back("md_em");
    break;
  case MD_SPAN_CODE:
    ctx->tag_stack.push_back("md_code");
    break;
  case MD_SPAN_DEL:
    ctx->tag_stack.push_back("md_del");
    break;
  default:
    ctx->tag_stack.push_back(nullptr);
    break;
  }
  (void)detail;
  return 0;
}

int on_leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
  auto *ctx = static_cast<MdGtk *>(userdata);
  if (!ctx->tag_stack.empty()) {
    ctx->tag_stack.pop_back();
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
  if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
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

} // namespace

void markdown_ensure_tags(GtkTextBuffer *buffer) {
  GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
  if (gtk_text_tag_table_lookup(table, "md_strong")) {
    return;
  }

  gtk_text_buffer_create_tag(buffer, "md_role", "weight", PANGO_WEIGHT_BOLD, "foreground", "#555555", nullptr);
  gtk_text_buffer_create_tag(buffer, "md_strong", "weight", PANGO_WEIGHT_BOLD, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_em", "style", PANGO_STYLE_ITALIC, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_code", "family", "Monospace", "background", "#E8E8E8", nullptr);
  gtk_text_buffer_create_tag(buffer, "md_pre", "family", "Monospace", "pixels-above-lines", 4, "pixels-below-lines", 4,
                             nullptr);
  gtk_text_buffer_create_tag(buffer, "md_html", "family", "Monospace", "foreground", "#606060", nullptr);
  gtk_text_buffer_create_tag(buffer, "md_link", "foreground", "#000080", "underline", PANGO_UNDERLINE_SINGLE, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_del", "strikethrough", TRUE, "foreground", "#707070", nullptr);
  gtk_text_buffer_create_tag(buffer, "md_quote", "left-margin", 12, "foreground", "#404040", nullptr);
  gtk_text_buffer_create_tag(buffer, "md_list", "left-margin", 6, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_th", "weight", PANGO_WEIGHT_BOLD, nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h1", "weight", PANGO_WEIGHT_BOLD, "scale",
                             static_cast<gdouble>(PANGO_SCALE_X_LARGE * 1.15), nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h2", "weight", PANGO_WEIGHT_BOLD, "scale",
                             static_cast<gdouble>(PANGO_SCALE_X_LARGE), nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h3", "weight", PANGO_WEIGHT_BOLD, "scale",
                             static_cast<gdouble>(PANGO_SCALE_LARGE), nullptr);
  gtk_text_buffer_create_tag(buffer, "md_h4", "weight", PANGO_WEIGHT_BOLD, "scale",
                             static_cast<gdouble>(PANGO_SCALE_MEDIUM), nullptr);
}

void markdown_insert(GtkTextBuffer *buffer, GtkTextIter *iter, const std::string &markdown) {
  markdown_ensure_tags(buffer);
  MdGtk ctx;
  ctx.buf = buffer;
  ctx.it = iter;

  MD_PARSER parser{};
  parser.abi_version = 0;
  parser.flags = MD_DIALECT_GITHUB;
  parser.enter_block = on_enter_block;
  parser.leave_block = on_leave_block;
  parser.enter_span = on_enter_span;
  parser.leave_span = on_leave_span;
  parser.text = on_text;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  md_parse(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()), &parser, &ctx);
}
