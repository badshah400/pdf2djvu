/* Copyright © 2007-2015 Jakub Wilk <jwilk@jwilk.net>
 * Copyright © 2009 Mateusz Turcza
 *
 * This file is part of pdfdjvu.
 *
 * pdf2djvu is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * pdf2djvu is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "config.hh"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <getopt.h>

#include "autoconf.hh"
#include "debug.hh"
#include "djvu-const.hh"
#include "i18n.hh"
#include "system.hh"

string_format::Template* Config::default_page_id_template(const std::string &prefix)
{
  return new string_format::Template(prefix + "{spage:04*}.djvu");
}

Config::Config()
{
  this->text = this->TEXT_WORDS;
  this->text_nfkc = true;
  this->text_crop = false;
  this->format = this->FORMAT_BUNDLED;
  this->output_stdout = true;
  this->verbose = 1;
  this->dpi = 300;
  this->guess_dpi = false;
  this->preferred_page_size = std::make_pair(0, 0);
  this->use_media_box = false;
  this->bg_subsample = 3;
  this->fg_colors = this->FG_COLORS_DEFAULT;
  this->antialias = false;
  this->extract_metadata = true;
  this->adjust_metadata = true;
  this->extract_outline = true;
  this->no_render = false;
  this->monochrome = false;
  this->loss_level = 0;
  this->bg_slices = NULL;
  this->page_id_template.reset(default_page_id_template("p"));
  this->page_title_template.reset(new string_format::Template(""));
  this->n_jobs = 1;
}

namespace string
{
  static void split(const std::string &, char, std::vector<std::string> &);

  static void replace(std::string &, char, char);

  template <typename tp>
  tp as(const std::string &);
}

static void string::split(const std::string &s, char c, std::vector<std::string> &result)
{
  size_t lpos = 0;
  while (true)
  {
    size_t rpos = s.find(c, lpos);
    result.push_back(s.substr(lpos, rpos - lpos));
    if (rpos == std::string::npos)
      break;
    else
      lpos = rpos + 1;
  }
}

static void string::replace(std::string &s, char c1, char c2)
{
  size_t lpos = 0;
  while (true)
  {
    size_t rpos = s.find(c1, lpos);
    if (rpos == std::string::npos)
      break;
    s[rpos] = c2;
    lpos = rpos + 1;
  }
}

static void parse_hyperlinks_options(std::string s, Config::Hyperlinks &options)
{
  std::vector<std::string> split;
  string::replace(s, '_', '-');
  string::split(s, ',', split);
  for (std::vector<std::string>::const_iterator it = split.begin(); it != split.end(); it++)
  {
    if (*it == "border-avis")
    {
      options.border_always_visible = true;
      continue;
    }
    else if (*it == "no" || *it == "none")
    {
      options.extract = false;
      continue;
    }
    else if
    (
      it->length() == 7 &&
      (*it)[0] == '#' &&
      it->find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos
    )
    {
      options.border_color = *it;
      continue;
    }
    throw Config::Error(_("Unable to parse hyperlinks options"));
  }
}

static void bad_pages()
{
  throw Config::Error(_("Unable to parse page numbers"));
}

static void parse_pages(const std::string &s, std::vector< std::pair<int, int> > &result)
{
  int state = 0;
  int value[2] = { 0, 0 };
  for (std::string::const_iterator it = s.begin(); it != s.end(); it++)
  {
    if (('0' <= *it) && (*it <= '9'))
    {
      value[state] = value[state] * 10 + (int)(*it - '0');
      if (state == 0)
        value[1] = value[0];
    }
    else if (state == 0 && *it == '-')
    {
      state = 1;
      value[1] = 0;
    }
    else if (*it == ',')
    {
      if (value[0] < 1 || value[1] < 1 || value[0] > value[1])
        bad_pages();
      result.push_back(std::make_pair(value[0], value[1]));
      value[0] = value[1] = 0;
      state = 0;
    }
    else
      bad_pages();
  }
  if (state == 0)
    value[1] = value[0];
  if (value[0] < 1 || value[1] < 1 || value[0] > value[1])
    bad_pages();
  result.push_back(std::make_pair(value[0], value[1]));
}

static std::pair<int, int>parse_page_size(const std::string &s)
{
  std::istringstream stream(s);
  int x, y;
  char c;
  stream >> x >> c >> y;
  if (x > 0 &&  y > 0 && c == 'x' && stream.eof() && !stream.fail())
    return std::make_pair(x, y);
  else
    throw Config::Error(_("Unable to parse page size"));
}

template <typename tp>
tp string::as(const std::string &s)
{
  tp n;
  std::istringstream stream(s);
  stream >> n;
  if (stream.fail() || !stream.eof())
    throw Config::Error(string_printf(
      _("\"%s\" is not a valid number"),
      s.c_str())
    );
  return n;
}

static unsigned int parse_fg_colors(const std::string &s)
{
  if (s == "web")
    return Config::FG_COLORS_WEB;
  else if (s == "default")
    return Config::FG_COLORS_DEFAULT;
  else if (s == "black")
    return Config::FG_COLORS_BLACK;
  long n = string::as<long>(s);
  if (n < 1 || n > static_cast<long>(djvu::max_fg_colors))
    throw Config::Error(string_printf(
      _("The specified number of foreground colors is outside the allowed range: %u .. %u"),
      1U, djvu::max_fg_colors
    ));
  return n;
}

static unsigned int parse_bg_subsample(const std::string &s)
{
  long n = string::as<long>(optarg);
  if (n < 1 || n > static_cast<long>(djvu::max_subsample_ratio))
    throw Config::Error(string_printf(
      _("The specified subsampling ratio is outside the allowed range: %u .. %u"),
      1, djvu::max_subsample_ratio
    ));
  return n;
}

static void validate_page_id_template(string_format::Template &page_id_template)
{
  string_format::Bindings bindings;
  bindings["max_spage"] = 0U;
  bindings["spage"] = 0U;
  bindings["max_page"] = 0U;
  bindings["page"] = 0U;
  bindings["max_dpage"] = 0U;
  bindings["dpage"] = 0U;
  std::string page_id = page_id_template.format(bindings);
  size_t length = page_id.length();
  bool dot_allowed = false;
  bool pm_allowed = false;
  for (std::string::const_iterator it = page_id.begin(); it != page_id.end(); it++)
  {
    if (!pm_allowed && (*it == '+' || *it == '-'))
    {
      throw Config::Error(_("Page identifier cannot start with a '+' or a '-' character"));
    }
    if (*it == '.')
    {
      if (!dot_allowed)
        throw Config::Error(_("Page identifier cannot start with a '.' character or contain two consecutive '.' characters"));
      dot_allowed = false;
    }
    else if ((*it >= 'a' && *it <= 'z') || (*it >= '0' && *it <= '9') || *it == '_' || *it == '-' || *it == '+')
      dot_allowed = true;
    else
      throw Config::Error(_("Page identifier must consist only of letters, digits, '_', '+', '-' and '.' characters"));
    pm_allowed = true;
  }
  if (length >= 4 && page_id.substr(length - 4) == ".djv")
    ;
  else if (length >= 5 && page_id.substr(length - 5) == ".djvu")
    ;
  else
    throw Config::Error(_("Page identifier must end with the '.djvu' or the '.djv' extension"));
}

void Config::read_config(int argc, char * const argv[])
{
  enum
  {
    OPT_DPI = 'd',
    OPT_HELP = 'h',
    OPT_INDIRECT = 'i',
    OPT_JOBS = 'j',
    OPT_OUTPUT = 'o',
    OPT_PAGES = 'p',
    OPT_QUIET = 'q',
    OPT_VERBOSE = 'v',
    OPT_DUMMY = CHAR_MAX,
    OPT_ANTIALIAS,
    OPT_BG_SLICES,
    OPT_BG_SUBSAMPLE,
    OPT_FG_COLORS,
    OPT_GUESS_DPI,
    OPT_HYPERLINKS,
    OPT_LOSS_100,
    OPT_LOSS_ANY,
    OPT_MEDIA_BOX,
    OPT_MONOCHROME,
    OPT_NO_HLINKS,
    OPT_NO_METADATA,
    OPT_NO_OUTLINE,
    OPT_NO_RENDER,
    OPT_PAGE_ID_PREFIX,
    OPT_PAGE_ID_TEMPLATE,
    OPT_PAGE_SIZE,
    OPT_PAGE_TITLE_TEMPLATE,
    OPT_TEXT_CROP,
    OPT_TEXT_FILTER,
    OPT_TEXT_LINES,
    OPT_TEXT_NONE,
    OPT_TEXT_NO_NFKC,
    OPT_TEXT_WORDS,
    OPT_VERBATIM_METADATA,
    OPT_VERSION,
  };
  static struct option options [] =
  {
    { "anti-alias", 0, 0, OPT_ANTIALIAS },
    { "antialias", 0, 0, OPT_ANTIALIAS }, /* deprecated alias */
    { "bg-slices", 1, 0, OPT_BG_SLICES },
    { "bg-subsample", 1, 0, OPT_BG_SUBSAMPLE },
    { "crop-text", 0, 0, OPT_TEXT_CROP },
    { "dpi", 1, 0, OPT_DPI },
    { "fg-colors", 1, 0, OPT_FG_COLORS },
    { "filter-text", 1, 0, OPT_TEXT_FILTER },
    { "guess-dpi", 0, 0, OPT_GUESS_DPI },
    { "help", 0, 0, OPT_HELP },
    { "hyperlinks", 1, 0, OPT_HYPERLINKS },
    { "indirect", 1, 0, OPT_INDIRECT },
    { "jobs", 1, 0, OPT_JOBS },
    { "lines", 0, 0, OPT_TEXT_LINES },
    { "loss-level", 1, 0, OPT_LOSS_ANY },
    { "losslevel", 1, 0, OPT_LOSS_ANY }, /* deprecated alias */
    { "lossy", 0, 0, OPT_LOSS_100 },
    { "media-box", 0, 0, OPT_MEDIA_BOX },
    { "monochrome", 0, 0, OPT_MONOCHROME },
    { "no-hyperlinks", 0, 0, OPT_NO_HLINKS },
    { "no-metadata", 0, 0, OPT_NO_METADATA },
    { "no-nfkc", 0, 0, OPT_TEXT_NO_NFKC },
    { "no-outline", 0, 0, OPT_NO_OUTLINE },
    { "no-render", 0, 0, OPT_NO_RENDER },
    { "no-text", 0, 0, OPT_TEXT_NONE },
    { "output", 1, 0, OPT_OUTPUT },
    { "page-id-prefix", 1, 0, OPT_PAGE_ID_PREFIX },
    { "page-id-template", 1, 0, OPT_PAGE_ID_TEMPLATE },
    { "page-size", 1, 0, OPT_PAGE_SIZE },
    { "page-title-template", 1, 0, OPT_PAGE_TITLE_TEMPLATE },
    { "pageid-prefix", 1, 0, OPT_PAGE_ID_PREFIX }, /* deprecated alias */
    { "pageid-template", 1, 0, OPT_PAGE_ID_TEMPLATE }, /* deprecated alias */
    { "pages", 1, 0, OPT_PAGES },
    { "quiet", 0, 0, OPT_QUIET },
    { "verbatim-metadata", 0, 0, OPT_VERBATIM_METADATA },
    { "verbose", 0, 0, OPT_VERBOSE },
    { "version", 0, 0, OPT_VERSION },
    { "words", 0, 0, OPT_TEXT_WORDS },
    { NULL, 0, 0, '\0' }
  };
  int optindex, c;
  while (true)
  {
    optindex = 0;
    c = getopt_long(argc, argv, "i:o:d:qvp:j:h", options, &optindex);
    if (c < 0)
      break;
    if (c == 0)
      throw Config::Error(_("Unable to parse command-line options"));
    switch (c)
    {
    case OPT_DPI:
      this->dpi = string::as<int>(optarg);
      if (this->dpi < djvu::min_dpi || this->dpi > djvu::max_dpi)
        throw Config::Error(string_printf(
          _("The specified resolution is outside the allowed range: %d .. %d"),
          djvu::min_dpi, djvu::max_dpi
        ));
      break;
    case OPT_GUESS_DPI:
      this->guess_dpi = true;
      break;
    case OPT_PAGE_SIZE:
      this->preferred_page_size = parse_page_size(optarg);
      break;
    case OPT_MEDIA_BOX:
      this->use_media_box = true;
      break;
    case OPT_QUIET:
      this->verbose = 0;
      break;
    case OPT_VERBOSE:
      this->verbose++;
      break;
    case OPT_BG_SLICES:
      this->bg_slices = optarg;
      break;
    case OPT_BG_SUBSAMPLE:
      this->bg_subsample = parse_bg_subsample(optarg);
      break;
    case OPT_FG_COLORS:
      this->fg_colors = parse_fg_colors(optarg);
      break;
    case OPT_MONOCHROME:
      this->monochrome = true;
      break;
    case OPT_LOSS_100:
      this->loss_level = 100;
      break;
    case OPT_LOSS_ANY:
      this->loss_level = string::as<int>(optarg);
      if (this->loss_level < 0)
        this->loss_level = 0;
      else if (this->loss_level > 200)
        this->loss_level = 200;
      break;
    case OPT_PAGES:
      parse_pages(optarg, this->pages);
      break;
    case OPT_ANTIALIAS:
      this->antialias = 1;
      break;
    case OPT_HYPERLINKS:
      parse_hyperlinks_options(optarg, this->hyperlinks);
      break;
    case OPT_NO_HLINKS:
      this->hyperlinks.extract = false;
      break;
    case OPT_NO_METADATA:
      this->extract_metadata = false;
      break;
    case OPT_VERBATIM_METADATA:
      this->adjust_metadata = false;
      break;
    case OPT_NO_OUTLINE:
      this->extract_outline = false;
      break;
    case OPT_NO_RENDER:
      this->no_render = true;
      this->monochrome = true;
      break;
    case OPT_TEXT_NONE:
      this->text = this->TEXT_NONE;
      break;
    case OPT_TEXT_WORDS:
      this->text = this->TEXT_WORDS;
      break;
    case OPT_TEXT_LINES:
      this->text = this->TEXT_LINES;
      break;
    case OPT_TEXT_NO_NFKC:
      this->text_nfkc = false;
      break;
    case OPT_TEXT_FILTER:
      this->text_nfkc = false; /* filter normally does some normalization on its own */
      this->text_filter_command_line = optarg;
      break;
    case OPT_TEXT_CROP:
      this->text_crop = true;
      break;
    case OPT_OUTPUT:
      this->format = this->FORMAT_BUNDLED;
      this->output = optarg;
      if (this->output == "-")
      {
        this->output.clear();
        this->output_stdout = true;
      }
      else
      {
        std::string directory_name, file_name;
        this->output_stdout = false;
        split_path(this->output, directory_name, file_name);
        if (file_name == "-")
        {
          /* ``djvmcvt`` does not support ``-`` as a file name.
           * Work-arounds are possible but not worthwhile.
           */
          throw Config::Error(_("Invalid output file name"));
        }
      }
      break;
    case OPT_INDIRECT:
      this->format = this->FORMAT_INDIRECT;
      this->output = optarg;
      this->output_stdout = false;
      break;
    case OPT_PAGE_ID_PREFIX:
      this->page_id_template.reset(this->default_page_id_template(optarg));
      validate_page_id_template(*this->page_id_template);
      break;
    case OPT_PAGE_ID_TEMPLATE:
      try
      {
        this->page_id_template.reset(new string_format::Template(optarg));
      }
      catch (string_format::ParseError)
      {
        throw Config::Error(_("Unable to parse page identifier template specification"));
      }
      validate_page_id_template(*this->page_id_template);
      break;
    case OPT_PAGE_TITLE_TEMPLATE:
      try
      {
        std::ostringstream sstream;
        sstream << encoding::proxy<encoding::native, encoding::utf8>(optarg);
        this->page_title_template.reset(
          new string_format::Template(sstream.str())
        );
      }
      catch (encoding::Error &exc)
      {
        throw Config::Error(string_printf(
          _("Unable to convert page title to UTF-8: %s"),
          exc.what())
        );
      }
      catch (string_format::ParseError)
      {
        throw Config::Error(
          _("Unable to parse page title template specification")
        );
      }
      break;
    case OPT_JOBS:
      this->n_jobs = string::as<int>(optarg);
      break;
    case OPT_HELP:
      throw NeedHelp();
    case OPT_VERSION:
      throw NeedVersion();
    case '?':
    case ':':
      throw InvalidOption();
    default:
      throw std::logic_error(_("Unknown option"));
    }
  }
  if (this->loss_level > 0 && !this->monochrome)
    throw Config::Error(_("--loss-level requires enabling --monochrome"));
  if (optind > argc - 1)
    throw Config::Error(_("No input file name was specified"));
  else
    while (optind < argc)
    {
      this->filenames.push_back(argv[optind]);
      if (is_same_file(this->output, argv[optind]))
        throw Config::Error(string_printf(
          _("Input file is the same as output file: %s"),
          this->output.c_str()
        ));
      optind++;
    }
}

void Config::usage(const Config::Error &error) const
{
  DebugStream &log = debug(0, this->verbose);
  if (error.is_already_printed())
    log << std::endl;
  if (!error.is_quiet())
    log << error << std::endl << std::endl;
  log
    << _("Usage: ") << std::endl
    << _("   pdf2djvu [-o <output-djvu-file>] [options] <pdf-file>") << std::endl
    << _("   pdf2djvu  -i <index-djvu-file>   [options] <pdf-file>") << std::endl
    << std::endl << _("Options: ")
    << std::endl << _(" -i, --indirect=FILE")
    << std::endl << _(" -o, --output=FILE")
    << std::endl << _("     --page-id-prefix=NAME")
    << std::endl << _("     --page-id-template=TEMPLATE")
    << std::endl << _("     --page-title-template=TEMPLATE")
    << std::endl << _(" -d, --dpi=RESOLUTION")
    << std::endl <<   "     --guess-dpi"
    << std::endl <<   "     --media-box"
    << std::endl << _("     --page-size=WxH")
    << std::endl <<   "     --bg-slices=N,...,N"
    << std::endl <<   "     --bg-slices=N+...+N"
    << std::endl <<   "     --bg-subsample=N"
    << std::endl <<   "     --fg-colors=default"
    << std::endl <<   "     --fg-colors=web"
    << std::endl <<   "     --fg-colors=black"
#if HAVE_GRAPHICSMAGICK
    << std::endl <<   "     --fg-colors=N"
#endif
    << std::endl <<   "     --monochrome"
    << std::endl <<   "     --loss-level=N"
    << std::endl <<   "     --lossy"
    << std::endl <<   "     --anti-alias"
    << std::endl <<   "     --no-metadata"
    << std::endl <<   "     --verbatim-metadata"
    << std::endl <<   "     --no-outline"
    << std::endl <<   "     --hyperlinks=border-avis"
    << std::endl <<   "     --hyperlinks=#RRGGBB"
    << std::endl <<   "     --no-hyperlinks"
    << std::endl <<   "     --no-text"
    << std::endl <<   "     --words"
    << std::endl <<   "     --lines"
    << std::endl <<   "     --crop-text"
    << std::endl <<   "     --no-nfkc"
    << std::endl << _("     --filter-text=COMMAND-LINE")
    << std::endl <<   " -p, --pages=..."
    << std::endl <<   " -v, --verbose"
#if _OPENMP
    << std::endl <<   " -j, --jobs=N"
#endif
    << std::endl <<   " -q, --quiet"
    << std::endl <<   " -h, --help"
    << std::endl <<   "     --version"
    << std::endl;
}

// vim:ts=2 sts=2 sw=2 et
