/*
 * Command-line driver for liblangid
 *
 * Marco Lui <saffsd@gmail.com>, September 2014
 *
 * Jonathan Graehl <graehl@gmail.com> 2017
 */

#include "liblangid.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

char const *getoptspec = "hpdlbmv:e:i:o:gj:D:L:f:I:F:";

void usage() {
  printf("Options (stdin/stdout): %s\n"
         "\n -v N: verbose level N"
         "\n -f: input from file instead of stdin"
         "\n -F: output instead of stdout"
         "\n -l: line-mode"
         "\n -b: batch-mode"
         "\n -g: grep-mode - keep lines that are ided as lang -e (default en)"
         "\n -i: additional input file (same lines get filtered) for grep-mode"
         "\n -o: filtered -i output filename - mandatory if -i"
         "\n -m: load model file"
         "\n -d: ignore [detok-marker] string"
         "\n -D: detok-marker"
         "\n -e: language to select; only output lines that get ided as e"
         "\n -I: if set, language to select for -i file (in addition to -e "
         "selection criteria on -f/stdin)"
         "\n -L: also keep lines with per-token logprob(e) - logprob(most "
         "likely) >= L, i.e. L<0 means tolerate 2nd place"
         "\n -j: rejected lines go here"
         "\n\n",
         getoptspec);
}

const char *no_file = "NOSUCHFILE";
const char *not_file = "NOTAFILE";

const char *lang;
size_t path_size = 4096, text_size = 4096;
ssize_t pathlen, textlen;
char *path = NULL,
     *text = NULL; /* NULL init required for use with getline/getdelim*/
LanguageIdentifier *lid;

/* for use while accessing files through mmap*/
int fd;

/* for use with getopt */
char *model_path = NULL;
int c, l_flag = 0, b_flag = 0, g_flag = 0, p_flag = 0, verbose = 0;
char *en = "en";
char *flang = NULL;
LangIndex f_index = (LangIndex)-1;
LangIndex en_index = (LangIndex)-1;
char *ff = NULL;
char *fin = NULL;
char *fout = NULL;
char *freject = NULL;
char *fF = NULL;
double min_logprob = -0.1;
double *logprobs = 0;
FILE *detectin = 0;
FILE *detectout = 0;
FILE *in = 0, *out = 0, *reject = 0;

char *detok_marker = "__LW_AT__";
unsigned len_detok_marker = 0;
int detok_flag;

char gotline(FILE *in) {
  textlen = getline(&text, &text_size, in);
  return textlen != -1;
}

void error(char const *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(-1);
}

FILE *openin(char const *name) {
  FILE *r = fopen(name, "r");
  if (r)
    return r;
  else {
    fprintf(stderr, "ERROR: couldn't open '%s'\n", name);
    exit(-1);
  }
}

void init() {
  /* load an identifier */
  lid = model_path ? load_identifier(model_path) : get_default_identifier();
  logprobs = malloc(sizeof(double) * lid->num_langs);
  en_index = get_lang_index(lid, en);
  if (detok_flag)
    len_detok_marker = strlen(detok_marker);

  detectout = fF ? fopen(fF, "w") : stdout;
  if (fin || fout) {
    if (fin && fout) {
      in = openin(fin);
      out = fopen(fout, "w");
    } else
      exit(-1);
  }
  if (flang) {
    if (!fin)
      error("must specify -i file for -f [language-id e.g. de]");
    else
      f_index = get_lang_index(lid, flang);
  }
  detectin = ff ? openin(ff) : stdin;
  reject = freject ? fopen(freject, "w") : 0;
}

char *dbuf = NULL;
ssize_t detok_text() {
  char *s = text;
  dbuf = realloc(dbuf, textlen + 1);
  char *o = dbuf;
  while (*s) {
    if (!strncmp(detok_marker, s, len_detok_marker)) {
      if (s != text && s[-1] == ' ')
        --o;
      s += len_detok_marker;
      if (*s == ' ')
        ++s;
    } else
      *o++ = *s++;
  }
  *o++ = 0;
  return o - dbuf;
}

LikelyLanguage langid_likely() {
  if (detok_flag) {
    ssize_t len = detok_text();
    return identify_likely_logprobs(lid, dbuf, len, logprobs);
  } else
    return identify_likely_logprobs(lid, text, textlen, logprobs);
}

char const *langid() { return lang = identify(lid, text, textlen); }

unsigned filtered = 0, total = 0;
char likely_enough(char const *lang, unsigned lang_index) {
  if (lang_index == (unsigned)-1)
    return 1;
  assert(lang);
  LikelyLanguage likely = langid_likely();
  normalize_logprobs_n(logprobs, lid->num_langs);
  double lpper = logprobs[lang_index];
  if (textlen)
    lpper /= textlen;
  char enough = textlen &&
                (likely.i == lang_index || (p_flag ? lpper >= min_logprob : 0));
  if (enough && verbose >= 1)
    fprintf(stderr, "%d %s %s=%.2f (/%d)\n", total, likely.lang, lang, lpper,
            (unsigned)textlen);
  else {
    ++filtered;
    char const *what = detok_flag ? dbuf : text;
    fprintf(stderr, "%d %s=%.2f (%.4f%%)\n", total, en, lpper,
            100. * filtered / total);
    if (reject)
      fprintf(reject, "%s!=%s %f %s", likely.lang, lang, lpper, what);
  }
  return enough;
}

int main(int argc, char **argv) {
  opterr = 0;

#ifdef DEBUG
  fprintf(stderr, "DEBUG MODE ENABLED\n");
#endif

  /* valid options are:
   * l: line-mode
   * b: batch-mode
   * m: load a model file
   */

  while ((c = getopt(argc, argv, getoptspec)) != -1)
    switch (c) {
    case 'F':
      fF = optarg;
      break;
    case 'f':
      ff = optarg;
      break;
    case 'h':
      usage();
      return 0;
    case 'v':
      verbose = atoi(optarg);
      break;
    case 'p':
      p_flag = 1;
      g_flag = 1;
      break;
    case 'L':
      min_logprob = strtod(optarg, NULL);
      p_flag = 1;
      g_flag = 1;
      break;
    case 'D':
      detok_marker = optarg;
      detok_flag = 1;
      break;
    case 'd':
      detok_flag = 1;
      break;
    case 'g':
      g_flag = 1;
      break;
    case 'j':
      g_flag = 1;
      freject = optarg;
      break;
    case 'e':
      g_flag = 1;
      en = optarg;
      break;
    case 'i':
      g_flag = 1;
      fin = optarg;
      break;
    case 'I':
      flang = optarg;
      break;
    case 'o':
      fout = optarg;
      break;
    case 'l':
      l_flag = 1;
      break;
    case 'b':
      b_flag = 1;
      break;
    case 'm':
      model_path = optarg;
      break;
    case '?':
      if (optopt == 'm')
        fprintf(stderr, "Option -%c requires an argument.\n", optopt);
      else if (isprint(optopt))
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
      else
        fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
      return 1;
    default:
      abort();
    }

  /* validate getopt options */
  if (l_flag && b_flag) {
    fprintf(stderr, "Cannot specify both -l and -b.\n");
    exit(-1);
  }

  /* enter appropriate operating mode.
   * we have an interactive mode determined by isatty, and then
   * the three modes are file-mode (default), line-mode and batch-mode
   */

  init();

  if (g_flag) {
    while (gotline(detectin)) {
      ++total;
      if (likely_enough(en, en_index)) {
        if (in) {
          fputs(text, detectout);
          if (gotline(in)) {
            if (likely_enough(flang, f_index))
              fputs(text, out);
          } else
            error("-i file had too few lines");
        } else
          fputs(text, detectout);
      } else if (in)
        gotline(in);
    }
  } else if (isatty(fileno(detectin))) {
    printf("langid.c interactive mode.\n");

    for (;;) {
      printf(">>> ");
      gotline(detectin);
      if (textlen == 1 || textlen == -1)
        break; /* -1 for EOF and 1 for only newline */
      lang = langid();
      printf("%s,%zd\n", lang, textlen);
    }

    printf("Bye!\n");

  } else if (l_flag) { /*line mode*/

    while (gotline(detectin)) {
      lang = langid();
      printf("%s,%zd\n", lang, textlen);
    }

  } else if (b_flag) { /*batch mode*/

    /* loop on detectin, interpreting each line as a path */
    while (gotline(detectin)) {
      path[textlen - 1] = '\0';
      /* TODO: ensure that path is a real file.
       * the main issue is with directories I think, no problem reading from a
       * pipe or socket presumably. Anything that returns data should be fair
       * game.*/
      if ((fd = open(path, O_RDONLY)) == -1) {
        lang = no_file;
      } else {
        textlen = lseek(fd, 0, SEEK_END);
        text = (char *)mmap(NULL, textlen, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                            fd, 0);
        lang = langid();

        /* no need to munmap if textlen is 0 */
        if (textlen && (munmap(text, textlen) == -1)) {
          fprintf(stderr, "failed to munmap %s of length %zd \n", path,
                  textlen);
          exit(-1);
        }

        close(fd);
      }
      printf("%s,%zd,%s\n", path, textlen, lang);
    }

  } else { /*file mode*/

    /* read all of detectin and process as a single file */
    textlen = getdelim(&text, &text_size, EOF, detectin);
    lang = langid();
    printf("%s,%zd\n", lang, textlen);
    free(text);
  }

  destroy_identifier(lid);
  if (reject)
    fclose(reject);
  if (out)
    fclose(out);
  return 0;
}
