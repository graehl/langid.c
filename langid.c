/*
 * Command-line driver for liblangid
 *
 * Marco Lui <saffsd@gmail.com>, September 2014
 *
 * Jonathan Graehl <graehl@gmail.com> 2017
 */

#include "liblangid.h"
#include <sys/mman.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char const* usagestr = "options: npdlbmv:e:i:o:g:j:D:L:\n";

void usage() {
  puts(usagestr);
}

const char* no_file = "NOSUCHFILE";
const char* not_file = "NOTAFILE";

const char* lang;
size_t path_size = 4096, text_size = 4096;
ssize_t pathlen, textlen;
char *path = NULL, *text = NULL; /* NULL init required for use with getline/getdelim*/
LanguageIdentifier* lid;

/* for use while accessing files through mmap*/
int fd;

/* for use with getopt */
char* model_path = NULL;
int c, l_flag = 0, b_flag = 0, g_flag = 0, p_flag = 0, verbose = 0;
char* en = "en";
LangIndex en_index = (LangIndex)-1;
char* fin = NULL;
char* fout = NULL;
char* freject = NULL;
double min_logprob = 0.1;
double* logprobs = 0;
FILE *in = 0, *out = 0, *reject = 0;

char* detok_marker = "__LW_AT__";
unsigned len_detok_marker = 0;
int detok_flag;

void init() {
  /* load an identifier */
  lid = model_path ? load_identifier(model_path) : get_default_identifier();
  logprobs = malloc(sizeof(double) * lid->num_langs);
  en_index = get_lang_index(lid, en);
  if (detok_flag) len_detok_marker = strlen(detok_marker);
  if (fin || fout) {
    if (fin && fout) {
      in = fopen(fin, "r");
      out = fopen(fout, "w");
    } else
      exit(-1);
  }
  if (freject) reject = fopen(freject, "w");
}

char* dbuf = NULL;
ssize_t detok_text() {
  char* s = text;
  dbuf = realloc(dbuf, textlen + 1);
  char* o = dbuf;
  while (*s) {
    if (!strncmp(detok_marker, s, len_detok_marker)) {
      if (s != text && s[-1] == ' ') --o;
      s += len_detok_marker;
      if (*s == ' ') ++s;
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

char const* langid() {
  return lang = identify(lid, text, textlen);
}

int main(int argc, char** argv) {
  opterr = 0;

#ifdef DEBUG
  fprintf(stderr, "DEBUG MODE ENABLED\n");
#endif

  /* valid options are:
   * l: line-mode
   * b: batch-mode
   * m: load a model file
   */

  while ((c = getopt(argc, argv, "hpdlbmv:e:i:o:g:j:D:L:")) != -1) switch (c) {
      case 'h': usage(); return 0;
      case 'v': verbose = atoi(optarg); break;
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
      case 'd': detok_flag = 1; break;
      case 'g': g_flag = 1; break;
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
      case 'o': fout = optarg; break;
      case 'l': l_flag = 1; break;
      case 'b': b_flag = 1; break;
      case 'm': model_path = optarg; break;
      case '?':
        if (optopt == 'm')
          fprintf(stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint(optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        return 1;
      default: abort();
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
    unsigned filtered = 0, total = 0;
    while ((textlen = getline(&text, &text_size, stdin)) != -1) {
      ++total;
      LikelyLanguage likely = langid_likely();
      normalize_logprobs_n(logprobs, lid->num_langs);
      double lpper = logprobs[en_index];
      if (textlen) lpper /= textlen;
      if (textlen && (likely.i == en_index || (p_flag ? lpper >= min_logprob : 0))) {
        if (verbose >= 1)
          fprintf(stderr, "%d %s %s=%.2f (/%d)\n", total, likely.lang, en, lpper, (unsigned)textlen);
        fputs(text, stdout);
        if (in && getline(&text, &text_size, in) != -1) fputs(text, out);
      } else {
        ++filtered;
        char const* what = detok_flag ? dbuf : text;
        fprintf(stderr, "%d %s=%.2f (%.4f%%)\n", total, en, lpper, 100. * filtered / total);
        if (reject) fprintf(reject, "%s %f %s", likely.lang, lpper, what);
        if (in) textlen = getline(&text, &text_size, in);
      }
    }
  } else if (isatty(fileno(stdin))) {
    printf("langid.c interactive mode.\n");

    for (;;) {
      printf(">>> ");
      textlen = getline(&text, &text_size, stdin);
      if (textlen == 1 || textlen == -1) break; /* -1 for EOF and 1 for only newline */
      lang = langid();
      printf("%s,%zd\n", lang, textlen);
    }

    printf("Bye!\n");

  } else if (l_flag) { /*line mode*/

    while ((textlen = getline(&text, &text_size, stdin)) != -1) {
      lang = langid();
      printf("%s,%zd\n", lang, textlen);
    }

  } else if (b_flag) { /*batch mode*/

    /* loop on stdin, interpreting each line as a path */
    while ((pathlen = getline(&path, &path_size, stdin)) != -1) {
      path[pathlen - 1] = '\0';
      /* TODO: ensure that path is a real file.
       * the main issue is with directories I think, no problem reading from a
       * pipe or socket presumably. Anything that returns data should be fair
       * game.*/
      if ((fd = open(path, O_RDONLY)) == -1) {
        lang = no_file;
      } else {
        textlen = lseek(fd, 0, SEEK_END);
        text = (char*)mmap(NULL, textlen, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        lang = langid();

        /* no need to munmap if textlen is 0 */
        if (textlen && (munmap(text, textlen) == -1)) {
          fprintf(stderr, "failed to munmap %s of length %zd \n", path, textlen);
          exit(-1);
        }

        close(fd);
      }
      printf("%s,%zd,%s\n", path, textlen, lang);
    }

  } else { /*file mode*/

    /* read all of stdin and process as a single file */
    textlen = getdelim(&text, &text_size, EOF, stdin);
    lang = langid();
    printf("%s,%zd\n", lang, textlen);
    free(text);
  }

  destroy_identifier(lid);
  if (reject) fclose(reject);
  if (out) fclose(out);
  return 0;
}
