// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "note_editor.h"

#include <algorithm>
#include <string>
#include <vector>

#include <wx/dcclient.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <wx/tokenzr.h>

#include "md4c.h"

namespace {

// Paragraph kinds, stored as the paragraph style name.
const wxString kNormal = "Normaal";
const wxString kHeading = "Kop ";       // "Kop 1" to "Kop 6"
const wxString kList = "Lijst";
const wxString kNumbered = "Genummerd";
const wxString kTask = "Taak";
const wxString kQuote = "Citaat";
const wxString kCode = "Codeblok";
const wxString kRule = "Lijn";
const wxString kRaw = "Markdown";       // kept as Markdown text, such as a table

// Character styles.
const wxString kCodeSpan = "Code";
const wxString kRawSpan = "Markdown";

const wxString kUnchecked = wxString::FromUTF8("\xE2\x98\x90"); // ballot box
const wxString kChecked = wxString::FromUTF8("\xE2\x98\x91");   // ballot box with check

const int kListIndent = 60; // tenths of a millimetre per list level

bool IsListKind(const wxString &kind)
{
    return kind == kList || kind == kNumbered || kind == kTask;
}

// ---------------------------------------------------------------------------
// Markdown to paragraphs, with md4c's parser.

struct Run
{
    wxString text;
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool code = false;
    bool raw = false;      // Markdown that could not be shown, kept as it is
    wxString url;
    wxString image;        // source of an image; text is then its alt text
};

struct Para
{
    wxString kind = kNormal;
    int level = 1;         // heading level, or list depth
    bool checked = false;
    int number = 1;
    std::vector<Run> runs;
};

struct ListInfo
{
    bool ordered = false;
    int next = 1;
};

struct Builder
{
    std::vector<Para> paras;
    std::vector<ListInfo> lists;
    int current = -1;       // paragraph receiving text, -1 = none
    bool itemFresh = false; // a list item that has had no text yet
    int quote = 0;
    bool codeBlock = false;
    int bold = 0;
    int italic = 0;
    int strike = 0;
    int code = 0;
    std::vector<wxString> links;
    int image = 0;
    wxString imageSource;
    wxString imageAlt;

    int NewPara(const wxString &kind)
    {
        Para para;
        para.kind = kind;
        paras.push_back(para);
        current = static_cast<int>(paras.size()) - 1;
        return current;
    }

    void Add(const wxString &text)
    {
        if (text.IsEmpty())
            return;
        if (image > 0) {
            imageAlt << text;
            return;
        }
        if (current < 0)
            NewPara(quote > 0 ? kQuote : kNormal);

        Run run;
        run.text = text;
        run.bold = bold > 0;
        run.italic = italic > 0;
        run.strike = strike > 0;
        run.code = code > 0;
        if (!links.empty())
            run.url = links.back();

        std::vector<Run> &runs = paras[current].runs;
        if (!runs.empty() && runs.back().image.IsEmpty() && runs.back().bold == run.bold &&
            runs.back().italic == run.italic && runs.back().strike == run.strike &&
            runs.back().code == run.code && runs.back().url == run.url && !runs.back().raw)
            runs.back().text << text;
        else
            runs.push_back(run);
        itemFresh = false;
    }
};

wxString Attribute(const MD_ATTRIBUTE &attribute)
{
    return wxString::FromUTF8(attribute.text, attribute.size);
}

// &amp;, &#38; and the like; unknown ones stay as written.
wxString DecodeEntity(const wxString &entity)
{
    if (entity == "&amp;")
        return "&";
    if (entity == "&lt;")
        return "<";
    if (entity == "&gt;")
        return ">";
    if (entity == "&quot;")
        return "\"";
    if (entity == "&apos;")
        return "'";
    if (entity == "&nbsp;")
        return wxString(wxUniChar(0xA0));
    if (entity.StartsWith("&#") && entity.EndsWith(";")) {
        wxString number = entity.Mid(2, entity.length() - 3);
        unsigned long value = 0;
        const bool hex = number.StartsWith("x") || number.StartsWith("X");
        if ((hex ? number.Mid(1).ToULong(&value, 16) : number.ToULong(&value)) && value > 0 &&
            value < 0x110000)
            return wxString(wxUniChar(static_cast<wxUint32>(value)));
    }
    return entity;
}

int EnterBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    Builder &b = *static_cast<Builder *>(userdata);
    switch (type) {
    case MD_BLOCK_UL: {
        ListInfo list;
        b.lists.push_back(list);
        break;
    }
    case MD_BLOCK_OL: {
        ListInfo list;
        list.ordered = true;
        list.next = static_cast<int>(static_cast<MD_BLOCK_OL_DETAIL *>(detail)->start);
        b.lists.push_back(list);
        break;
    }
    case MD_BLOCK_LI: {
        const MD_BLOCK_LI_DETAIL *li = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
        ListInfo &list = b.lists.empty() ? *b.lists.insert(b.lists.end(), ListInfo()) : b.lists.back();
        const wxString kind = li->is_task ? kTask : (list.ordered ? kNumbered : kList);
        Para &para = b.paras[b.NewPara(kind)];
        para.level = static_cast<int>(b.lists.size());
        para.checked = li->is_task && (li->task_mark == 'x' || li->task_mark == 'X');
        para.number = list.next++;
        b.itemFresh = true;
        break;
    }
    case MD_BLOCK_H: {
        Para &para = b.paras[b.NewPara(kHeading)];
        para.level = std::min(6, static_cast<int>(static_cast<MD_BLOCK_H_DETAIL *>(detail)->level));
        break;
    }
    case MD_BLOCK_P:
        // The first paragraph of a list item is the item itself.
        if (b.current >= 0 && b.itemFresh)
            b.itemFresh = false;
        else
            b.NewPara(b.quote > 0 ? kQuote : kNormal);
        break;
    case MD_BLOCK_QUOTE:
        ++b.quote;
        break;
    case MD_BLOCK_CODE:
        b.NewPara(kCode);
        b.codeBlock = true;
        break;
    case MD_BLOCK_HR:
        b.NewPara(kRule);
        b.current = -1;
        break;
    default:
        break;
    }
    return 0;
}

int LeaveBlock(MD_BLOCKTYPE type, void *, void *userdata)
{
    Builder &b = *static_cast<Builder *>(userdata);
    switch (type) {
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (!b.lists.empty())
            b.lists.pop_back();
        break;
    case MD_BLOCK_LI:
        b.itemFresh = false;
        b.current = -1;
        break;
    case MD_BLOCK_QUOTE:
        --b.quote;
        break;
    case MD_BLOCK_CODE:
        // The line break after the last line made an empty paragraph.
        if (b.current >= 0 && b.paras[b.current].kind == kCode && b.paras[b.current].runs.empty())
            b.paras.pop_back();
        b.codeBlock = false;
        b.current = -1;
        break;
    case MD_BLOCK_P:
    case MD_BLOCK_H:
        b.current = -1;
        break;
    default:
        break;
    }
    return 0;
}

int EnterSpan(MD_SPANTYPE type, void *detail, void *userdata)
{
    Builder &b = *static_cast<Builder *>(userdata);
    switch (type) {
    case MD_SPAN_EM:
        ++b.italic;
        break;
    case MD_SPAN_STRONG:
        ++b.bold;
        break;
    case MD_SPAN_DEL:
        ++b.strike;
        break;
    case MD_SPAN_CODE:
        ++b.code;
        break;
    case MD_SPAN_A:
        b.links.push_back(Attribute(static_cast<MD_SPAN_A_DETAIL *>(detail)->href));
        break;
    case MD_SPAN_IMG:
        ++b.image;
        b.imageSource = Attribute(static_cast<MD_SPAN_IMG_DETAIL *>(detail)->src);
        b.imageAlt.Clear();
        break;
    default:
        break;
    }
    return 0;
}

int LeaveSpan(MD_SPANTYPE type, void *, void *userdata)
{
    Builder &b = *static_cast<Builder *>(userdata);
    switch (type) {
    case MD_SPAN_EM:
        --b.italic;
        break;
    case MD_SPAN_STRONG:
        --b.bold;
        break;
    case MD_SPAN_DEL:
        --b.strike;
        break;
    case MD_SPAN_CODE:
        --b.code;
        break;
    case MD_SPAN_A:
        if (!b.links.empty())
            b.links.pop_back();
        break;
    case MD_SPAN_IMG: {
        --b.image;
        if (b.current < 0)
            b.NewPara(b.quote > 0 ? kQuote : kNormal);
        Run run;
        run.image = b.imageSource;
        run.text = b.imageAlt;
        b.paras[b.current].runs.push_back(run);
        b.itemFresh = false;
        break;
    }
    default:
        break;
    }
    return 0;
}

int Text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    Builder &b = *static_cast<Builder *>(userdata);
    const wxString value = wxString::FromUTF8(text, size);
    switch (type) {
    case MD_TEXT_SOFTBR:
        b.Add(" ");
        break;
    case MD_TEXT_BR:
        b.Add(wxString(wxRichTextLineBreakChar));
        break;
    case MD_TEXT_ENTITY:
        b.Add(DecodeEntity(value));
        break;
    case MD_TEXT_NULLCHAR:
        b.Add(wxString(wxUniChar(0xFFFD)));
        break;
    case MD_TEXT_CODE:
        if (b.codeBlock) {
            // A code block arrives line by line; each line is a paragraph.
            wxString rest = value;
            while (true) {
                const int newline = rest.Find('\n');
                b.Add(newline == wxNOT_FOUND ? rest : rest.Left(newline));
                if (newline == wxNOT_FOUND)
                    break;
                b.NewPara(kCode);
                rest = rest.Mid(newline + 1);
            }
        }
        else {
            b.Add(value);
        }
        break;
    default:
        b.Add(value);
        break;
    }
    return 0;
}

// A table is shown as its Markdown lines: they are cut out before parsing
// and kept as paragraphs of their own.
bool IsTableLine(const wxString &line)
{
    wxString trimmed = line;
    trimmed.Trim(false);
    return trimmed.StartsWith("|");
}

void ParseInto(const std::string &markdown, Builder &builder)
{
    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
    parser.enter_block = EnterBlock;
    parser.leave_block = LeaveBlock;
    parser.enter_span = EnterSpan;
    parser.leave_span = LeaveSpan;
    parser.text = Text;
    md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &builder);
    builder.current = -1;
    builder.lists.clear();
    builder.quote = 0;
}

std::vector<Para> ParseMarkdown(const wxString &markdown)
{
    Builder builder;
    wxArrayString lines = wxSplit(markdown, '\n', '\0');

    wxString chunk;
    bool fenced = false;
    auto flush = [&]() {
        if (!chunk.IsEmpty()) {
            const wxScopedCharBuffer utf8 = chunk.utf8_str();
            ParseInto(std::string(utf8.data(), utf8.length()), builder);
            chunk.Clear();
        }
    };

    for (size_t i = 0; i < lines.GetCount(); ++i) {
        wxString line = lines[i];
        if (line.EndsWith("\r"))
            line.RemoveLast();
        wxString trimmed = line;
        trimmed.Trim(false);
        if (trimmed.StartsWith("```") || trimmed.StartsWith("~~~"))
            fenced = !fenced;

        if (!fenced && IsTableLine(line)) {
            flush();
            Para para;
            para.kind = kRaw;
            Run run;
            run.text = line;
            run.raw = true;
            para.runs.push_back(run);
            builder.paras.push_back(para);
            continue;
        }
        chunk << line << '\n';
    }
    flush();
    return builder.paras;
}

// ---------------------------------------------------------------------------
// Paragraphs back to Markdown.

// Characters with a meaning in Markdown get a backslash; not an underscore
// inside a word, as in "image_cropped", which Markdown leaves alone.
wxString Escape(const wxString &text)
{
    wxString out;
    const size_t length = text.length();
    for (size_t i = 0; i < length; ++i) {
        const wxUniChar c = text[i];
        const bool inWord = c == '_' && i > 0 && i + 1 < length && wxIsalnum(text[i - 1]) &&
                            wxIsalnum(text[i + 1]);
        if (!inWord &&
            (c == '\\' || c == '*' || c == '_' || c == '`' || c == '[' || c == ']'))
            out << '\\';
        out << c;
    }
    return out;
}

// A paragraph that starts like a heading, list or quote would become one.
wxString EscapeStart(const wxString &text)
{
    if (text.IsEmpty())
        return text;
    const wxUniChar first = text[0];
    if (first == '#' || first == '>' || first == '|')
        return "\\" + text;
    if ((first == '-' || first == '+') && text.length() > 1 && text[1] == ' ')
        return "\\" + text;
    size_t digits = 0;
    while (digits < text.length() && wxIsdigit(text[digits]))
        ++digits;
    if (digits > 0 && digits + 1 < text.length() && (text[digits] == '.' || text[digits] == ')') &&
        text[digits + 1] == ' ')
        return text.Left(digits) + "\\" + text.Mid(digits);
    return text;
}

struct Piece
{
    wxString text;
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool code = false;
    bool raw = false;
    wxString url;
};

wxString Marked(const Piece &piece)
{
    if (piece.raw)
        return piece.text;

    // Spaces at the edges go outside the marks: "** bold**" is no bold.
    size_t lead = 0;
    while (lead < piece.text.length() && piece.text[lead] == ' ')
        ++lead;
    size_t trail = 0;
    while (trail < piece.text.length() - lead &&
           piece.text[piece.text.length() - 1 - trail] == ' ')
        ++trail;
    const wxString core = piece.text.Mid(lead, piece.text.length() - lead - trail);
    if (core.IsEmpty())
        return piece.text;

    // A plain web address stays plain; Markdown makes it a link itself.
    if (!piece.url.IsEmpty() && core == piece.url && !piece.bold && !piece.italic &&
        !piece.strike && !piece.code &&
        (core.StartsWith("http://") || core.StartsWith("https://")))
        return piece.text;

    wxString inner;
    if (piece.code)
        inner = core.Contains("`") ? "`` " + core + " ``" : "`" + core + "`";
    else
        inner = Escape(core);
    if (piece.strike)
        inner = "~~" + inner + "~~";
    if (piece.italic)
        inner = "*" + inner + "*";
    if (piece.bold)
        inner = "**" + inner + "**";
    if (!piece.url.IsEmpty())
        inner = "[" + inner + "](" + piece.url + ")";
    return wxString(' ', lead) + inner + wxString(' ', trail);
}

} // namespace

// ---------------------------------------------------------------------------

NoteEditor::NoteEditor(wxWindow *parent, wxWindowID id)
    : wxRichTextCtrl(parent, id, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                     wxRE_MULTILINE | wxBORDER_NONE)
{
    m_base_size = GetFont().GetPointSize();
    m_mono_face = wxFont(m_base_size, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                         wxFONTWEIGHT_NORMAL).GetFaceName();

    wxRichTextAttr basic;
    basic.SetFont(GetFont());
    basic.SetTextColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    basic.SetParagraphSpacingAfter(20);
    SetBasicStyle(basic);
    SetMargins(12, 10);

    auto changed = [this](wxRichTextEvent &event) {
        event.Skip();
        Changed();
    };
    Bind(wxEVT_RICHTEXT_CONTENT_INSERTED, changed);
    Bind(wxEVT_RICHTEXT_CONTENT_DELETED, changed);
    Bind(wxEVT_RICHTEXT_STYLE_CHANGED, changed);
    Bind(wxEVT_RICHTEXT_PROPERTIES_CHANGED, changed);
    Bind(wxEVT_RICHTEXT_RETURN, &NoteEditor::OnReturn, this);
    Bind(wxEVT_LEFT_DOWN, &NoteEditor::OnLeftDown, this);
    // A web link is not followed while writing; a place goes to the chart.
    Bind(wxEVT_TEXT_URL, [this](wxTextUrlEvent &event) {
        wxRichTextAttr attr;
        if (GetStyle(event.GetURLStart(), attr) && attr.HasURL() &&
            attr.GetURL().StartsWith("geo:") && m_place)
            m_place(attr.GetURL());
    });
}

void NoteEditor::Changed()
{
    if (!m_loading && m_changed)
        m_changed();
}

void NoteEditor::SetColours(const wxColour &background, const wxColour &text,
                            const wxColour &link)
{
    SetBackgroundColour(background);
    wxRichTextAttr basic = GetBasicStyle();
    basic.SetTextColour(text);
    SetBasicStyle(basic);
    m_link_colour = link;
    m_muted_colour = wxColour((background.Red() + text.Red()) / 2,
                              (background.Green() + text.Green()) / 2,
                              (background.Blue() + text.Blue()) / 2);
    Refresh();
}

wxRichTextAttr NoteEditor::KindAttr(const wxString &kind, int level, bool checked,
                                    int number) const
{
    wxRichTextAttr attr;
    attr.SetParagraphStyleName(kind == kHeading ? kHeading + wxString::Format("%d", level) : kind);
    attr.SetLeftIndent(0, 0);
    attr.SetBulletStyle(wxTEXT_ATTR_BULLET_STYLE_NONE);
    attr.SetFontSize(m_base_size);
    attr.SetFontWeight(wxFONTWEIGHT_NORMAL);
    attr.SetFontStyle(wxFONTSTYLE_NORMAL);
    attr.SetParagraphSpacingBefore(0);
    attr.SetParagraphSpacingAfter(20);
    attr.SetAlignment(wxTEXT_ALIGNMENT_LEFT);

    if (kind == kHeading) {
        // From large to the size of the text; level 6 a little smaller.
        static const double scales[] = {1.6, 1.35, 1.18, 1.08, 1.0, 0.92};
        const double scale = scales[std::max(1, std::min(6, level)) - 1];
        attr.SetFontSize(static_cast<int>(m_base_size * scale + 0.5));
        attr.SetFontWeight(wxFONTWEIGHT_BOLD);
        attr.SetParagraphSpacingBefore(level == 1 ? 0 : 30);
        attr.SetParagraphSpacingAfter(15);
    }
    else if (IsListKind(kind)) {
        attr.SetLeftIndent(kListIndent * std::max(1, level), kListIndent);
        attr.SetParagraphSpacingAfter(8);
        if (kind == kList) {
            attr.SetBulletStyle(wxTEXT_ATTR_BULLET_STYLE_STANDARD);
            attr.SetBulletName("standard/circle");
        }
        else if (kind == kNumbered) {
            attr.SetBulletStyle(wxTEXT_ATTR_BULLET_STYLE_ARABIC | wxTEXT_ATTR_BULLET_STYLE_PERIOD);
            attr.SetBulletNumber(number);
        }
        else {
            attr.SetBulletStyle(wxTEXT_ATTR_BULLET_STYLE_SYMBOL);
            attr.SetBulletText(checked ? kChecked : kUnchecked);
        }
    }
    else if (kind == kQuote) {
        attr.SetLeftIndent(50, 0);
        attr.SetFontStyle(wxFONTSTYLE_ITALIC);
    }
    else if (kind == kCode || kind == kRaw) {
        attr.SetFontFaceName(m_mono_face);
        attr.SetParagraphSpacingAfter(0);
    }
    else if (kind == kRule) {
        attr.SetAlignment(wxTEXT_ALIGNMENT_CENTRE);
    }
    return attr;
}

void NoteEditor::ApplyKind(long from, long to, const wxString &kind, int level, bool checked,
                           int number)
{
    // An empty paragraph (a new line after Enter) cannot be styled through a
    // range; its attributes are set on the paragraph itself.
    if (to <= from) {
        if (wxRichTextParagraph *para = GetBuffer().GetParagraphAtPosition(from)) {
            para->SetAttributes(KindAttr(kind, level, checked, number));
            para->Invalidate(wxRICHTEXT_ALL);
            GetBuffer().Invalidate(wxRichTextRange(from, from));
            if (!m_loading) {
                LayoutContent();
                Refresh();
                Changed();
            }
            return;
        }
    }

    SetStyleEx(wxRichTextRange(from, std::max(from, to)), KindAttr(kind, level, checked, number),
               wxRICHTEXT_SETSTYLE_PARAGRAPHS_ONLY | wxRICHTEXT_SETSTYLE_RESET |
                   (m_loading ? 0 : wxRICHTEXT_SETSTYLE_WITH_UNDO));
}

wxImage NoteEditor::LoadImageFor(const wxString &source) const
{
    wxString path = source;
    if (path.StartsWith("<") && path.EndsWith(">"))
        path = path.Mid(1, path.length() - 2);
    path.Replace("%20", " ");
    if (path.StartsWith("http://") || path.StartsWith("https://"))
        return wxImage();

    wxFileName file(path, wxPATH_UNIX);
    if (!file.IsAbsolute())
        file.MakeAbsolute(m_base_directory);
    wxImage image;
    if (!file.FileExists() || !image.LoadFile(file.GetFullPath()))
        return wxImage();

    // Shown at most 320 pixels wide; the file itself is not changed.
    const int maxWidth = 320;
    if (image.GetWidth() > maxWidth) {
        const int height = image.GetHeight() * maxWidth / image.GetWidth();
        image.Rescale(maxWidth, std::max(1, height), wxIMAGE_QUALITY_HIGH);
    }
    return image;
}

void NoteEditor::InsertImageObject(const wxImage &image, const wxString &source,
                                   const wxString &alt)
{
    const long position = GetInsertionPoint();
    WriteImage(image, wxBITMAP_TYPE_PNG);
    wxRichTextObject *object = GetFocusObject()->GetLeafObjectAtPosition(position);
    if (wxRichTextImage *picture = wxDynamicCast(object, wxRichTextImage)) {
        picture->GetProperties().SetProperty("src", source);
        picture->GetProperties().SetProperty("alt", alt);
    }
}

void NoteEditor::LoadMarkdown(const wxString &markdown, const wxString &baseDirectory)
{
    m_base_directory = baseDirectory;
    m_loading = true;
    Freeze();
    Clear();
    // Nothing of the last note carries over, such as bold at its end.
    SetDefaultStyle(wxRichTextAttr());
    BeginSuppressUndo();

    const std::vector<Para> paras = ParseMarkdown(markdown);
    std::vector<std::pair<long, long>> ranges;

    for (size_t i = 0; i < paras.size(); ++i) {
        const Para &para = paras[i];
        const long start = GetInsertionPoint();

        if (para.kind == kRule)
            WriteText(wxString::FromUTF8("\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94"));

        for (const Run &run : para.runs) {
            if (!run.image.IsEmpty()) {
                const wxImage image = LoadImageFor(run.image);
                if (image.IsOk()) {
                    InsertImageObject(image, run.image, run.text);
                    continue;
                }
                // An image that cannot be shown stays as Markdown.
                wxRichTextAttr raw;
                raw.SetCharacterStyleName(kRawSpan);
                raw.SetFontFaceName(m_mono_face);
                raw.SetTextColour(m_muted_colour);
                BeginStyle(raw);
                WriteText("![" + run.text + "](" + run.image + ")");
                EndStyle();
                continue;
            }

            // Every mark said outright, also when off, so no style of the
            // text before runs on.
            wxRichTextAttr attr;
            attr.SetFontWeight(run.bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
            attr.SetFontStyle(run.italic ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL);
            attr.SetTextEffects(run.strike ? wxTEXT_ATTR_EFFECT_STRIKETHROUGH : 0);
            attr.SetTextEffectFlags(wxTEXT_ATTR_EFFECT_STRIKETHROUGH);
            if (run.code) {
                attr.SetCharacterStyleName(kCodeSpan);
                attr.SetFontFaceName(m_mono_face);
            }
            if (run.raw)
                attr.SetCharacterStyleName(kRawSpan);
            if (!run.url.IsEmpty()) {
                attr.SetURL(run.url);
                attr.SetFontUnderlined(true);
                attr.SetTextColour(m_link_colour);
            }
            BeginStyle(attr);
            WriteText(run.text);
            EndStyle();
        }

        ranges.push_back(std::make_pair(start, GetInsertionPoint()));
        if (i + 1 < paras.size())
            Newline();
    }

    for (size_t i = 0; i < paras.size(); ++i)
        ApplyKind(ranges[i].first, ranges[i].second, paras[i].kind, paras[i].level,
                  paras[i].checked, paras[i].number);

    EndSuppressUndo();
    SetInsertionPoint(0);
    SetDefaultStyle(wxRichTextAttr());
    Thaw();
    DiscardEdits();
    m_loading = false;
}

wxString NoteEditor::ToMarkdown() const
{
    wxString out;
    wxString previous;      // kind of the paragraph written last
    int previousLevel = 1;
    bool gap = false;       // an empty paragraph since then
    std::vector<int> numbers;

    const wxRichTextParagraphLayoutBox &buffer = const_cast<NoteEditor *>(this)->GetBuffer();
    for (wxRichTextObjectList::compatibility_iterator node = buffer.GetChildren().GetFirst();
         node; node = node->GetNext()) {
        const wxRichTextParagraph *para = wxDynamicCast(node->GetData(), wxRichTextParagraph);
        if (!para)
            continue;

        const wxRichTextAttr &pattr = para->GetAttributes();
        wxString kind = pattr.GetParagraphStyleName();
        int level = 1;
        if (kind.StartsWith(kHeading)) {
            long value = 1;
            kind.Mid(kHeading.length()).ToLong(&value);
            level = static_cast<int>(value);
            kind = kHeading;
        }
        if (IsListKind(kind) && pattr.HasLeftIndent())
            level = std::max(1, static_cast<int>((pattr.GetLeftIndent() + kListIndent / 2) / kListIndent));
        if (kind.IsEmpty())
            kind = kNormal;
        const bool verbatim = kind == kCode || kind == kRaw;

        // The inline content, as pieces with the same marks merged.
        std::vector<Piece> pieces;
        for (wxRichTextObjectList::compatibility_iterator child = para->GetChildren().GetFirst();
             child; child = child->GetNext()) {
            const wxRichTextObject *object = child->GetData();
            if (const wxRichTextImage *image = wxDynamicCast(object, wxRichTextImage)) {
                Piece piece;
                piece.raw = true;
                piece.text = "![" + image->GetProperties().GetPropertyString("alt") + "](" +
                             image->GetProperties().GetPropertyString("src") + ")";
                pieces.push_back(piece);
                continue;
            }
            const wxRichTextPlainText *plain = wxDynamicCast(object, wxRichTextPlainText);
            if (!plain)
                continue;
            const wxRichTextAttr &a = plain->GetAttributes();
            Piece piece;
            piece.text = plain->GetText();
            piece.raw = verbatim || a.GetCharacterStyleName() == kRawSpan;
            piece.code = !piece.raw && a.GetCharacterStyleName() == kCodeSpan;
            piece.bold = !piece.raw && kind != kHeading && a.HasFontWeight() &&
                         a.GetFontWeight() >= wxFONTWEIGHT_BOLD;
            piece.italic = !piece.raw && kind != kQuote && a.HasFontItalic() &&
                           a.GetFontStyle() == wxFONTSTYLE_ITALIC;
            piece.strike = !piece.raw && a.HasTextEffects() &&
                           (a.GetTextEffects() & wxTEXT_ATTR_EFFECT_STRIKETHROUGH);
            if (!piece.raw && a.HasURL())
                piece.url = a.GetURL();
            if (!pieces.empty() && pieces.back().bold == piece.bold &&
                pieces.back().italic == piece.italic && pieces.back().strike == piece.strike &&
                pieces.back().code == piece.code && pieces.back().raw == piece.raw &&
                pieces.back().url == piece.url && !(piece.raw && !verbatim))
                pieces.back().text << piece.text;
            else
                pieces.push_back(piece);
        }

        wxString body;
        for (const Piece &piece : pieces)
            body << Marked(piece);

        wxString check = body;
        check.Trim(true).Trim(false);
        if (check.IsEmpty() && kind != kRule && kind != kCode) {
            if (!previous.IsEmpty())
                gap = true;
            continue;
        }
        if (kind != kHeading && kind != kCode && kind != kRaw && kind != kRule && !body.IsEmpty() &&
            pieces.front().raw == false)
            body = EscapeStart(body);

        // What separates it from the paragraph before.
        // A list of another kind starts after an empty line; nested items
        // belong to the list around them.
        const bool sameList = IsListKind(kind) && IsListKind(previous) &&
                              (kind == previous || level > 1 || previousLevel > 1);
        const bool sameGroup =
            !gap && (sameList || (kind == previous &&
                                  (kind == kQuote || kind == kCode || kind == kRaw)));
        const bool closeCode = previous == kCode && (kind != kCode || gap);
        const bool openCode = kind == kCode && (previous != kCode || gap);
        if (closeCode)
            out << "\n```";
        if (!previous.IsEmpty())
            out << (sameGroup ? "\n" : "\n\n");
        if (openCode)
            out << "```\n";

        // Numbering restarts with every list.
        if (!sameList || gap)
            numbers.clear();
        if (static_cast<int>(numbers.size()) < level)
            numbers.resize(level, 0);
        numbers.resize(level);

        const wxString indent(' ', 2 * (level - 1));
        wxString prefix;
        if (kind == kHeading)
            prefix = wxString('#', level) + " ";
        else if (kind == kList)
            prefix = indent + "- ";
        else if (kind == kNumbered) {
            // A list keeps the number it starts with, such as 5.
            if (numbers[level - 1] == 0 && pattr.HasBulletNumber() && pattr.GetBulletNumber() > 0)
                numbers[level - 1] = pattr.GetBulletNumber() - 1;
            prefix = indent + wxString::Format("%d. ", ++numbers[level - 1]);
        }
        else if (kind == kTask)
            prefix = indent + (pattr.GetBulletText() == kChecked ? "- [x] " : "- [ ] ");
        else if (kind == kQuote)
            prefix = "> ";

        if (kind == kRule) {
            body = "---";
        }
        else {
            // A line break inside a paragraph, made with Shift+Enter.
            const wxString continuation =
                kind == kQuote ? wxString("> ") : wxString(' ', prefix.length());
            body.Replace(wxString(wxRichTextLineBreakChar), "\\\n" + continuation);
        }

        out << prefix << body;
        previous = kind;
        previousLevel = level;
        gap = false;
    }

    if (previous == kCode)
        out << "\n```";
    if (!out.IsEmpty())
        out << "\n";
    return out;
}

std::vector<wxRichTextParagraph *> NoteEditor::SelectedParagraphs() const
{
    std::vector<wxRichTextParagraph *> result;
    const wxRichTextParagraphLayoutBox &buffer = const_cast<NoteEditor *>(this)->GetBuffer();
    long from = GetInsertionPoint();
    long to = from;
    if (HasSelection()) {
        const wxRichTextRange range = GetSelectionRange();
        from = range.GetStart();
        to = range.GetEnd();
    }
    for (long pos = from; pos <= to;) {
        wxRichTextParagraph *para = buffer.GetParagraphAtPosition(pos);
        if (!para)
            break;
        if (result.empty() || result.back() != para)
            result.push_back(para);
        pos = para->GetRange().GetEnd() + 1;
    }
    return result;
}

int NoteEditor::HeadingLevel() const
{
    const std::vector<wxRichTextParagraph *> paras = SelectedParagraphs();
    if (paras.empty())
        return 0;
    const wxString name = paras.front()->GetAttributes().GetParagraphStyleName();
    if (!name.StartsWith(kHeading))
        return 0;
    long level = 0;
    name.Mid(kHeading.length()).ToLong(&level);
    return static_cast<int>(level);
}

void NoteEditor::SetHeading(int level)
{
    const std::vector<wxRichTextParagraph *> paras = SelectedParagraphs();
    if (paras.empty())
        return;
    BeginBatchUndo("Kop");
    for (wxRichTextParagraph *para : paras)
        ApplyKind(para->GetRange().GetStart(), para->GetRange().GetEnd(),
                  level > 0 ? kHeading : kNormal, std::max(1, std::min(6, level)));
    EndBatchUndo();
    SetFocus();
}

void NoteEditor::ToggleBold()
{
    ApplyBoldToSelection();
    SetFocus();
}

void NoteEditor::ToggleItalic()
{
    ApplyItalicToSelection();
    SetFocus();
}

void NoteEditor::ToggleList()
{
    const std::vector<wxRichTextParagraph *> paras = SelectedParagraphs();
    if (paras.empty())
        return;
    const bool all = std::all_of(paras.begin(), paras.end(), [](wxRichTextParagraph *para) {
        return para->GetAttributes().GetParagraphStyleName() == kList;
    });
    BeginBatchUndo("Lijst");
    for (wxRichTextParagraph *para : paras)
        ApplyKind(para->GetRange().GetStart(), para->GetRange().GetEnd(), all ? kNormal : kList);
    EndBatchUndo();
    SetFocus();
}

// A numbered list; it carries on from a numbered item just above.
void NoteEditor::ToggleNumbered()
{
    const std::vector<wxRichTextParagraph *> paras = SelectedParagraphs();
    if (paras.empty())
        return;
    const bool all = std::all_of(paras.begin(), paras.end(), [](wxRichTextParagraph *para) {
        return para->GetAttributes().GetParagraphStyleName() == kNumbered;
    });

    int number = 1;
    const long before = paras.front()->GetRange().GetStart() - 1;
    if (before >= 0) {
        const wxRichTextParagraph *above = GetBuffer().GetParagraphAtPosition(before);
        if (above && above != paras.front() &&
            above->GetAttributes().GetParagraphStyleName() == kNumbered)
            number = above->GetAttributes().GetBulletNumber() + 1;
    }

    BeginBatchUndo("Genummerd");
    for (wxRichTextParagraph *para : paras)
        ApplyKind(para->GetRange().GetStart(), para->GetRange().GetEnd(),
                  all ? kNormal : kNumbered, 1, false, number++);
    EndBatchUndo();
    SetFocus();
}

void NoteEditor::ToggleTasks()
{
    const std::vector<wxRichTextParagraph *> paras = SelectedParagraphs();
    if (paras.empty())
        return;
    const bool all = std::all_of(paras.begin(), paras.end(), [](wxRichTextParagraph *para) {
        return para->GetAttributes().GetParagraphStyleName() == kTask;
    });
    BeginBatchUndo("Afvinken");
    for (wxRichTextParagraph *para : paras)
        ApplyKind(para->GetRange().GetStart(), para->GetRange().GetEnd(), all ? kNormal : kTask);
    EndBatchUndo();
    SetFocus();
}

void NoteEditor::AddLink(const wxString &address)
{
    if (address.IsEmpty())
        return;
    wxRichTextAttr attr;
    attr.SetURL(address);
    attr.SetFontUnderlined(true);
    attr.SetTextColour(m_link_colour);
    if (HasSelection()) {
        SetStyleEx(GetSelectionRange(), attr,
                   wxRICHTEXT_SETSTYLE_WITH_UNDO | wxRICHTEXT_SETSTYLE_CHARACTERS_ONLY);
    }
    else {
        BeginStyle(attr);
        WriteText(address);
        EndStyle();
    }
    SetFocus();
}

void NoteEditor::AddLinkWithText(const wxString &address, const wxString &text)
{
    if (HasSelection()) {
        AddLink(address);
        return;
    }
    wxRichTextAttr attr;
    attr.SetURL(address);
    attr.SetFontUnderlined(true);
    attr.SetTextColour(m_link_colour);
    BeginStyle(attr);
    WriteText(text);
    EndStyle();
    SetFocus();
}

bool NoteEditor::InsertNoteImage(const wxString &source, const wxString &alt)
{
    const wxImage image = LoadImageFor(source);
    if (!image.IsOk())
        return false;
    if (HasSelection())
        DeleteSelection();
    InsertImageObject(image, source, alt);
    SetFocus();
    return true;
}

// A new line after a heading is normal text; Enter on an empty list item
// ends the list; a new task is not yet ticked off.
void NoteEditor::OnReturn(wxRichTextEvent &event)
{
    event.Skip();
    CallAfter([this]() { AfterNewParagraph(); });
}

// The paragraph with the cursor was just made with Enter: it copied the
// kind of the one before. After a heading it is normal text; an empty list
// item before it ends the list; a new task is not yet ticked off.
void NoteEditor::AfterNewParagraph()
{
    wxRichTextParagraphLayoutBox &buffer = GetBuffer();
    // The caret position is one less than the position of the character
    // after the caret: the new paragraph starts at caret + 1.
    wxRichTextParagraph *now = buffer.GetParagraphAtPosition(GetCaretPosition() + 1);
    if (!now)
        return;
    wxRichTextObjectList::compatibility_iterator node = buffer.GetChildren().Find(now);
    if (!node || !node->GetPrevious())
        return;
    wxRichTextParagraph *before = wxDynamicCast(node->GetPrevious()->GetData(), wxRichTextParagraph);
    if (!before)
        return;

    const wxString kind = before->GetAttributes().GetParagraphStyleName();
    const long nowStart = now->GetRange().GetStart();
    const long nowEnd = now->GetRange().GetEnd();
    const long caret = GetCaretPosition();

    if (kind.StartsWith(kHeading)) {
        ApplyKind(nowStart, nowEnd, kNormal);
    }
    else if (IsListKind(kind)) {
        wxString text = before->GetTextForRange(before->GetRange());
        text.Trim(true).Trim(false);
        if (text.IsEmpty()) {
            ApplyKind(before->GetRange().GetStart(), before->GetRange().GetEnd(), kNormal);
            ApplyKind(nowStart, nowEnd, kNormal);
        }
        else if (kind == kTask) {
            const int level = std::max(
                1, static_cast<int>((before->GetAttributes().GetLeftIndent() + kListIndent / 2) /
                                    kListIndent));
            ApplyKind(nowStart, nowEnd, kTask, level, false);
        }
    }
    // Typing goes on in the style of the paragraph now.
    SetCaretPosition(caret);
    SetDefaultStyleToCursorStyle();
}

// A click on the box of a task ticks it off, or on again.
void NoteEditor::OnLeftDown(wxMouseEvent &event)
{
    event.Skip();
    long position = 0;
    const wxPoint logical = CalcUnscrolledPosition(event.GetPosition());
    if (HitTest(event.GetPosition(), &position) == wxTE_HT_UNKNOWN)
        return;
    wxRichTextParagraph *para = GetBuffer().GetParagraphAtPosition(position);
    if (!para || para->GetAttributes().GetParagraphStyleName() != kTask)
        return;

    // The box sits in the indent before the text.
    wxClientDC dc(this);
    const int indent = para->GetAttributes().GetLeftIndent() * dc.GetPPI().x / 254;
    if (logical.x > para->GetPosition().x + indent + 4)
        return;

    const bool checked = para->GetAttributes().GetBulletText() == kChecked;
    const int level =
        std::max(1, static_cast<int>((para->GetAttributes().GetLeftIndent() + kListIndent / 2) / kListIndent));
    ApplyKind(para->GetRange().GetStart(), para->GetRange().GetEnd(), kTask, level, !checked);
    event.Skip(false);
}

// ---------------------------------------------------------------------------
// Icons of the buttons.

namespace {

wxImage DrawToolIcon(const wxString &name, const wxColour &colour, int size)
{
    wxImage image(size, size);
    image.InitAlpha();
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            image.SetAlpha(x, y, 0);

    wxGraphicsContext *gc = wxGraphicsContext::Create(image);
    if (!gc)
        return image;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const double s = size;
    const double line = s * 0.09;
    wxGraphicsPen pen = gc->CreatePen(wxGraphicsPenInfo(colour).Width(line).Cap(wxCAP_ROUND)
                                  .Join(wxJOIN_ROUND));
    gc->SetPen(pen);
    gc->SetBrush(wxBrush(colour));

    auto letter = [&](const wxString &text, wxFontFamily family, bool bold, bool italic,
                      double height, double cx, double cy) {
        wxFontInfo info(height);
        info.Family(family);
        if (bold)
            info.Bold();
        if (italic)
            info.Italic();
        gc->SetFont(wxFont(info), colour);
        double w = 0, h = 0, descent = 0, leading = 0;
        gc->GetTextExtent(text, &w, &h, &descent, &leading);
        gc->DrawText(text, cx - w / 2.0, cy - (h - descent) / 2.0 - descent * 0.25);
    };
    auto stroke = [&](double x1, double y1, double x2, double y2) {
        gc->StrokeLine(x1 * s, y1 * s, x2 * s, y2 * s);
    };

    if (name == "heading") {
        letter("H", wxFONTFAMILY_SWISS, true, false, s * 1.0, s / 2, s / 2);
    }
    else if (name == "bold") {
        letter("B", wxFONTFAMILY_ROMAN, true, false, s * 1.05, s / 2, s / 2);
    }
    else if (name == "italic") {
        letter("I", wxFONTFAMILY_ROMAN, true, true, s * 1.05, s / 2, s / 2);
    }
    else if (name == "bullets" || name == "numbers") {
        for (int i = 0; i < 3; ++i) {
            const double y = 0.24 + 0.26 * i;
            if (name == "bullets") {
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->DrawEllipse(0.13 * s, y * s - 0.075 * s, 0.15 * s, 0.15 * s);
                gc->SetPen(pen);
            }
            else {
                letter(wxString::Format("%d", i + 1), wxFONTFAMILY_SWISS, true, false, s * 0.52,
                       0.2 * s, y * s);
            }
            stroke(0.42, y, 0.86, y);
        }
    }
    else if (name == "tasks") {
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->DrawRoundedRectangle(0.16 * s, 0.16 * s, 0.68 * s, 0.68 * s, 0.12 * s);
        wxGraphicsPath check = gc->CreatePath();
        check.MoveToPoint(0.32 * s, 0.5 * s);
        check.AddLineToPoint(0.45 * s, 0.64 * s);
        check.AddLineToPoint(0.69 * s, 0.36 * s);
        gc->StrokePath(check);
    }
    else if (name == "link") {
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->Translate(s / 2, s / 2);
        gc->Rotate(-3.14159265358979323846 / 4);
        const double w = 0.46 * s, h = 0.26 * s;
        gc->DrawRoundedRectangle(-0.4 * s, -h / 2, w, h, h / 2);
        gc->DrawRoundedRectangle(-0.06 * s, -h / 2, w, h, h / 2);
    }
    else if (name == "edit") {
        // A pencil, point down to the left.
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(s * 0.075).Join(wxJOIN_ROUND)));
        gc->Translate(s / 2, s / 2);
        gc->Rotate(3.0 * 3.14159265358979323846 / 4.0);
        const double h = 0.2 * s;
        wxGraphicsPath pencil = gc->CreatePath();
        pencil.MoveToPoint(-0.38 * s, -h / 2);
        pencil.AddLineToPoint(0.2 * s, -h / 2);
        pencil.AddLineToPoint(0.4 * s, 0);
        pencil.AddLineToPoint(0.2 * s, h / 2);
        pencil.AddLineToPoint(-0.38 * s, h / 2);
        pencil.CloseSubpath();
        gc->StrokePath(pencil);
        gc->StrokeLine(-0.24 * s, -h / 2, -0.24 * s, h / 2);
    }
    else if (name == "new") {
        stroke(0.5, 0.2, 0.5, 0.8);
        stroke(0.2, 0.5, 0.8, 0.5);
    }
    else if (name == "rename") {
        // A text field with the text cursor in it.
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(s * 0.07).Join(wxJOIN_ROUND)));
        gc->DrawRoundedRectangle(0.08 * s, 0.3 * s, 0.84 * s, 0.4 * s, 0.07 * s);
        gc->SetPen(pen);
        stroke(0.22, 0.5, 0.44, 0.5);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(s * 0.07).Cap(wxCAP_ROUND)));
        stroke(0.63, 0.16, 0.63, 0.84);
        stroke(0.55, 0.16, 0.71, 0.16);
        stroke(0.55, 0.84, 0.71, 0.84);
    }
    else if (name == "trash") {
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(s * 0.075).Cap(wxCAP_ROUND)
                                     .Join(wxJOIN_ROUND)));
        stroke(0.18, 0.27, 0.82, 0.27);
        wxGraphicsPath handle = gc->CreatePath();
        handle.MoveToPoint(0.4 * s, 0.27 * s);
        handle.AddLineToPoint(0.4 * s, 0.16 * s);
        handle.AddLineToPoint(0.6 * s, 0.16 * s);
        handle.AddLineToPoint(0.6 * s, 0.27 * s);
        gc->StrokePath(handle);
        wxGraphicsPath bin = gc->CreatePath();
        bin.MoveToPoint(0.26 * s, 0.33 * s);
        bin.AddLineToPoint(0.32 * s, 0.85 * s);
        bin.AddLineToPoint(0.68 * s, 0.85 * s);
        bin.AddLineToPoint(0.74 * s, 0.33 * s);
        gc->StrokePath(bin);
        stroke(0.44, 0.43, 0.45, 0.74);
        stroke(0.56, 0.43, 0.55, 0.74);
    }
    else if (name == "place") {
        // A map pin: a round head with a point below and a hole in it.
        gc->SetBrush(wxBrush(colour));
        gc->SetPen(*wxTRANSPARENT_PEN);
        wxGraphicsPath pin = gc->CreatePath();
        const double cx = 0.5 * s;
        const double cy = 0.4 * s;
        const double r = 0.27 * s;
        pin.MoveToPoint(cx, 0.9 * s);
        pin.AddArc(cx, cy, r, 3.14159265358979323846 * 0.75, 3.14159265358979323846 * 0.25, true);
        pin.CloseSubpath();
        pin.AddCircle(cx, cy, r * 0.42);
        gc->FillPath(pin, wxODDEVEN_RULE);
    }
    else if (name == "image") {
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(s * 0.07).Join(wxJOIN_ROUND)));
        gc->DrawRoundedRectangle(0.12 * s, 0.2 * s, 0.76 * s, 0.6 * s, 0.08 * s);
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(colour));
        gc->DrawEllipse(0.6 * s, 0.3 * s, 0.13 * s, 0.13 * s);
        wxGraphicsPath hills = gc->CreatePath();
        hills.MoveToPoint(0.18 * s, 0.75 * s);
        hills.AddLineToPoint(0.4 * s, 0.47 * s);
        hills.AddLineToPoint(0.58 * s, 0.67 * s);
        hills.AddLineToPoint(0.68 * s, 0.57 * s);
        hills.AddLineToPoint(0.82 * s, 0.75 * s);
        hills.CloseSubpath();
        gc->FillPath(hills);
    }

    delete gc;
    return image;
}

} // namespace

NoteToolBitmap NoteToolIcon(const wxString &name, const wxColour &colour)
{
#if wxCHECK_VERSION(3, 1, 6)
    return wxBitmapBundle::FromBitmaps(wxBitmap(DrawToolIcon(name, colour, 18)),
                                       wxBitmap(DrawToolIcon(name, colour, 36)));
#else
    return wxBitmap(DrawToolIcon(name, colour, 18));
#endif
}

wxImage NoteToolImage(const wxString &name, const wxColour &colour, int size)
{
    return DrawToolIcon(name, colour, size);
}
